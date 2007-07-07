// TortoiseSVN - a Windows shell extension for easy version control

// Copyright (C) 2003-2007 - Stefan Kueng

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
#include "StdAfx.h"
#include "resource.h"
#include "client.h"
#include "UnicodeUtils.h"
#include "registry.h"
#include "AppUtils.h"
#include "PathUtils.h"
#include "SVN.h"
#include "TSVNPath.h"
#include ".\revisiongraph.h"
#include "SVNError.h"
#include "CachedLogInfo.h"
#include "RevisionIndex.h"
#include "CopyFollowingLogIterator.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

void CSearchPathTree::DeLink()
{
	assert (parent);

	if (previous)
		previous->next = next;
	if (next)
		next->previous = previous;

	if (parent->firstChild == this)
		parent->firstChild = next;
	if (parent->lastChild == this)
		parent->lastChild = previous;

	parent = NULL;
}

void CSearchPathTree::Link (CSearchPathTree* newParent)
{
	assert (parent == NULL);
	assert (newParent != NULL);

	parent = newParent;

	previous = parent->lastChild;

	if (parent->firstChild == NULL)
		parent->firstChild = this;
	else
		parent->lastChild->next = this;

	parent->lastChild = this;
}

// construction / destruction

CSearchPathTree::CSearchPathTree (const CPathDictionary* dictionary)
	: path (dictionary, std::string())
	, startRevision ((revision_t)NO_REVISION)
	, lastEntry (NULL)
	, parent (NULL)
	, firstChild (NULL)
	, lastChild (NULL)
	, previous (NULL)
	, next (NULL)
{
}

CSearchPathTree::CSearchPathTree ( const CDictionaryBasedTempPath& path
								 , revision_t startrev
								 , CSearchPathTree* parent)
	: path (path)
	, startRevision (startrev)
	, lastEntry (NULL)
	, parent (NULL)
	, firstChild (NULL)
	, lastChild (NULL)
	, previous (NULL)
	, next (NULL)
{
	Link (parent);
}

CSearchPathTree::~CSearchPathTree()
{
	while (firstChild != NULL)
		delete firstChild;

	if (parent)
		DeLink();
}

// add a node for the given path and rev. to the tree

CSearchPathTree* CSearchPathTree::Insert ( const CDictionaryBasedTempPath& path
										 , revision_t startrev)
{
	assert (startrev != NO_REVISION);

	// exact match (will happen on root node only)?

	if (this->path == path)
	{
		startRevision = startrev;
		return this;
	}

	// (partly or fully) overlap with an existing child?

	for (CSearchPathTree* child = firstChild; child != NULL; child = child->next)
	{
		CDictionaryBasedTempPath commonPath = child->path.GetCommonRoot (path);

		if (commonPath != this->path)
		{
			if (child->path == path)
			{
				// there is already a node for the exact same path
				// -> use it, if unused so far; append a new node otherwise

				if (child->startRevision == NO_REVISION)
					child->startRevision = startrev;
				else
					return new CSearchPathTree (path, startrev, this);
			}
			else
			{
				if (child->path == commonPath)
				{
					// the path is a (true) sub-node of the child

					return child->Insert (path, startrev);
				}
				else
				{
					// there is a common base path with this child
					// Note: no other child can have that.
					// ->factor out and insert new sub-child

					CSearchPathTree* commonParent 
						= new CSearchPathTree (commonPath, revision_t(NO_REVISION), this);

					child->DeLink();
					child->Link (commonParent);

					return new CSearchPathTree (path, startrev, commonParent);
				}
			}

			return this;
		}
	}

	// no overlap with any existing node
	// -> create a new child

	return new CSearchPathTree (path, startrev, this);
}

void CSearchPathTree::Remove()
{
	startRevision = revision_t (NO_REVISION);
	lastEntry = NULL;

	CSearchPathTree* node = this;
	while (node->IsEmpty() && (node->parent != NULL))
	{
		CSearchPathTree* temp = node;
		node = node->parent;

		delete temp;
	}
}

