/*
 * Copyright (C) the libgit2 contributors. All rights reserved.
 *
 * This file is part of libgit2, distributed under the GNU GPL v2 with
 * a Linking Exception. For full terms see the included COPYING file.
 */

#include "common.h"
#include "odb.h"
#include "pack.h"
#include "delta-apply.h"
#include "sha1_lookup.h"
#include "mwindow.h"
#include "fileops.h"

#include "git2/oid.h"
#include <zlib.h>

static int packfile_open(struct git_pack_file *p);
static git_off_t nth_packed_object_offset(const struct git_pack_file *p, uint32_t n);
int packfile_unpack_compressed(
		git_rawobj *obj,
		struct git_pack_file *p,
		git_mwindow **w_curs,
		git_off_t *curpos,
		size_t size,
		git_otype type);

/* Can find the offset of an object given
 * a prefix of an identifier.
 * Throws GIT_EAMBIGUOUSOIDPREFIX if short oid
 * is ambiguous within the pack.
 * This method assumes that len is between
 * GIT_OID_MINPREFIXLEN and GIT_OID_HEXSZ.
 */
static int pack_entry_find_offset(
		git_off_t *offset_out,
		git_oid *found_oid,
		struct git_pack_file *p,
		const git_oid *short_oid,
		size_t len);

static int packfile_error(const char *message)
{
	giterr_set(GITERR_ODB, "Invalid pack file - %s", message);
	return -1;
}

/********************
 * Delta base cache
 ********************/

static git_pack_cache_entry *new_cache_object(git_rawobj *source)
{
	git_pack_cache_entry *e = git__calloc(1, sizeof(git_pack_cache_entry));
	if (!e)
		return NULL;

	memcpy(&e->raw, source, sizeof(git_rawobj));

	return e;
}

static void free_cache_object(void *o)
{
	git_pack_cache_entry *e = (git_pack_cache_entry *)o;

	if (e != NULL) {
		assert(e->refcount.val == 0);
		git__free(e->raw.data);
		git__free(e);
	}
}

static void cache_free(git_pack_cache *cache)
{
	khiter_t k;

	if (cache->entries) {
		for (k = kh_begin(cache->entries); k != kh_end(cache->entries); k++) {
			if (kh_exist(cache->entries, k))
				free_cache_object(kh_value(cache->entries, k));
		}

		git_offmap_free(cache->entries);
		git_mutex_free(&cache->lock);
	}
}

static int cache_init(git_pack_cache *cache)
{
	memset(cache, 0, sizeof(git_pack_cache));
	cache->entries = git_offmap_alloc();
	GITERR_CHECK_ALLOC(cache->entries);
	cache->memory_limit = GIT_PACK_CACHE_MEMORY_LIMIT;
	git_mutex_init(&cache->lock);

	return 0;
}

static git_pack_cache_entry *cache_get(git_pack_cache *cache, git_off_t offset)
{
	khiter_t k;
	git_pack_cache_entry *entry = NULL;

	if (git_mutex_lock(&cache->lock) < 0)
		return NULL;

	k = kh_get(off, cache->entries, offset);
	if (k != kh_end(cache->entries)) { /* found it */
		entry = kh_value(cache->entries, k);
		git_atomic_inc(&entry->refcount);
		entry->last_usage = cache->use_ctr++;
	}
	git_mutex_unlock(&cache->lock);

	return entry;
}

/* Run with the cache lock held */
static void free_lowest_entry(git_pack_cache *cache)
{
	git_pack_cache_entry *entry;
	khiter_t k;

	for (k = kh_begin(cache->entries); k != kh_end(cache->entries); k++) {
		if (!kh_exist(cache->entries, k))
			continue;

		entry = kh_value(cache->entries, k);

		if (entry && entry->refcount.val == 0) {
			cache->memory_used -= entry->raw.len;
			kh_del(off, cache->entries, k);
			free_cache_object(entry);
		}
	}
}

