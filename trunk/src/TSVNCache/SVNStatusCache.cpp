// TortoiseSVN - a Windows shell extension for easy version control

// External Cache Copyright (C) 2005 - Will Dean

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//

#include "StdAfx.h"
#include "SVNStatus.h"
#include "Svnstatuscache.h"
#include "CacheInterface.h"

//////////////////////////////////////////////////////////////////////////

CSVNStatusCache* CSVNStatusCache::m_pInstance;

CSVNStatusCache& CSVNStatusCache::Instance()
{
	ATLASSERT(m_pInstance != NULL);
	return *m_pInstance;
}

void CSVNStatusCache::Create()
{
	ATLASSERT(m_pInstance == NULL);
	m_pInstance = new CSVNStatusCache;
}
void CSVNStatusCache::Destroy()
{
	delete m_pInstance;
	m_pInstance = NULL;
}


CSVNStatusCache::CSVNStatusCache(void)
{
	m_folderCrawler.Initialise();
}

CSVNStatusCache::~CSVNStatusCache(void)
{
}

void CSVNStatusCache::Clear()
{

}

CCachedDirectory& CSVNStatusCache::GetDirectoryCacheEntry(const CTSVNPath& path)
{
	ATLASSERT(path.IsDirectory() || !PathFileExists(path.GetWinPath()));

	AutoLocker lock(m_critSec);

	CCachedDirectory::ItDir itMap;
	itMap = m_directoryCache.find(path);
	if(itMap != m_directoryCache.end())
	{
		// We've found this directory in the cache 
		return itMap->second;
	}
	else
	{
		// We don't know anything about this directory yet - lets add it to our cache
		return m_directoryCache.insert(m_directoryCache.lower_bound(path), std::make_pair(path, CCachedDirectory(path)))->second;
	}
}


CStatusCacheEntry CSVNStatusCache::GetStatusForPath(const CTSVNPath& path, DWORD flags)
{
	AutoLocker lock(m_critSec);

	bool bRecursive = !!(flags & TSVNCACHE_FLAGS_RECUSIVE_STATUS);


	// Check a very short-lived 'mini-cache' of the last thing we were asked for.
	long now = (long)GetTickCount();
	if(now-m_mostRecentExpiresAt < 0)
	{
		if(path.IsEquivalentTo(m_mostRecentPath))
		{
			return m_mostRecentStatus;
		}
	}
	m_mostRecentPath = path;
	m_mostRecentExpiresAt = now+1000;

	ATLTRACE("Req: %ws\n", path.GetWinPathString());

	// Stop the crawler starting on a new folder while we're doing this much more important task...
	CCrawlInhibitor crawlInhibit(&m_folderCrawler);

	if(path.IsEquivalentTo(CTSVNPath(_T("N:\\will\\svn\\TortoiseSVN\\src"))))
	{
		ATLTRACE("");
	}

	return m_mostRecentStatus = GetDirectoryCacheEntry(path.GetContainingDirectory()).GetStatusForMember(path, bRecursive);
}

void CSVNStatusCache::AddFolderForCrawling(const CTSVNPath& path)
{
	m_folderCrawler.AddDirectoryForUpdate(path);
}

//////////////////////////////////////////////////////////////////////////

static class StatusCacheTests
{
public:
	StatusCacheTests()
	{
		apr_initialize();
		CSVNStatusCache::Create();

		{
//			CSVNStatusCache& cache = CSVNStatusCache::Instance();

//		cache.GetStatusForPath(CTSVNPath(_T("n:/will/tsvntest/file1.txt")));
//		cache.GetStatusForPath(CTSVNPath(_T("n:/will/tsvntest/NonVersion/Test1.txt")));

		}

		CSVNStatusCache::Destroy();
		apr_terminate();
	}

} StatusCacheTests;