// there is a new revision entry for this path

void CSearchPathTree::ChainEntries (CRevisionEntry* entry)
{
	if (lastEntry != NULL)
	{
		// branch or chain?

		if (entry->action == CRevisionEntry::addedwithhistory)
			lastEntry->copyTargets.push_back (entry);
		else
			lastEntry->next = entry;
	}

	lastEntry = entry;
}

CRevisionGraph::CRevisionGraph(void) : m_bCancelled(FALSE)
	, m_FilterMinRev(-1)
	, m_FilterMaxRev(-1)
{
	memset (&m_ctx, 0, sizeof (m_ctx));
	parentpool = svn_pool_create(NULL);

	Err = svn_config_ensure(NULL, parentpool);
	pool = svn_pool_create (parentpool);
	// set up the configuration
	if (Err == 0)
		Err = svn_config_get_config (&(m_ctx.config), g_pConfigDir, pool);

	if (Err != 0)
	{
		::MessageBox(NULL, this->GetLastErrorMessage(), _T("TortoiseSVN"), MB_ICONERROR);
		svn_error_clear(Err);
		svn_pool_destroy (pool);
		svn_pool_destroy (parentpool);
		exit(-1);
	}

	// set up authentication
	m_prompt.Init(pool, &m_ctx);

	m_ctx.cancel_func = cancel;
	m_ctx.cancel_baton = this;

	//set up the SVN_SSH param
	CString tsvn_ssh = CRegString(_T("Software\\TortoiseSVN\\SSH"));
	if (tsvn_ssh.IsEmpty())
		tsvn_ssh = CPathUtils::GetAppDirectory() + _T("TortoisePlink.exe");
	tsvn_ssh.Replace('\\', '/');
	if (!tsvn_ssh.IsEmpty())
	{
		svn_config_t * cfg = (svn_config_t *)apr_hash_get (m_ctx.config, SVN_CONFIG_CATEGORY_CONFIG,
			APR_HASH_KEY_STRING);
		svn_config_set(cfg, SVN_CONFIG_SECTION_TUNNELS, "ssh", CUnicodeUtils::GetUTF8(tsvn_ssh));
	}
}

CRevisionGraph::~CRevisionGraph(void)
{
	svn_error_clear(Err);
	svn_pool_destroy (parentpool);

	ClearRevisionEntries();
}

void CRevisionGraph::ClearRevisionEntries()
{
	for (size_t i = 0, count = m_entryPtrs.size(); i < count; ++i)
		delete m_entryPtrs[i];

	m_entryPtrs.clear();

	for (size_t i = 0, count = copyToRelation.size(); i < count; ++i)
		delete copyToRelation[i];

	copyToRelation.clear();
	copyFromRelation.clear();
}

bool CRevisionGraph::SetFilter(svn_revnum_t minrev, svn_revnum_t maxrev, const CString& sPathFilter)
{
	m_FilterMinRev = minrev;
	m_FilterMaxRev = maxrev;
	m_filterpaths.clear();
	// the filtered paths are separated by an '*' char, since that char is illegal in paths and urls
	if (!sPathFilter.IsEmpty())
	{
		int pos = sPathFilter.Find('*');
		if (pos)
		{
			CString sTemp = sPathFilter;
			while (pos>=0)
			{
				m_filterpaths.insert((LPCWSTR)sTemp.Left(pos));
				sTemp = sTemp.Mid(pos+1);
				pos = sTemp.Find('*');
			}
			m_filterpaths.insert((LPCWSTR)sTemp);
		}
		else
			m_filterpaths.insert((LPCWSTR)sPathFilter);
	}
	return true;
}

BOOL CRevisionGraph::ProgressCallback(CString /*text*/, CString /*text2*/, DWORD /*done*/, DWORD /*total*/) {return TRUE;}