static int cache_add(git_pack_cache *cache, git_rawobj *base, git_off_t offset)
{
	git_pack_cache_entry *entry;
	int error, exists = 0;
	khiter_t k;

	if (base->len > GIT_PACK_CACHE_SIZE_LIMIT)
		return -1;

	entry = new_cache_object(base);
	if (entry) {
		if (git_mutex_lock(&cache->lock) < 0) {
			giterr_set(GITERR_OS, "failed to lock cache");
			return -1;
		}
		/* Add it to the cache if nobody else has */
		exists = kh_get(off, cache->entries, offset) != kh_end(cache->entries);
		if (!exists) {
			while (cache->memory_used + base->len > cache->memory_limit)
				free_lowest_entry(cache);

			k = kh_put(off, cache->entries, offset, &error);
			assert(error != 0);
			kh_value(cache->entries, k) = entry;
			cache->memory_used += entry->raw.len;
		}
		git_mutex_unlock(&cache->lock);
		/* Somebody beat us to adding it into the cache */
		if (exists) {
			git__free(entry);
			return -1;
		}
	}

	return 0;
}

/***********************************************************
 *
 * PACK INDEX METHODS
 *
 ***********************************************************/

static void pack_index_free(struct git_pack_file *p)
{
	if (p->oids) {
		git__free(p->oids);
		p->oids = NULL;
	}
	if (p->index_map.data) {
		git_futils_mmap_free(&p->index_map);
		p->index_map.data = NULL;
	}
}

static int pack_index_check(const char *path, struct git_pack_file *p)
{
	struct git_pack_idx_header *hdr;
	uint32_t version, nr, i, *index;
	void *idx_map;
	size_t idx_size;
	struct stat st;
	int error;
	/* TODO: properly open the file without access time using O_NOATIME */
	git_file fd = git_futils_open_ro(path);
	if (fd < 0)
		return fd;

	if (p_fstat(fd, &st) < 0 ||
		!S_ISREG(st.st_mode) ||
		!git__is_sizet(st.st_size) ||
		(idx_size = (size_t)st.st_size) < 4 * 256 + 20 + 20)
	{
		p_close(fd);
		giterr_set(GITERR_OS, "Failed to check pack index.");
		return -1;
	}

	error = git_futils_mmap_ro(&p->index_map, fd, 0, idx_size);

	p_close(fd);

	if (error < 0)
		return error;

	hdr = idx_map = p->index_map.data;

	if (hdr->idx_signature == htonl(PACK_IDX_SIGNATURE)) {
		version = ntohl(hdr->idx_version);

		if (version < 2 || version > 2) {
			git_futils_mmap_free(&p->index_map);
			return packfile_error("unsupported index version");
		}

	} else
		version = 1;

	nr = 0;
	index = idx_map;

	if (version > 1)
		index += 2; /* skip index header */

	for (i = 0; i < 256; i++) {
		uint32_t n = ntohl(index[i]);
		if (n < nr) {
			git_futils_mmap_free(&p->index_map);
			return packfile_error("index is non-monotonic");
		}
		nr = n;
	}

	if (version == 1) {
		/*
		 * Total size:
		 * - 256 index entries 4 bytes each
		 * - 24-byte entries * nr (20-byte sha1 + 4-byte offset)
		 * - 20-byte SHA1 of the packfile
		 * - 20-byte SHA1 file checksum
		 */
		if (idx_size != 4*256 + nr * 24 + 20 + 20) {
			git_futils_mmap_free(&p->index_map);
			return packfile_error("index is corrupted");
		}
	} else if (version == 2) {
		/*
		 * Minimum size:
		 * - 8 bytes of header
		 * - 256 index entries 4 bytes each
		 * - 20-byte sha1 entry * nr
		 * - 4-byte crc entry * nr
		 * - 4-byte offset entry * nr
		 * - 20-byte SHA1 of the packfile
		 * - 20-byte SHA1 file checksum
		 * And after the 4-byte offset table might be a
		 * variable sized table containing 8-byte entries
		 * for offsets larger than 2^31.
		 */
		unsigned long min_size = 8 + 4*256 + nr*(20 + 4 + 4) + 20 + 20;
		unsigned long max_size = min_size;

		if (nr)
			max_size += (nr - 1)*8;

		if (idx_size < min_size || idx_size > max_size) {
			git_futils_mmap_free(&p->index_map);
			return packfile_error("wrong index size");
		}
	}

	p->index_version = version;
	p->num_objects = nr;
	return 0;
}

