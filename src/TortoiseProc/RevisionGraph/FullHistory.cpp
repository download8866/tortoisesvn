// TortoiseSVN - a Windows shell extension for easy version control

// Copyright (C) 2003-2015 - TortoiseSVN

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
#include "stdafx.h"
#include "FullHistory.h"

#include "resource.h"
#include "Client.h"
#include "registry.h"
#include "UnicodeUtils.h"
#include "PathUtils.h"
#include "SVN.h"
#include "TSVNPath.h"
#include "SVNConfig.h"
#include "SVNError.h"
#include "SVNInfo.h"
#include "CachedLogInfo.h"
#include "RepositoryInfo.h"
#include "RevisionIndex.h"
#include "Access/CopyFollowingLogIterator.h"
#include "ProgressDlg.h"
#include "AsyncCall.h"
#include "Future.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

using namespace async;

CFullHistory::CFullHistory(void)
    : cancelled (false)
    , progress (NULL)
    , taskbarlist (NULL)
    , hwnd (NULL)
    , headRevision ((revision_t)NO_REVISION)
    , pegRevision ((revision_t)NO_REVISION)
    , firstRevision ((revision_t)NO_REVISION)
    , copyInfoPool (sizeof (SCopyInfo), 1024)
    , copyToRelation (NULL)
    , copyToRelationEnd (NULL)
    , copyFromRelation (NULL)
    , copyFromRelationEnd (NULL)
    , cache (NULL)
    , Err (NULL)
    , startRevision(0)
    , diskIOScheduler (2, 0, true)  // two threads for crawling the disk
                                    // (they will both query info from the same place,
                                    // i.e. read different portions of the same WC status)
    , cpuLoadScheduler (1, INT_MAX, true) // at least one thread for CPU intense ops
                                    // plus as much as we got left from the shared pool
{
    parentpool = svn_pool_create(NULL);
    pool = svn_pool_create (parentpool);
    svn_error_clear(svn_client_create_context2(&ctx, SVNConfig::Instance().GetConfig(pool), parentpool));

    // set up authentication
    prompt.Init(pool, ctx);

    ctx->cancel_func = cancel;
    ctx->cancel_baton = this;
}

CFullHistory::~CFullHistory(void)
{
    ClearCopyInfo();

    svn_error_clear(Err);
    svn_pool_destroy (parentpool);
}

bool CFullHistory::ClearCopyInfo()
{
    delete copyToRelation;
    delete copyFromRelation;

    copyToRelation = NULL;
    copyToRelationEnd = NULL;
    copyFromRelation = NULL;
    copyFromRelationEnd = NULL;

    for (size_t i = 0, count = copiesContainer.size(); i < count; ++i)
        copiesContainer[i]->Destroy (copyInfoPool);

    copiesContainer.clear();

    return true;
}

svn_error_t* CFullHistory::cancel(void *baton)
{
    CFullHistory * self = (CFullHistory *)baton;
    if (self->cancelled)
    {
        CString temp;
        temp.LoadString(IDS_SVN_USERCANCELLED);
        return svn_error_create(SVN_ERR_CANCELLED, NULL, CUnicodeUtils::GetUTF8(temp));
    }
    return SVN_NO_ERROR;
}

// implement ILogReceiver

void CFullHistory::ReceiveLog ( TChangedPaths* changes
                              , svn_revnum_t rev
                              , const StandardRevProps* stdRevProps
                              , UserRevPropArray* userRevProps
                              , const MergeInfo* mergeInfo)
{
    // fix release mode compiler warning

    UNREFERENCED_PARAMETER(changes);
    UNREFERENCED_PARAMETER(stdRevProps);
    UNREFERENCED_PARAMETER(userRevProps);
    UNREFERENCED_PARAMETER(mergeInfo);

    // update internal data

    if ((headRevision < (revision_t)rev) || (headRevision == NO_REVISION))
        headRevision = rev;

    // update progress bar and check for user pressing "Cancel" somewhere

    static ULONGLONG lastProgressCall = 0;
    if (lastProgressCall < GetTickCount64() - 200UL)
    {
        lastProgressCall = GetTickCount64();

        if (progress)
        {
            CString text, text2;
            text.LoadString(IDS_REVGRAPH_PROGGETREVS);
            text2.Format(IDS_REVGRAPH_PROGCURRENTREV, rev);

            DWORD revisionCount = headRevision - firstRevision+1;
            progress->SetLine(1, text);
            progress->SetLine(2, text2);
            progress->SetProgress (headRevision - rev, revisionCount);
            if (taskbarlist)
            {
                taskbarlist->SetProgressState(hwnd, TBPF_NORMAL);
                taskbarlist->SetProgressValue(hwnd, headRevision - rev, revisionCount);
            }
            if (!progress->IsVisible())
                progress->ShowModeless (hwnd);

            if (progress->HasUserCancelled())
            {
                cancelled = true;
                throw SVNError (cancel (this));
            }
        }
    }
}