svn_error_t* CRevisionGraph::cancel(void *baton)
{
	CRevisionGraph * me = (CRevisionGraph *)baton;
	if (me->m_bCancelled)
	{
		CString temp;
		temp.LoadString(IDS_SVN_USERCANCELLED);
		return svn_error_create(SVN_ERR_CANCELLED, NULL, CUnicodeUtils::GetUTF8(temp));
	}
	return SVN_NO_ERROR;
}

// implement ILogReceiver

void CRevisionGraph::ReceiveLog ( LogChangedPathArray* changes
								, svn_revnum_t rev
								, const CString&
								, const apr_time_t&
								, const CString&)
{
    // we passed revs_only to Log()

    assert (changes == NULL);

	// update internal data

	if ((m_lHeadRevision < (revision_t)rev) || (m_lHeadRevision == NO_REVISION))
		m_lHeadRevision = rev;

	// update progress bar and check for user pressing "Cancel" somewhere

	static DWORD lastProgressCall = 0;
	if (lastProgressCall < GetTickCount() - 200)
	{
		lastProgressCall = GetTickCount();

		CString temp, temp2;
		temp.LoadString(IDS_REVGRAPH_PROGGETREVS);
		temp2.Format(IDS_REVGRAPH_PROGCURRENTREV, rev);
		if (!ProgressCallback (temp, temp2, m_lHeadRevision - rev, m_lHeadRevision))
		{
			m_bCancelled = TRUE;
			throw SVNError (cancel (this));
		}
	}
}

BOOL CRevisionGraph::FetchRevisionData(CString path)
{
	// set some text on the progress dialog, before we wait
	// for the log operation to start
	CString temp;
	temp.LoadString(IDS_REVGRAPH_PROGGETREVS);
	ProgressCallback(temp, _T(""), 0, 1);

	// prepare the path for Subversion
	SVN::preparePath(path);
	CStringA url = CUnicodeUtils::GetUTF8(path);

	svn_error_clear(Err);
	// convert a working copy path into an URL if necessary
	if (!svn_path_is_url(url))
	{
		//not an url, so get the URL from the working copy path first
		svn_wc_adm_access_t *adm_access;          
		const svn_wc_entry_t *entry;  
		const char * canontarget = svn_path_canonicalize(url, pool);
#pragma warning(push)
#pragma warning(disable: 4127)	// conditional expression is constant
			Err = svn_wc_adm_probe_open2 (&adm_access, NULL, canontarget,
				FALSE, 0, pool);
			if (Err) return FALSE;
			Err =  svn_wc_entry (&entry, canontarget, adm_access, FALSE, pool);
			if (Err) return FALSE;
			Err = svn_wc_adm_close (adm_access);
			if (Err) return FALSE;
#pragma warning(pop)

			url = entry ? entry->url : "";
	}
	url = CPathUtils::PathEscape(url);

	// we have to get the log from the repository root
	CTSVNPath urlpath;
	urlpath.SetFromSVN(url);

	SVN svn;
	m_sRepoRoot = svn.GetRepositoryRoot(urlpath);
	url = m_sRepoRoot;
	urlpath.SetFromSVN(url);

	if (m_sRepoRoot.IsEmpty())
	{
		Err = svn_error_dup(svn.Err);
		return FALSE;
	}

	m_lHeadRevision = (revision_t)NO_REVISION;
	try
	{
		CRegStdWORD useLogCache (_T("Software\\TortoiseSVN\\UseLogCache"), TRUE);
		CLogCachePool* caches = useLogCache != FALSE 
							  ? svn.GetLogCachePool() 
							  : NULL;

		svnQuery.reset (new CSVNLogQuery (&m_ctx, pool));
		query.reset (new CCacheLogQuery (caches, svnQuery.get()));

		query->Log ( CTSVNPathList (urlpath)
				   , SVNRev(SVNRev::REV_HEAD)
				   , SVNRev(SVNRev::REV_HEAD)
				   , SVNRev(0)
				   , 0
				   , false
				   , this
                   , true);
	}
	catch (SVNError& e)
	{
		Err = svn_error_create (e.GetCode(), NULL, e.GetMessage());
		return FALSE;
	}

	return TRUE;
}