static int pack_index_open(struct git_pack_file *p)
{
	char *idx_name;
	int error;
	size_t name_len, offset;

	if (p->index_map.data)
		return 0;

	idx_name = git__strdup(p->pack_name);
	GITERR_CHECK_ALLOC(idx_name);

	name_len = strlen(idx_name);
	offset = name_len - strlen(".pack");
	assert(offset < name_len); /* make sure no underflow */

	strncpy(idx_name + offset, ".idx", name_len - offset);

	error = pack_index_check(idx_name, p);
	git__free(idx_name);

	return error;
}

static unsigned char *pack_window_open(
		struct git_pack_file *p,
		git_mwindow **w_cursor,
		git_off_t offset,
		unsigned int *left)
{
	if (p->mwf.fd == -1 && packfile_open(p) < 0)
		return NULL;

	/* Since packfiles end in a hash of their content and it's
	 * pointless to ask for an offset into the middle of that
	 * hash, and the pack_window_contains function above wouldn't match
	 * don't allow an offset too close to the end of the file.
	 */
	if (offset > (p->mwf.size - 20))
		return NULL;

	return git_mwindow_open(&p->mwf, w_cursor, offset, 20, left);
 }

static int packfile_unpack_header1(
		unsigned long *usedp,
		size_t *sizep,
		git_otype *type,
		const unsigned char *buf,
		unsigned long len)
{
	unsigned shift;
	unsigned long size, c;
	unsigned long used = 0;

	c = buf[used++];
	*type = (c >> 4) & 7;
	size = c & 15;
	shift = 4;
	while (c & 0x80) {
		if (len <= used)
			return GIT_EBUFS;

		if (bitsizeof(long) <= shift) {
			*usedp = 0;
			return -1;
		}

		c = buf[used++];
		size += (c & 0x7f) << shift;
		shift += 7;
	}

	*sizep = (size_t)size;
	*usedp = used;
	return 0;
}

int git_packfile_unpack_header(
		size_t *size_p,
		git_otype *type_p,
		git_mwindow_file *mwf,
		git_mwindow **w_curs,
		git_off_t *curpos)
{
	unsigned char *base;
	unsigned int left;
	unsigned long used;
	int ret;

	/* pack_window_open() assures us we have [base, base + 20) available
	 * as a range that we can look at at. (Its actually the hash
	 * size that is assured.) With our object header encoding
	 * the maximum deflated object size is 2^137, which is just
	 * insane, so we know won't exceed what we have been given.
	 */
//	base = pack_window_open(p, w_curs, *curpos, &left);
	base = git_mwindow_open(mwf, w_curs, *curpos, 20, &left);
	if (base == NULL)
		return GIT_EBUFS;

		ret = packfile_unpack_header1(&used, size_p, type_p, base, left);
	git_mwindow_close(w_curs);
	if (ret == GIT_EBUFS)
		return ret;
	else if (ret < 0)
		return packfile_error("header length is zero");

	*curpos += used;
	return 0;
}

int git_packfile_resolve_header(
		size_t *size_p,
		git_otype *type_p,
		struct git_pack_file *p,
		git_off_t offset)
{
	git_mwindow *w_curs = NULL;
	git_off_t curpos = offset;
	size_t size;
	git_otype type;
	git_off_t base_offset;
	int error;

	error = git_packfile_unpack_header(&size, &type, &p->mwf, &w_curs, &curpos);
	git_mwindow_close(&w_curs);
	if (error < 0)
		return error;

	if (type == GIT_OBJ_OFS_DELTA || type == GIT_OBJ_REF_DELTA) {
		size_t base_size;
		git_rawobj delta;
		base_offset = get_delta_base(p, &w_curs, &curpos, type, offset);
		git_mwindow_close(&w_curs);
		error = packfile_unpack_compressed(&delta, p, &w_curs, &curpos, size, type);
		git_mwindow_close(&w_curs);
		if (error < 0)
			return error;
		error = git__delta_read_header(delta.data, delta.len, &base_size, size_p);
		git__free(delta.data);
		if (error < 0)
			return error;
	} else
		*size_p = size;

	while (type == GIT_OBJ_OFS_DELTA || type == GIT_OBJ_REF_DELTA) {
		curpos = base_offset;
		error = git_packfile_unpack_header(&size, &type, &p->mwf, &w_curs, &curpos);
		git_mwindow_close(&w_curs);
		if (error < 0)
			return error;
		if (type != GIT_OBJ_OFS_DELTA && type != GIT_OBJ_REF_DELTA)
			break;
		base_offset = get_delta_base(p, &w_curs, &curpos, type, base_offset);
		git_mwindow_close(&w_curs);
	}
	*type_p = type;

	return error;
}