bool CFullHistory::FetchRevisionData ( CString path
                                     , SVNRev pegRev
                                     , bool showWCRev
                                     , bool showWCModification
                                     , CProgressDlg* progressDlg
                                     , ITaskbarList3 * pTaskBarList
                                     , HWND hWnd)
{
    // clear any previously existing SVN error info

    svn_error_clear(Err);
    Err = NULL;

    // remove internal data from previous runs

    CFuture<bool> clearJob (this, &CFullHistory::ClearCopyInfo, &cpuLoadScheduler);

    // set some text on the progress dialog, before we wait
    // for the log operation to start
    this->progress = progressDlg;
    this->taskbarlist = pTaskBarList;
    this->hwnd = hWnd;

    CString temp;
    temp.LoadString (IDS_REVGRAPH_PROGGETREVS);
    progress->SetLine(1, temp);

    temp.LoadString (IDS_REVGRAPH_PROGPREPARING);
    progress->SetLine(2, temp);
    progress->SetProgress(0, 1);
    progress->ShowModeless (hWnd);
    if (taskbarlist)
    {
        taskbarlist->SetProgressState(hwnd, TBPF_INDETERMINATE);
    }

    // prepare the path for Subversion
    CTSVNPath svnPath (path);
    CStringA url = CPathUtils::PathEscape
                        (CUnicodeUtils::GetUTF8
                            (svn.GetURLFromPath (svnPath)));

    // we have to get the log from the repository root

    CTSVNPath rootPath;
    svn_revnum_t head;
    if (FALSE == svn.GetRootAndHead (svnPath, rootPath, head))
    {
        Err = svn_error_dup(const_cast<svn_error_t*>(svn.GetSVNError()));
        return false;
    }

    if (pegRev.IsHead())
        pegRev = head;

    headRevision = head;
    CString escapedRepoRoot = rootPath.GetSVNPathString();
    relPath = CPathUtils::PathUnescape (url.Mid (escapedRepoRoot.GetLength()));
    repoRoot = CPathUtils::PathUnescape (escapedRepoRoot);

    // fix issue #360: use WC revision as peg revision

    pegRevision = pegRev;
    if (pegRevision == NO_REVISION)
    {
        if (!svnPath.IsUrl())
        {
            SVNInfo info;
            const SVNInfoData * baseInfo
                = info.GetFirstFileInfo (svnPath, SVNRev(), SVNRev());
            if (baseInfo != NULL)
                pegRevision = baseInfo->rev;
        }
    }

    // fetch missing data from the repository
    try
    {
        // select / construct query object and optimize revision range to fetch

        svnQuery.reset (new CSVNLogQuery (ctx, pool));

        bool cacheIsComplete = false;
        if (svn.GetLogCachePool()->IsEnabled())
        {
            CLogCachePool* cachepool = svn.GetLogCachePool();
            query.reset (new CCacheLogQuery (cachepool, svnQuery.get()));

            // get the cache and the lowest missing revision
            // (in off-line mode, the query may not find the cache as
            // it cannot contact the server to get the UUID)

            uuid = cachepool->GetRepositoryInfo().GetRepositoryUUID (rootPath);
            cache = cachepool->GetCache (uuid, escapedRepoRoot);

            firstRevision = cache != NULL
                          ? cache->GetRevisions().GetFirstMissingRevision(1)
                          : 0;

            // if the cache is already complete, the firstRevision here is
            // HEAD+1 - that revision does not exist and would throw an error later

            if (firstRevision > headRevision)
            {
                cacheIsComplete = true;
                firstRevision = headRevision;
            }
        }
        else
        {
            query.reset (new CCacheLogQuery (svn, svnQuery.get()));
            cache = NULL;
            firstRevision = 0;
        }

        // Find the revision the working copy is on, we mark that revision
        // later in the graph (handle option changes properly!).
        // For performance reasons, we only don't do it if we want to display it.

        wcInfo = SWCInfo (pegRev);
        if (showWCRev || showWCModification)
        {
            new CAsyncCall ( this
                           , &CFullHistory::QueryWCRevision
                           , true
                           , path
                           , &diskIOScheduler);

            new CAsyncCall ( this
                           , &CFullHistory::QueryWCRevision
                           , false
                           , path
                           , &diskIOScheduler);
        }

        // actually fetch the data

        if (!cacheIsComplete)
            query->Log ( CTSVNPathList (rootPath)
                       , headRevision
                       , headRevision
                       , firstRevision
                       , 0
                       , false      // strictNodeHistory
                       , this
                       , false      // includeChanges (log cache fetches them automatically)
                       , false      // includeMerges
                       , true       // includeStandardRevProps
                       , false      // includeUserRevProps
                       , TRevPropNames());

        // Store updated cache data

        if (cache == NULL)
        {
            cache = query->GetCache();

            // This should never happen:

            if (cache == NULL)
                return false;
        }
        else
        {
            if (cache->IsModified())
                new CAsyncCall ( cache
                               , &LogCache::CCachedLogInfo::Save
                               , &cpuLoadScheduler);
        }

        // store WC path

        const CPathDictionary* paths = &cache->GetLogInfo().GetPaths();
        wcPath.reset (new CDictionaryBasedTempPath (paths, (const char*)relPath));

        // wait for the cleanup jobs to finish before starting new ones
        // that depend of them

        clearJob.GetResult();

        // analyse the data

        new CAsyncCall ( this
                       , &CFullHistory::AnalyzeRevisionData
                       , &cpuLoadScheduler);

        // pre-process log data (invert copy-relationship)

        new CAsyncCall ( this
                       , &CFullHistory::BuildForwardCopies
                       , &cpuLoadScheduler);

        // Wait for the jobs to finish

        if (showWCRev || showWCModification)
        {
            temp.LoadString (IDS_REVGRAPH_PROGREADINGWC);
            progress->SetLine(2, temp);
        }

        cpuLoadScheduler.WaitForEmptyQueue();
        diskIOScheduler.WaitForEmptyQueue();
    }
    catch (SVNError& e)
    {
        Err = svn_error_create (e.GetCode(), NULL, e.GetMessage());
        return false;
    }

    return true;
}