BOOL CRevisionGraph::AnalyzeRevisionData(CString path, bool bShowAll /* = false */, bool /* bArrangeByPath = false */)
{
	svn_error_clear(Err);

	ClearRevisionEntries();
	m_maxurllength = 0;
	m_maxurl.Empty();
	m_maxRow = 0;
	m_maxColumn = 0;

	SVN::preparePath(path);
	CStringA url = CUnicodeUtils::GetUTF8(path);

	// convert a working copy path into an URL if necessary
	if (!svn_path_is_url(url))
	{
		//not an url, so get the URL from the working copy path first
		svn_wc_adm_access_t *adm_access;          
		const svn_wc_entry_t *entry;  
		const char * canontarget = svn_path_canonicalize(url, pool);
#pragma warning(push)
#pragma warning(disable: 4127)	// conditional expression is constant
		Err = svn_wc_adm_probe_open2 (&adm_access, NULL, canontarget,
			FALSE, 0, pool);
		if (Err) return FALSE;
		Err =  svn_wc_entry (&entry, canontarget, adm_access, FALSE, pool);
		if (Err) return FALSE;
		Err = svn_wc_adm_close (adm_access);
		if (Err) return FALSE;
#pragma warning(pop)

		url = entry ? entry->url : "";
	}

	url = CPathUtils::PathUnescape(url);
	url = url.Mid(CPathUtils::PathUnescape(m_sRepoRoot).GetLength());

	// in case our path was renamed and had a different name in the past,
	// we have to find out that name now, because we will analyze the data
	// from lower to higher revisions

	const CCachedLogInfo* cache = query->GetCache();
	const CPathDictionary* paths = &cache->GetLogInfo().GetPaths();
	CDictionaryBasedTempPath startPath (paths, (const char*)url);

	CCopyFollowingLogIterator iterator (cache, m_lHeadRevision, startPath);
	iterator.Retry();
	revision_t initialrev = m_lHeadRevision;

	while (   (iterator.GetRevision() > 0) 
		   && !iterator.EndOfPath()
		   && !iterator.DataIsMissing())
	{
		initialrev = iterator.GetRevision();
		iterator.Advance();
	}

	startPath = iterator.GetPath();

	// step 1: create "copy-to" lists based on the "copy-from" info

	BuildForwardCopies();

	// step 2: crawl the history upward, follow branches and create revision info graph

	AnalyzeRevisions (startPath, initialrev, bShowAll);

	// step 3: reduce graph by saying "renamed" instead of "deleted"+"addedWithHistory" etc.

	Optimize();

	// step 4: place the nodes on a row, column grid

	AssignCoordinates();

	// step 5: final sorting etc.

	Cleanup();

	return true;
}

inline bool AscendingFromRevision (const SCopyInfo* lhs, const SCopyInfo* rhs)
{
	return lhs->fromRevision < rhs->fromRevision;
}

inline bool AscendingToRevision (const SCopyInfo* lhs, const SCopyInfo* rhs)
{
	return lhs->toRevision < rhs->toRevision;
}