static int packfile_unpack_delta(
		git_rawobj *obj,
		struct git_pack_file *p,
		git_mwindow **w_curs,
		git_off_t *curpos,
		size_t delta_size,
		git_otype delta_type,
		git_off_t obj_offset)
{
	git_off_t base_offset, base_key;
	git_rawobj base, delta;
	git_pack_cache_entry *cached = NULL;
	int error, found_base = 0;

	base_offset = get_delta_base(p, w_curs, curpos, delta_type, obj_offset);
	git_mwindow_close(w_curs);
	if (base_offset == 0)
		return packfile_error("delta offset is zero");
	if (base_offset < 0) /* must actually be an error code */
		return (int)base_offset;

	if (!p->bases.entries && (cache_init(&p->bases) < 0))
		return -1;

	base_key = base_offset; /* git_packfile_unpack modifies base_offset */
	if ((cached = cache_get(&p->bases, base_offset)) != NULL) {
		memcpy(&base, &cached->raw, sizeof(git_rawobj));
		found_base = 1;
	}

	if (!cached) { /* have to inflate it */
		error = git_packfile_unpack(&base, p, &base_offset);

		/*
		 * TODO: git.git tries to load the base from other packfiles
		 * or loose objects.
		 *
		 * We'll need to do this in order to support thin packs.
		 */
		if (error < 0)
			return error;
	}

	error = packfile_unpack_compressed(&delta, p, w_curs, curpos, delta_size, delta_type);
	git_mwindow_close(w_curs);

	if (error < 0) {
		if (!found_base)
			git__free(base.data);
		return error;
	}

	obj->type = base.type;
	error = git__delta_apply(obj, base.data, base.len, delta.data, delta.len);
	if (error < 0)
		goto on_error;

	if (found_base)
		git_atomic_dec(&cached->refcount);
	else if (cache_add(&p->bases, &base, base_key) < 0)
		git__free(base.data);

on_error:
	git__free(delta.data);

	return error; /* error set by git__delta_apply */
}

int git_packfile_unpack(
	git_rawobj *obj,
	struct git_pack_file *p,
	git_off_t *obj_offset)
{
	git_mwindow *w_curs = NULL;
	git_off_t curpos = *obj_offset;
	int error;

	size_t size = 0;
	git_otype type;

	/*
	 * TODO: optionally check the CRC on the packfile
	 */

	obj->data = NULL;
	obj->len = 0;
	obj->type = GIT_OBJ_BAD;

	error = git_packfile_unpack_header(&size, &type, &p->mwf, &w_curs, &curpos);
	git_mwindow_close(&w_curs);

	if (error < 0)
		return error;

	switch (type) {
	case GIT_OBJ_OFS_DELTA:
	case GIT_OBJ_REF_DELTA:
		error = packfile_unpack_delta(
				obj, p, &w_curs, &curpos,
				size, type, *obj_offset);
		break;

	case GIT_OBJ_COMMIT:
	case GIT_OBJ_TREE:
	case GIT_OBJ_BLOB:
	case GIT_OBJ_TAG:
		error = packfile_unpack_compressed(
				obj, p, &w_curs, &curpos,
				size, type);
		break;

	default:
		error = packfile_error("invalid packfile type in header");;
		break;
	}

	*obj_offset = curpos;
	return error;
}

static void *use_git_alloc(void *opaq, unsigned int count, unsigned int size)
{
	GIT_UNUSED(opaq);
	return git__calloc(count, size);
}

static void use_git_free(void *opaq, void *ptr)
{
	GIT_UNUSED(opaq);
	git__free(ptr);
}