void CFullHistory::QueryWCRevision (bool commitRevs, CString path)
{
    // we need a thread-local SVN instance that checks our shared
    // progress dialog for cancelation.

    class SVNForwardedCancel : public SVN
    {
    private:

        CProgressDlg *progress;

    public:

        SVNForwardedCancel (CProgressDlg *progress)
            : progress (progress)
        {
        }

        virtual BOOL Cancel()
        {
            return progress && progress->HasUserCancelled() ? TRUE : FALSE;
        }
    };

    // Find the revision the working copy is on, we mark that revision
    // later in the graph (handle option changes properly!).

    CTSVNPath tpath = CTSVNPath (path);

    if (!tpath.IsUrl())
    {
        svn_revnum_t maxrev = pegRevision;
        svn_revnum_t minrev = 0;

        bool switched, modified, sparse;

        SVNForwardedCancel svntmp (progress);
        if (svntmp.GetWCRevisionStatus(CTSVNPath(path)
                                       , commitRevs    // get the "commit" revision
                                       , minrev
                                       , maxrev
                                       , switched
                                       , modified
                                       , sparse))
        {
            // we want to report the oldest revision as WC revision:
            // If you commit at $WC/path/ and after that ask for the
            // rev graph at $WC/, we want to display the revision of
            // the base path ($WC/ is now "older") instead of the
            // newest one.

            if (commitRevs)
            {
                wcInfo.minCommit = minrev;
                wcInfo.maxCommit = maxrev;
                wcInfo.modified = modified;
            }
            else
            {
                wcInfo.minAtRev = minrev;
                wcInfo.maxAtRev = maxrev;
            }
        }
    }
}