void CRevisionGraph::BuildForwardCopies()
{
	// iterate through all revisions and fill copyToRelation:
	// for every copy-from info found, add an entry

	const CCachedLogInfo* cache = query->GetCache();
	const CRevisionIndex& revisions = cache->GetRevisions();
	const CRevisionInfoContainer& revisionInfo = cache->GetLogInfo();

	// for all revisions ...

	for ( revision_t revision = revisions.GetFirstRevision()
		, last = revisions.GetLastRevision()
		; revision < last
		; ++revision)
	{
		// ... in the cache ...

		index_t index = revisions[revision];
		if (index != NO_INDEX)
		{
			// ... examine all changes ...

			for ( CRevisionInfoContainer::CChangesIterator 
					iter = revisionInfo.GetChangesBegin (index)
				, end = revisionInfo.GetChangesEnd (index)
				; iter != end
				; ++iter)
			{
				// ... and if it has a copy-from info ...

				if (iter->HasFromPath())
				{
					// ... add it to the list

					SCopyInfo* copyInfo = new SCopyInfo;
					copyInfo->fromRevision = iter->GetFromRevision();
					copyInfo->fromPathIndex = iter->GetFromPathID();
					copyInfo->toRevision = revision;
					copyInfo->toPathIndex = iter->GetPathID();

					copyToRelation.push_back (copyInfo);
					copyFromRelation.push_back (copyInfo);
				}
			}
		}
	}

	// sort container by source revision and path

	std::sort (copyToRelation.begin(), copyToRelation.end(), &AscendingToRevision);
	std::sort (copyFromRelation.begin(), copyFromRelation.end(), &AscendingFromRevision);
}

void CRevisionGraph::AnalyzeRevisions ( const CDictionaryBasedTempPath& path
									  , revision_t startrev
									  , bool bShowAll)
{
	const CCachedLogInfo* cache = query->GetCache();
	const CRevisionIndex& revisions = cache->GetRevisions();
	const CRevisionInfoContainer& revisionInfo = cache->GetLogInfo();

	// initialize the paths we have to search for

	std::auto_ptr<CSearchPathTree> searchTree 
		(new CSearchPathTree (&revisionInfo.GetPaths()));
	searchTree->Insert (path, startrev);

	// the range of copy-to info that applies to the current revision

	TSCopyIterator lastFromCopy = copyFromRelation.begin();
	TSCopyIterator lastToCopy = copyToRelation.begin();

	// collect nodes to draw ... revision by revision

	for (revision_t revision = startrev; revision <= m_lHeadRevision; ++revision)
	{
		index_t index = revisions[revision];
		if (index == NO_INDEX)
			continue;

		// handle remaining copy-to entries
		// (some may have a fromRevision that does not touch the fromPath)

		AddCopiedPaths ( revision
					   , searchTree.get()
					   , lastToCopy);

		// we are looking for search paths that (may) overlap 
		// with the revisions' changes

		CDictionaryBasedPath basePath = revisionInfo.GetRootPath (index);
		if (!basePath.IsValid())
			continue;	// empty revision

		// collect search paths that have been deleted in this container
		// (delay potential node deletion until we finished tree traversal)

		std::vector<CSearchPathTree*> toRemove;

		// pre-order search-tree traversal

		CSearchPathTree* searchNode = searchTree.get();
		while (searchNode != NULL)
		{
			if (basePath.IsSameOrParentOf (searchNode->GetPath().GetBasePath()))
			{
				// maybe a hit -> match all changes against the whole sub-tree

				AnalyzeRevisions ( revision
								 , revisionInfo.GetChangesBegin (index)
								 , revisionInfo.GetChangesEnd (index)
								 , searchNode
								 , bShowAll
								 , toRemove);
			}
			else
			{
				bool subTreeTouched 
					= searchNode->GetPath().IsSameOrParentOf (basePath);

				// show intermediate nodes as well?

				if (bShowAll && subTreeTouched && searchNode->IsActive())
				{
					AnalyzeRevisions ( revision
									 , revisionInfo.GetChangesBegin (index)
									 , revisionInfo.GetChangesEnd (index)
									 , searchNode
									 , bShowAll
									 , toRemove);
				}

				if ((searchNode->GetFirstChild() != NULL) && subTreeTouched)
				{
					// the sub-nodes may be a match

					searchNode = searchNode->GetFirstChild();
					continue;
				}
			}

			// this sub-tree has fully been covered (or been no match at all)
			// -> to the next node

			while (   (searchNode->GetNext() == NULL)
				   && (searchNode->GetParent() != NULL))
				searchNode = searchNode->GetParent();

			searchNode = searchNode->GetNext();
		}

		// handle remaining copy-to entries
		// (some may have a fromRevision that does not touch the fromPath)

		FillCopyTargets ( revision
						, searchTree.get()
						, lastFromCopy);

		// remove deleted search paths

		for (size_t i = 0, count = toRemove.size(); i < count; ++i)
			toRemove[i]->Remove();
	}
}