int git_packfile_stream_open(git_packfile_stream *obj, struct git_pack_file *p, git_off_t curpos)
{
	int st;

	memset(obj, 0, sizeof(git_packfile_stream));
	obj->curpos = curpos;
	obj->p = p;
	obj->zstream.zalloc = use_git_alloc;
	obj->zstream.zfree = use_git_free;
	obj->zstream.next_in = Z_NULL;
	obj->zstream.next_out = Z_NULL;
	st = inflateInit(&obj->zstream);
	if (st != Z_OK) {
		git__free(obj);
		giterr_set(GITERR_ZLIB, "Failed to inflate packfile");
		return -1;
	}

	return 0;
}

ssize_t git_packfile_stream_read(git_packfile_stream *obj, void *buffer, size_t len)
{
	unsigned char *in;
	size_t written;
	int st;

	if (obj->done)
		return 0;

	in = pack_window_open(obj->p, &obj->mw, obj->curpos, &obj->zstream.avail_in);
	if (in == NULL)
		return GIT_EBUFS;

	obj->zstream.next_out = buffer;
	obj->zstream.avail_out = (unsigned int)len;
	obj->zstream.next_in = in;

	st = inflate(&obj->zstream, Z_SYNC_FLUSH);
	git_mwindow_close(&obj->mw);

	obj->curpos += obj->zstream.next_in - in;
	written = len - obj->zstream.avail_out;

	if (st != Z_OK && st != Z_STREAM_END) {
		giterr_set(GITERR_ZLIB, "Failed to inflate packfile");
		return -1;
	}

	if (st == Z_STREAM_END)
		obj->done = 1;


	/* If we didn't write anything out but we're not done, we need more data */
	if (!written && st != Z_STREAM_END)
		return GIT_EBUFS;

	return written;

}

void git_packfile_stream_free(git_packfile_stream *obj)
{
	inflateEnd(&obj->zstream);
}

int packfile_unpack_compressed(
	git_rawobj *obj,
	struct git_pack_file *p,
	git_mwindow **w_curs,
	git_off_t *curpos,
	size_t size,
	git_otype type)
{
	int st;
	z_stream stream;
	unsigned char *buffer, *in;

	buffer = git__calloc(1, size + 1);
	GITERR_CHECK_ALLOC(buffer);

	memset(&stream, 0, sizeof(stream));
	stream.next_out = buffer;
	stream.avail_out = (uInt)size + 1;
	stream.zalloc = use_git_alloc;
	stream.zfree = use_git_free;

	st = inflateInit(&stream);
	if (st != Z_OK) {
		git__free(buffer);
		giterr_set(GITERR_ZLIB, "Failed to inflate packfile");

		return -1;
	}

	do {
		in = pack_window_open(p, w_curs, *curpos, &stream.avail_in);
		stream.next_in = in;
		st = inflate(&stream, Z_FINISH);
		git_mwindow_close(w_curs);

		if (!stream.avail_out)
			break; /* the payload is larger than it should be */

		if (st == Z_BUF_ERROR && in == NULL) {
			inflateEnd(&stream);
			git__free(buffer);
			return GIT_EBUFS;
		}

		*curpos += stream.next_in - in;
	} while (st == Z_OK || st == Z_BUF_ERROR);

	inflateEnd(&stream);

	if ((st != Z_STREAM_END) || stream.total_out != size) {
		git__free(buffer);
		giterr_set(GITERR_ZLIB, "Failed to inflate packfile");
		return -1;
	}

	obj->type = type;
	obj->len = size;
	obj->data = buffer;
	return 0;
}

/*
 * curpos is where the data starts, delta_obj_offset is the where the
 * header starts
 */