void CFullHistory::AnalyzeRevisionData()
{
    // special case: empty log

    if (headRevision == NO_REVISION)
        return;

    // we don't have a peg revision yet, set it to HEAD

    if (pegRevision == NO_REVISION)
        pegRevision = headRevision;

    // in case our path was renamed and had a different name in the past,
    // we have to find out that name now, because we will analyze the data
    // from lower to higher revisions

    startPath.reset (new CDictionaryBasedTempPath (*wcPath));

    CCopyFollowingLogIterator iterator (cache, pegRevision, *startPath);
    iterator.Retry();
    startRevision = pegRevision;

    while ((iterator.GetRevision() > 0) && !iterator.EndOfPath())
    {
        if (iterator.DataIsMissing())
        {
            iterator.ToNextAvailableData();
        }
        else
        {
            startRevision = iterator.GetRevision();
            iterator.Advance();
        }
    }

    *startPath = iterator.GetAddPath();
}

inline bool AscendingFromRevision (const SCopyInfo* lhs, const SCopyInfo* rhs)
{
    return lhs->fromRevision < rhs->fromRevision;
}

inline bool AscendingToRevision (const SCopyInfo* lhs, const SCopyInfo* rhs)
{
    return lhs->toRevision < rhs->toRevision;
}

void CFullHistory::BuildForwardCopies()
{
    // iterate through all revisions and fill copyToRelation:
    // for every copy-from info found, add an entry

    const CRevisionIndex& revisions = cache->GetRevisions();
    const CRevisionInfoContainer& revisionInfo = cache->GetLogInfo();

    // for all revisions ...

    copiesContainer.reserve (revisions.GetLastRevision());
    for ( revision_t revision = revisions.GetFirstRevision()
        , last = revisions.GetLastRevision()
        ; revision < last
        ; ++revision)
    {
        // ... in the cache ...

        index_t index = revisions[revision];
        if (   (index != NO_INDEX)
            && (revisionInfo.GetSumChanges (index) & CRevisionInfoContainer::HAS_COPY_FROM))
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

                    SCopyInfo* copyInfo = SCopyInfo::Create (copyInfoPool);

                    copyInfo->fromRevision = iter->GetFromRevision();
                    copyInfo->fromPathIndex = iter->GetFromPathID();
                    copyInfo->toRevision = revision;
                    copyInfo->toPathIndex = iter->GetPathID();

                    copiesContainer.push_back (copyInfo);
                }
            }
        }
    }

    // sort container by source revision and path

    copyToRelation = new SCopyInfo*[copiesContainer.size()];
    copyToRelationEnd = copyToRelation + copiesContainer.size();

    copyFromRelation = new SCopyInfo*[copiesContainer.size()];
    copyFromRelationEnd = copyFromRelation + copiesContainer.size();

    // in early phases, there will be no copies
    // -> front() iterator is invalid

    if (!copiesContainer.empty())
    {
        size_t bytesToCopy = copiesContainer.size() * sizeof (SCopyInfo*[1]);

        memcpy (copyToRelation, &copiesContainer.front(), bytesToCopy);
        memcpy (copyFromRelation, &copiesContainer.front(), bytesToCopy);

        std::sort (copyToRelation, copyToRelationEnd, &AscendingToRevision);
        std::sort (copyFromRelation, copyFromRelationEnd, &AscendingFromRevision);
    }
}

CString CFullHistory::GetLastErrorMessage() const
{
    return SVN::GetErrorString(Err);
}

void CFullHistory::GetCopyFromRange ( SCopyInfo**& first
                                    , SCopyInfo**& last
                                    , revision_t revision) const
{
    // find first entry for this revision (or first beyond)

    while (   (first != copyFromRelationEnd)
           && ((*first)->fromRevision < revision))
        ++first;

    // find first beyond this revision

    last = first;
    while (   (last != copyFromRelationEnd)
           && ((*last)->fromRevision <= revision))
        ++last;
}

void CFullHistory::GetCopyToRange ( SCopyInfo**& first
                                  , SCopyInfo**& last
                                  , revision_t revision) const
{
    // find first entry for this revision (or first beyond)

    while (   (first != copyToRelationEnd)
           && ((*first)->toRevision < revision))
        ++first;

    // find first beyond this revision

    last = first;
    while (   (last != copyToRelationEnd)
           && ((*last)->toRevision <= revision))
        ++last;
}