void CRevisionGraph::AnalyzeRevisions ( revision_t revision
									  , CRevisionInfoContainer::CChangesIterator first
									  , CRevisionInfoContainer::CChangesIterator last
									  , CSearchPathTree* startNode
									  , bool bShowAll
									  , std::vector<CSearchPathTree*>& toRemove)
{
	// cover the whole sub-tree

	CSearchPathTree* searchNode = startNode;
	do
	{
		// is this search path active?

		if (searchNode->IsActive())
		{
			const CDictionaryBasedTempPath& path = searchNode->GetPath();

			// looking for the closet change that affected the path

			for ( CRevisionInfoContainer::CChangesIterator iter = first
				; iter != last
				; ++iter)
			{
				CDictionaryBasedPath changePath = iter->GetPath();
				if (   (  bShowAll 
					   && path.IsSameOrParentOf (changePath))
					|| (  (iter->GetAction() != CRevisionInfoContainer::ACTION_CHANGED)
					   && changePath.IsSameOrParentOf (path.GetBasePath())))
				{
					// construct the action member

					int actionValue = iter->GetAction();
					if (iter->HasFromPath())
						++actionValue;

					CRevisionEntry::Action action 
						= static_cast<CRevisionEntry::Action>(actionValue);

					// show modifications within the sub-tree as "modified"
					// (otherwise, deletions would terminate the path)

					if (bShowAll && (path.GetBasePath().GetIndex() < changePath.GetIndex()))
						action = CRevisionEntry::modified;

					// create & init the new graph node

					CRevisionEntry* newEntry 
						= new CRevisionEntry (path, revision, action);
					newEntry->index = m_entryPtrs.size();
					newEntry->realPath = changePath;
					m_entryPtrs.push_back (newEntry);

					// link entries for the same search path

					if (newEntry)
						searchNode->ChainEntries (newEntry);

					// end of path?

					if (action == CRevisionEntry::deleted)
						toRemove.push_back (searchNode);

					// we will create at most one node per path and revision

					break;
				}
			}
		}

		// select next node

		if (searchNode->GetFirstChild() != NULL)
		{
			searchNode = searchNode->GetFirstChild();
		}
		else
		{
			while (    (searchNode->GetNext() == NULL)
					&& (searchNode != startNode))
				searchNode = searchNode->GetParent();

			if (searchNode != startNode)
				searchNode = searchNode->GetNext();
		}
	}
	while (searchNode != startNode);
}

void CRevisionGraph::AddCopiedPaths ( revision_t revision
								    , CSearchPathTree* rootNode
								    , TSCopyIterator& lastToCopy)
{
	TSCopyIterator endToCopy = copyToRelation.end();

	// find first entry for this revision (or first beyond)

	TSCopyIterator firstToCopy = lastToCopy;
	while (   (firstToCopy != endToCopy) 
		   && ((*firstToCopy)->toRevision < revision))
		++firstToCopy;

	// find first beyond this revision

	lastToCopy = firstToCopy;
	while (   (lastToCopy != endToCopy) 
		   && ((*lastToCopy)->toRevision <= revision))
		++lastToCopy;

	// create search paths for all *relevant* paths added in this revision

	for (TSCopyIterator iter = firstToCopy; iter != lastToCopy; ++iter)
	{
		const std::vector<SCopyInfo::STarget>& targets = (*iter)->targets;
		for (size_t i = 0, count = targets.size(); i < count; ++i)
		{
			const SCopyInfo::STarget& target = targets[i];
			CSearchPathTree* node = rootNode->Insert (target.path, revision);
			node->ChainEntries (target.source);
		}
	}
}