git_off_t get_delta_base(
	struct git_pack_file *p,
	git_mwindow **w_curs,
	git_off_t *curpos,
	git_otype type,
	git_off_t delta_obj_offset)
{
	unsigned int left = 0;
	unsigned char *base_info;
	git_off_t base_offset;
	git_oid unused;

	base_info = pack_window_open(p, w_curs, *curpos, &left);
	/* Assumption: the only reason this would fail is because the file is too small */
	if (base_info == NULL)
		return GIT_EBUFS;
	/* pack_window_open() assured us we have [base_info, base_info + 20)
	 * as a range that we can look at without walking off the
	 * end of the mapped window. Its actually the hash size
	 * that is assured. An OFS_DELTA longer than the hash size
	 * is stupid, as then a REF_DELTA would be smaller to store.
	 */
	if (type == GIT_OBJ_OFS_DELTA) {
		unsigned used = 0;
		unsigned char c = base_info[used++];
		base_offset = c & 127;
		while (c & 128) {
			if (left <= used)
				return GIT_EBUFS;
			base_offset += 1;
			if (!base_offset || MSB(base_offset, 7))
				return 0; /* overflow */
			c = base_info[used++];
			base_offset = (base_offset << 7) + (c & 127);
		}
		base_offset = delta_obj_offset - base_offset;
		if (base_offset <= 0 || base_offset >= delta_obj_offset)
			return 0; /* out of bound */
		*curpos += used;
	} else if (type == GIT_OBJ_REF_DELTA) {
		/* If we have the cooperative cache, search in it first */
		if (p->has_cache) {
			size_t pos;
			struct git_pack_entry key;

			git_oid_fromraw(&key.sha1, base_info);
			if (!git_vector_bsearch(&pos, &p->cache, &key)) {
				*curpos += 20;
				return ((struct git_pack_entry *)git_vector_get(&p->cache, pos))->offset;
			}
		}
		/* The base entry _must_ be in the same pack */
		if (pack_entry_find_offset(&base_offset, &unused, p, (git_oid *)base_info, GIT_OID_HEXSZ) < 0)
			return packfile_error("base entry delta is not in the same pack");
		*curpos += 20;
	} else
		return 0;

	return base_offset;
}

/***********************************************************
 *
 * PACKFILE METHODS
 *
 ***********************************************************/

static struct git_pack_file *packfile_alloc(size_t extra)
{
	struct git_pack_file *p = git__calloc(1, sizeof(*p) + extra);
	if (p != NULL)
		p->mwf.fd = -1;
	return p;
}


void git_packfile_free(struct git_pack_file *p)
{
	assert(p);

	cache_free(&p->bases);

	git_mwindow_free_all(&p->mwf);
	git_mwindow_file_deregister(&p->mwf);

	if (p->mwf.fd != -1)
		p_close(p->mwf.fd);

	pack_index_free(p);

	git__free(p->bad_object_sha1);
	git__free(p);
}

static int packfile_open(struct git_pack_file *p)
{
	struct stat st;
	struct git_pack_header hdr;
	git_oid sha1;
	unsigned char *idx_sha1;

	assert(p->index_map.data);

	if (!p->index_map.data && pack_index_open(p) < 0)
		return git_odb__error_notfound("failed to open packfile", NULL);

	/* TODO: open with noatime */
	p->mwf.fd = git_futils_open_ro(p->pack_name);
	if (p->mwf.fd < 0) {
		p->mwf.fd = -1;
		return -1;
	}

	if (p_fstat(p->mwf.fd, &st) < 0 ||
		git_mwindow_file_register(&p->mwf) < 0)
		goto cleanup;

	/* If we created the struct before we had the pack we lack size. */
	if (!p->mwf.size) {
		if (!S_ISREG(st.st_mode))
			goto cleanup;
		p->mwf.size = (git_off_t)st.st_size;
	} else if (p->mwf.size != st.st_size)
		goto cleanup;

#if 0
	/* We leave these file descriptors open with sliding mmap;
	 * there is no point keeping them open across exec(), though.
	 */
	fd_flag = fcntl(p->mwf.fd, F_GETFD, 0);
	if (fd_flag < 0)
		goto cleanup;

	fd_flag |= FD_CLOEXEC;
	if (fcntl(p->pack_fd, F_SETFD, fd_flag) == -1)
		goto cleanup;
#endif

	/* Verify we recognize this pack file format. */
	if (p_read(p->mwf.fd, &hdr, sizeof(hdr)) < 0 ||
		hdr.hdr_signature != htonl(PACK_SIGNATURE) ||
		!pack_version_ok(hdr.hdr_version))
		goto cleanup;

	/* Verify the pack matches its index. */
	if (p->num_objects != ntohl(hdr.hdr_entries) ||
		p_lseek(p->mwf.fd, p->mwf.size - GIT_OID_RAWSZ, SEEK_SET) == -1 ||
		p_read(p->mwf.fd, sha1.id, GIT_OID_RAWSZ) < 0)
		goto cleanup;

	idx_sha1 = ((unsigned char *)p->index_map.data) + p->index_map.len - 40;

	if (git_oid_cmp(&sha1, (git_oid *)idx_sha1) == 0)
		return 0;

cleanup:
	giterr_set(GITERR_OS, "Invalid packfile '%s'", p->pack_name);
	p_close(p->mwf.fd);
	p->mwf.fd = -1;
	return -1;
}