void CRevisionGraph::FillCopyTargets ( revision_t revision
								     , CSearchPathTree* rootNode
								     , TSCopyIterator& lastFromCopy)
{
	TSCopyIterator endFromCopy = copyFromRelation.end();

	// find first entry for this revision (or first beyond)

	TSCopyIterator firstFromCopy = lastFromCopy;
	while (   (firstFromCopy != endFromCopy) 
		   && ((*firstFromCopy)->fromRevision < revision))
		++firstFromCopy;

	// find first beyond this revision

	lastFromCopy = firstFromCopy;
	while (   (lastFromCopy != endFromCopy) 
		   && ((*lastFromCopy)->fromRevision <= revision))
		++lastFromCopy;

	// create search paths for all *relevant* paths added in this revision

	for (TSCopyIterator iter = firstFromCopy; iter != lastFromCopy; ++iter)
	{
		SCopyInfo* copy = *iter;
		std::vector<SCopyInfo::STarget>& targets = copy->targets;

		// crawl the whole sub-tree for path matches

		CSearchPathTree* searchNode = rootNode;
		while (searchNode != NULL)
		{
			const CDictionaryBasedTempPath& path = searchNode->GetPath();

			// got this path copied?

            bool sameOrChild = path.IsSameOrChildOf (copy->fromPathIndex);
			if (searchNode->IsActive() && sameOrChild)
			{
				CRevisionEntry*	entry = searchNode->GetLastEntry();
				if ((entry == NULL) || (entry->revision != revision))
				{
					// the copy source graph node has yet to be created

					entry = new CRevisionEntry ( path
											   , revision
											   , CRevisionEntry::source);
					entry->realPath 
						= CDictionaryBasedPath ( path.GetDictionary()
											   , copy->fromPathIndex);

					entry->index = m_entryPtrs.size();
					m_entryPtrs.push_back (entry);

					searchNode->ChainEntries (entry);
				}

				// add & schedule the new search path

				SCopyInfo::STarget target 
					( entry
					, path.ReplaceParent ( CDictionaryBasedPath ( path.GetDictionary()
															    , copy->fromPathIndex)
									     , CDictionaryBasedPath ( path.GetDictionary()
															    , copy->toPathIndex)));

				targets.push_back (target);
			}

			// select next node

			if (   (searchNode->GetFirstChild() != NULL)
                && (sameOrChild || path.IsSameOrParentOf (copy->fromPathIndex)))
			{
				searchNode = searchNode->GetFirstChild();
			}
			else
			{
				while (    (searchNode->GetNext() == NULL)
						&& (searchNode->GetParent() != NULL))
					searchNode = searchNode->GetParent();

				searchNode = searchNode->GetNext();
			}
		}
	}
}

void CRevisionGraph::AssignColumns ( CRevisionEntry* start
								   , std::vector<int>& columnByRow
                                   , int column)
{
	// find larges level for the chain starting at "start"

	int lastRow = 0;
	for (CRevisionEntry* entry = start; entry != NULL; entry = entry->next)
		lastRow = entry->row;

	for (int row = start->row; row <= lastRow; ++row)
		column = max (column, columnByRow[row]+1);

	// assign that level & collect branches

	std::vector<CRevisionEntry*> branches;
	for (CRevisionEntry* entry = start; entry != NULL; entry = entry->next)
	{
		entry->column = column;
		if (!entry->copyTargets.empty())
			branches.push_back (entry);
	}

	// block the level for the whole chain

	for (int row = start->row; row <= lastRow; ++row)
		columnByRow[row] = column;

	// follow the branches

	for ( std::vector<CRevisionEntry*>::reverse_iterator iter = branches.rbegin()
		, end = branches.rend()
		; iter != end
		; ++iter)
	{
		const std::vector<CRevisionEntry*>& targets = (*iter)->copyTargets;
		for (size_t i = 0, count = targets.size(); i < count; ++i)
			AssignColumns (targets[i], columnByRow, column+1);
	}
}