int git_packfile_check(struct git_pack_file **pack_out, const char *path)
{
	struct stat st;
	struct git_pack_file *p;
	size_t path_len;

	*pack_out = NULL;
	path_len = strlen(path);
	p = packfile_alloc(path_len + 2);
	GITERR_CHECK_ALLOC(p);

	/*
	 * Make sure a corresponding .pack file exists and that
	 * the index looks sane.
	 */
	path_len -= strlen(".idx");
	if (path_len < 1) {
		git__free(p);
		return git_odb__error_notfound("invalid packfile path", NULL);
	}

	memcpy(p->pack_name, path, path_len);

	strcpy(p->pack_name + path_len, ".keep");
	if (git_path_exists(p->pack_name) == true)
		p->pack_keep = 1;

	strcpy(p->pack_name + path_len, ".pack");
	if (p_stat(p->pack_name, &st) < 0 || !S_ISREG(st.st_mode)) {
		git__free(p);
		return git_odb__error_notfound("packfile not found", NULL);
	}

	/* ok, it looks sane as far as we can check without
	 * actually mapping the pack file.
	 */
	p->mwf.size = st.st_size;
	p->pack_local = 1;
	p->mtime = (git_time_t)st.st_mtime;

	/* see if we can parse the sha1 oid in the packfile name */
	if (path_len < 40 ||
		git_oid_fromstr(&p->sha1, path + path_len - GIT_OID_HEXSZ) < 0)
		memset(&p->sha1, 0x0, GIT_OID_RAWSZ);

	*pack_out = p;

	return 0;
}

/***********************************************************
 *
 * PACKFILE ENTRY SEARCH INTERNALS
 *
 ***********************************************************/

static git_off_t nth_packed_object_offset(const struct git_pack_file *p, uint32_t n)
{
	const unsigned char *index = p->index_map.data;
	index += 4 * 256;
	if (p->index_version == 1) {
		return ntohl(*((uint32_t *)(index + 24 * n)));
	} else {
		uint32_t off;
		index += 8 + p->num_objects * (20 + 4);
		off = ntohl(*((uint32_t *)(index + 4 * n)));
		if (!(off & 0x80000000))
			return off;
		index += p->num_objects * 4 + (off & 0x7fffffff) * 8;
		return (((uint64_t)ntohl(*((uint32_t *)(index + 0)))) << 32) |
					ntohl(*((uint32_t *)(index + 4)));
	}
}

static int git__memcmp4(const void *a, const void *b) {
	return memcmp(a, b, 4);
}

int git_pack_foreach_entry(
	struct git_pack_file *p,
	git_odb_foreach_cb cb,
	void *data)
{
	const unsigned char *index = p->index_map.data, *current;
	uint32_t i;

	if (index == NULL) {
		int error;

		if ((error = pack_index_open(p)) < 0)
			return error;

		assert(p->index_map.data);

		index = p->index_map.data;
	}

	if (p->index_version > 1) {
		index += 8;
	}

	index += 4 * 256;

	if (p->oids == NULL) {
		git_vector offsets, oids;
		int error;

		if ((error = git_vector_init(&oids, p->num_objects, NULL)))
			return error;

		if ((error = git_vector_init(&offsets, p->num_objects, git__memcmp4)))
			return error;

		if (p->index_version > 1) {
			const unsigned char *off = index + 24 * p->num_objects;
			for (i = 0; i < p->num_objects; i++)
				git_vector_insert(&offsets, (void*)&off[4 * i]);
			git_vector_sort(&offsets);
			git_vector_foreach(&offsets, i, current)
				git_vector_insert(&oids, (void*)&index[5 * (current - off)]);
		} else {
			for (i = 0; i < p->num_objects; i++)
				git_vector_insert(&offsets, (void*)&index[24 * i]);
			git_vector_sort(&offsets);
			git_vector_foreach(&offsets, i, current)
				git_vector_insert(&oids, (void*)&current[4]);
		}
		git_vector_free(&offsets);
		p->oids = (git_oid **)oids.contents;
	}

	for (i = 0; i < p->num_objects; i++)
		if (cb(p->oids[i], data))
			return GIT_EUSER;

	return 0;
}

static int pack_entry_find_offset(
	git_off_t *offset_out,
	git_oid *found_oid,
	struct git_pack_file *p,
	const git_oid *short_oid,
	size_t len)
{
	const uint32_t *level1_ofs = p->index_map.data;
	const unsigned char *index = p->index_map.data;
	unsigned hi, lo, stride;
	int pos, found = 0;
	const unsigned char *current = 0;

	*offset_out = 0;

	if (index == NULL) {
		int error;

		if ((error = pack_index_open(p)) < 0)
			return error;

		assert(p->index_map.data);

		index = p->index_map.data;
		level1_ofs = p->index_map.data;
	}

	if (p->index_version > 1) {
		level1_ofs += 2;
		index += 8;
	}

	index += 4 * 256;
	hi = ntohl(level1_ofs[(int)short_oid->id[0]]);
	lo = ((short_oid->id[0] == 0x0) ? 0 : ntohl(level1_ofs[(int)short_oid->id[0] - 1]));

	if (p->index_version > 1) {
		stride = 20;
	} else {
		stride = 24;
		index += 4;
	}

#ifdef INDEX_DEBUG_LOOKUP
	printf("%02x%02x%02x... lo %u hi %u nr %d\n",
		short_oid->id[0], short_oid->id[1], short_oid->id[2], lo, hi, p->num_objects);
#endif

	/* Use git.git lookup code */
	pos = sha1_entry_pos(index, stride, 0, lo, hi, p->num_objects, short_oid->id);

	if (pos >= 0) {
		/* An object matching exactly the oid was found */
		found = 1;
		current = index + pos * stride;
	} else {
		/* No object was found */
		/* pos refers to the object with the "closest" oid to short_oid */
		pos = - 1 - pos;
		if (pos < (int)p->num_objects) {
			current = index + pos * stride;

			if (!git_oid_ncmp(short_oid, (const git_oid *)current, len))
				found = 1;
		}
	}

	if (found && len != GIT_OID_HEXSZ && pos + 1 < (int)p->num_objects) {
		/* Check for ambiguousity */
		const unsigned char *next = current + stride;

		if (!git_oid_ncmp(short_oid, (const git_oid *)next, len)) {
			found = 2;
		}
	}

	if (!found)
		return git_odb__error_notfound("failed to find offset for pack entry", short_oid);
	if (found > 1)
		return git_odb__error_ambiguous("found multiple offsets for pack entry");
	*offset_out = nth_packed_object_offset(p, pos);
	git_oid_fromraw(found_oid, current);

#ifdef INDEX_DEBUG_LOOKUP
	{
		unsigned char hex_sha1[GIT_OID_HEXSZ + 1];
		git_oid_fmt(hex_sha1, found_oid);
		hex_sha1[GIT_OID_HEXSZ] = '\0';
		printf("found lo=%d %s\n", lo, hex_sha1);
	}
#endif
	return 0;
}

int git_pack_entry_find(
		struct git_pack_entry *e,
		struct git_pack_file *p,
		const git_oid *short_oid,
		size_t len)
{
	git_off_t offset;
	git_oid found_oid;
	int error;

	assert(p);

	if (len == GIT_OID_HEXSZ && p->num_bad_objects) {
		unsigned i;
		for (i = 0; i < p->num_bad_objects; i++)
			if (git_oid_cmp(short_oid, &p->bad_object_sha1[i]) == 0)
				return packfile_error("bad object found in packfile");
	}

	error = pack_entry_find_offset(&offset, &found_oid, p, short_oid, len);
	if (error < 0)
		return error;

	/* we found a unique entry in the index;
	 * make sure the packfile backing the index
	 * still exists on disk */
	if (p->mwf.fd == -1 && (error = packfile_open(p)) < 0)
		return error;

	e->offset = offset;
	e->p = p;

	git_oid_cpy(&e->sha1, &found_oid);
	return 0;
}