void CRevisionGraph::Optimize()
{
	// say "renamed" for "Deleted"/"Added" entries

	for (size_t i = 0, count = m_entryPtrs.size(); i < count; ++i)
	{
		CRevisionEntry * entry = m_entryPtrs[i];
		CRevisionEntry * next = entry->next;

		if ((next != NULL) && (next->action == CRevisionEntry::deleted))
		{
			// this line will be deleted. 
			// will it be continued under a different name?

			if (entry->copyTargets.size() == 1)
			{
				CRevisionEntry * target = entry->copyTargets[0];
				assert (target->action == CRevisionEntry::addedwithhistory);

				if (target->revision == next->revision)
				{
					// that's actually a rename

					target->action = CRevisionEntry::renamed;

					// make it part of this line (not a branch)

					entry->next = target;
					entry->copyTargets.clear();

					// mark the old "deleted" entry for removal

					next->action = CRevisionEntry::nothing;
				}
			}
		}
	}

	// compract

	std::vector<CRevisionEntry*>::iterator target = m_entryPtrs.begin();
	for ( std::vector<CRevisionEntry*>::iterator source = target
		, end = m_entryPtrs.end()
		; source != end
		; ++source)
	{
		if ((*source)->action == CRevisionEntry::nothing)
		{
			delete *source;
		}
		else
		{
			*target = *source;
			++target;
		}
	}

	m_entryPtrs.erase (target, m_entryPtrs.end());
}

void CRevisionGraph::AssignCoordinates()
{
	// assign rows

	int row = 0;
	revision_t lastRevision = 0;
	for (size_t i = 0, count = m_entryPtrs.size(); i < count; ++i)
	{
		CRevisionEntry * entry = m_entryPtrs[i];
		if (entry->revision > lastRevision)
		{
			lastRevision = entry->revision;
			++row;
		}
		
		entry->row = row;
	}

	// the highest used column per revision

	std::vector<int> columnByRow;
	columnByRow.insert (columnByRow.begin(), row+1, 0);

	AssignColumns (m_entryPtrs[0], columnByRow, 1);
}

inline bool AscendingColumRow ( const CRevisionEntry* lhs
							  , const CRevisionEntry* rhs)
{
	return (lhs->column < rhs->column)
		|| (   (lhs->column == rhs->column)
		    && (lhs->row < rhs->row));
}

void CRevisionGraph::Cleanup()
{
	// add "next" to "targets"

	for (size_t i = 0, count = m_entryPtrs.size(); i < count; ++i)
	{
		// add the parent line to the target list

		CRevisionEntry * entry = m_entryPtrs[i];
		std::vector<CRevisionEntry*>& targets = entry->copyTargets;
		if (entry->next != NULL)
			targets.push_back (entry->next);

		// sort targets by level and revision

		sort (targets.begin(), targets.end(), &AscendingColumRow);
	}
}

bool CRevisionGraph::IsParentOrItself(const char * parent, const char * child)
{
	size_t len = strlen(parent);
	if (strncmp(parent, child, len)==0)
	{
		if ((child[len]=='/')||(child[len]==0))
			return true;
	}
	return false;
}

bool CRevisionGraph::IsParentOrItself(const wchar_t * parent, const wchar_t * child)
{
	size_t len = wcslen(parent);
	if (wcsncmp(parent, child, len)==0)
	{
		if ((child[len]=='/')||(child[len]==0))
			return true;
	}
	return false;
}

CString CRevisionGraph::GetLastErrorMessage()
{
	return SVN::GetErrorString(Err);
}
