// TortoiseSVN - a Windows shell extension for easy version control

// Copyright (C) 2003-2011 - TortoiseSVN

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
#pragma once

#include "resource.h"
#include "svn.h"
#include "ProjectProperties.h"
#include "StandAloneDlg.h"
#include "TSVNPath.h"
#include "registry.h"
#include "SplitterControl.h"
#include "Colors.h"
#include "LogDlgHelper.h"
#include "FilterEdit.h"
#include "LogDlgFilter.h"
#include "SVNRev.h"
#include "Tooltip.h"
#include "SubTooltipListCtrl.h"
#include "HintCtrl.h"
#include "JobScheduler.h"
#include "ListViewAccServer.h"
#include "Win7.h"

using namespace std;


#define MERGE_REVSELECTSTART     1
#define MERGE_REVSELECTEND       2
#define MERGE_REVSELECTSTARTEND  3      ///< both
#define MERGE_REVSELECTMINUSONE  4      ///< first with N-1

#define LOGFILTER_ALL           0xFFFF
#define LOGFILTER_MESSAGES      0x0001
#define LOGFILTER_PATHS         0x0002
#define LOGFILTER_AUTHORS       0x0004
#define LOGFILTER_REVS          0x0008
#define LOGFILTER_REGEX         0x0010
#define LOGFILTER_BUGID         0x0020
#define LOGFILTER_CASE          0x0040
#define LOGFILTER_DATE          0x0080
#define LOGFILTER_DATERANGE     0x0100

#define LOGFILTER_TIMER     101

typedef int (__cdecl *GENERICCOMPAREFN)(const void * elem1, const void * elem2);

/**
 * \ingroup TortoiseProc
 * Shows log messages of a single file or folder in a listbox.
 */
class CLogDlg : public CResizableStandAloneDialog, public SVN, IFilterEditValidator, IListCtrlTooltipProvider, ListViewAccProvider
{
    DECLARE_DYNAMIC(CLogDlg)

    friend class CStoreSelection;

public:
    CLogDlg(CWnd* pParent = NULL);   // standard constructor
    virtual ~CLogDlg();

    void SetParams(const CTSVNPath& path, SVNRev pegrev, SVNRev startrev, SVNRev endrev,
        BOOL bStrict = CRegDWORD(_T("Software\\TortoiseSVN\\LastLogStrict"), FALSE), BOOL bSaveStrict = TRUE,
        int limit = (int)(DWORD)CRegDWORD(_T("Software\\TortoiseSVN\\NumberOfLogs"), 100));
    void SetFilter(const CString& findstr, LONG findtype, bool findregex);
    void SetIncludeMerge(bool bInclude = true) {m_bIncludeMerges = bInclude;}
    void SetProjectPropertiesPath(const CTSVNPath& path) {m_ProjectProperties.ReadProps(path);}
    bool IsThreadRunning() {return !netScheduler.WaitForEmptyQueueOrTimeout(0);}
    void SetDialogTitle(const CString& sTitle) {m_sTitle = sTitle;}
    void SetSelect(bool bSelect) {m_bSelect = bSelect;}
    void ContinuousSelection(bool bCont = true) {m_bSelectionMustBeContinuous = bCont;}
    void SetMergePath(const CTSVNPath& mergepath) {m_mergePath = mergepath;}

    const SVNRevRangeArray& GetSelectedRevRanges() {return m_selectedRevs;}
    void SetSelectedRevRanges(const SVNRevRangeArray& revArray);

// Dialog Data
    enum { IDD = IDD_LOGMESSAGE };

protected:
    //implement the virtual methods from SVN base class
    virtual BOOL Log(svn_revnum_t rev, const std::string& author, const std::string& message, apr_time_t time, const MergeInfo* mergeInfo);
    virtual BOOL Cancel();
    virtual bool Validate(LPCTSTR string);
    virtual bool FilterConditionChanged();

    virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

    afx_msg LRESULT OnFindDialogMessage(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnClickedInfoIcon(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnClickedCancelFilter(WPARAM wParam, LPARAM lParam);
    afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
    afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
    afx_msg void OnBnClickedGetall();
    afx_msg void OnNMDblclkChangedFileList(NMHDR *pNMHDR, LRESULT *pResult);
    afx_msg void OnNMDblclkLoglist(NMHDR *pNMHDR, LRESULT *pResult);
    afx_msg void OnLvnItemchangedLoglist(NMHDR *pNMHDR, LRESULT *pResult);
    afx_msg void OnBnClickedHelp();
    afx_msg void OnEnLinkMsgview(NMHDR *pNMHDR, LRESULT *pResult);
    afx_msg void OnBnClickedStatbutton();
    afx_msg void OnNMCustomdrawLoglist(NMHDR *pNMHDR, LRESULT *pResult);
    afx_msg void OnNMCustomdrawChangedFileList(NMHDR *pNMHDR, LRESULT *pResult);
    afx_msg void OnLvnGetdispinfoLoglist(NMHDR *pNMHDR, LRESULT *pResult);
    afx_msg void OnLvnGetdispinfoChangedFileList(NMHDR *pNMHDR, LRESULT *pResult);
    afx_msg void OnEnChangeSearchedit();
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnDtnDatetimechangeDateto(NMHDR *pNMHDR, LRESULT *pResult);
    afx_msg void OnDtnDatetimechangeDatefrom(NMHDR *pNMHDR, LRESULT *pResult);
    afx_msg void OnLvnColumnclick(NMHDR *pNMHDR, LRESULT *pResult);
    afx_msg void OnLvnColumnclickChangedFileList(NMHDR *pNMHDR, LRESULT *pResult);
    afx_msg void OnBnClickedNexthundred();
    afx_msg void OnBnClickedHidepaths();
    afx_msg void OnBnClickedCheckStoponcopy();
    afx_msg void OnLvnOdfinditemLoglist(NMHDR *pNMHDR, LRESULT *pResult);
    afx_msg void OnDtnDropdownDatefrom(NMHDR *pNMHDR, LRESULT *pResult);
    afx_msg void OnDtnDropdownDateto(NMHDR *pNMHDR, LRESULT *pResult);
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnBnClickedIncludemerge();
    afx_msg void OnBnClickedRefresh();
    afx_msg void OnRefresh();
    afx_msg void OnFind();
    afx_msg void OnFocusFilter();
    afx_msg void OnEditCopy();
    afx_msg void OnLogCancel();
    afx_msg void OnLvnKeydownLoglist(NMHDR *pNMHDR, LRESULT *pResult);
    afx_msg void OnLvnKeydownFilelist(NMHDR *pNMHDR, LRESULT *pResult);
    afx_msg void OnNMClickLoglist(NMHDR *pNMHDR, LRESULT *pResult);
    afx_msg void OnEnscrollMsgview();

    virtual void OnCancel();
    virtual void OnOK();
    virtual BOOL OnInitDialog();
    virtual BOOL PreTranslateMessage(MSG* pMsg);

    void    FillLogMessageCtrl(bool bShow = true);
    void    DoDiffFromLog(INT_PTR selIndex, svn_revnum_t rev1, svn_revnum_t rev2, bool blame, bool unified);

    DECLARE_MESSAGE_MAP()

private:
    void LogThread();
    void StatusThread();
    void Refresh (bool autoGoOnline = false);
    BOOL IsDiffPossible (const CLogChangedPath& changedpath, svn_revnum_t rev);
    BOOL Open(bool bOpenWith, CString changedpath, svn_revnum_t rev);
    void EditAuthor(const std::vector<PLOGENTRYDATA>& logs);
    void EditLogMessage(int index);
    void DoSizeV1(int delta);
    void DoSizeV2(int delta);
    void AdjustMinSize();
    void SetSplitterRange();
    void SetFilterCueText();
    void CopySelectionToClipBoard();
    void CopySelectionToClipBoard(bool bIncludeChangedList);
    void CopyChangedSelectionToClipBoard();
    CTSVNPathList GetChangedPathsAndMessageSketchFromSelectedRevisions(CString& sMessageSketch, CLogChangedPathArray& currentChangedArray);
    void RecalculateShownList(svn_revnum_t revToKeep = -1);
    void SetSortArrow(CListCtrl * control, int nColumn, bool bAscending);
    void SortByColumn(int nSortColumn, bool bAscending);
    void SortAndFilter (svn_revnum_t revToKeep = -1);
    bool IsSelectionContinuous();
    void EnableOKButton();
    void GetAll(bool bForceAll = false);
    void UpdateSelectedRevs();
    void UpdateLogInfoLabel();
    void SaveSplitterPos();
    bool ValidateRegexp(LPCTSTR regexp_str, tr1::wregex& pat, bool bMatchCase);
    void CheckRegexpTooltip();
    void DiffSelectedFile();
    void DiffSelectedRevWithPrevious();
    void SetDlgTitle(bool bOffline);
    void ToggleCheckbox(size_t item);
    void SelectAllVisibleRevisions();
    void AddMainAnchors();
    void RemoveMainAnchors();
    void AdjustDateFilterVisibility();
    void ReportNoUrlOfFile(const CString& filepath) const;
    void ReportNoUrlOfFile(LPCTSTR filepath) const;
    CRect DrawListColumnBackground(CListCtrl& listCtrl, NMLVCUSTOMDRAW * pLVCD, PLOGENTRYDATA pLogEntry);
    LRESULT DrawListItemWithMatches(CListCtrl& listCtrl, NMLVCUSTOMDRAW * pLVCD, PLOGENTRYDATA pLogEntry);

    inline int ShownCountWithStopped() const { return (int)m_logEntries.GetVisibleCount() + (m_bStrictStopped ? 1 : 0); }

    virtual LRESULT DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam);

    void ResizeAllListCtrlCols(bool bOnlyVisible);

    void ShowContextMenuForRevisions(CWnd* pWnd, CPoint point);
    void ShowContextMenuForChangedpaths(CWnd* pWnd, CPoint point);

    virtual CString GetToolTipText(int nItem, int nSubItem);
    bool DoFindItemLogList(LPNMLVFINDITEM pFindInfo, size_t startIndex, size_t endIndex,
        const CString& whatToFind, LRESULT *pResult);
    void NotifyTargetOnOk();
    void CreateFindDialog();
    void DoOpenFileWith(bool bOpenWith, const CTSVNPath& tempfile);
    bool ConfirmRevert(const CString& path, bool bToRev = false);

    // selection management

    void AutoStoreSelection();
    void AutoRestoreSelection();

    // ListViewAccProvider
    virtual CString GetListviewHelpString(HWND hControl, int index);
public:
    CWnd *              m_pNotifyWindow;
    ProjectProperties   m_ProjectProperties;
    WORD                m_wParam;
private:
    HFONT               m_boldFont;
    CString             m_sRelativeRoot;
    CString             m_sRepositoryRoot;
    CString             m_sURL;
    CString             m_sUUID;    ///< empty if the log cache is not used
    CHintCtrl<CSubTooltipListCtrl> m_LogList;
    CListCtrl           m_ChangedFileListCtrl;
    CFilterEdit         m_cFilter;
    CLogDlgFilter       m_filter;
    CProgressCtrl       m_LogProgress;
    CMFCMenuButton      m_btnShow;
    CMenu               m_btnMenu;
    CTSVNPath           m_path;
    CTSVNPath           m_mergePath;
    SVNRev              m_pegrev;
    SVNRev              m_startrev;
    svn_revnum_t        m_head;     ///< only used in Range case of log
    bool                m_bRefresh;
    svn_revnum_t        m_temprev;  ///< only used during ReceiveLog
    SVNRev              m_LogRevision;
    SVNRev              m_endrev;
    SVNRev              m_wcRev;
    SVNRevRangeArray    m_selectedRevs;
    SVNRevRangeArray    m_selectedRevsOneRange;
    bool                m_bSelectionMustBeContinuous;
    bool                m_bCancelled;
    volatile LONG       m_bLogThreadRunning;
    BOOL                m_bStrict;
    bool                m_bStrictStopped;
    BOOL                m_bIncludeMerges;
    BOOL                m_bSaveStrict;
    BOOL                m_bHideNonMergeables;

    bool                m_bSingleRevision;
    CLogChangedPathArray m_currentChangedArray;
    CTSVNPathList       m_currentChangedPathList;
    bool                m_hasWC;
    int                 m_nSearchIndex;
    bool                m_bFilterWithRegex;
    bool                m_bFilterCaseSensitively;
    static const UINT   m_FindDialogMessage;
    CFindReplaceDialog *m_pFindDialog;
    CFont               m_logFont;
    CString             m_sMessageBuf;
    CSplitterControl    m_wndSplitter1;
    CSplitterControl    m_wndSplitter2;
    CRect               m_DlgOrigRect;
    CRect               m_MsgViewOrigRect;
    CRect               m_LogListOrigRect;
    CRect               m_ChgOrigRect;
    CString             m_sFilterText;
    DWORD               m_SelectedFilters;
    CDateTimeCtrl       m_DateFrom;
    CDateTimeCtrl       m_DateTo;
    __time64_t          m_tFrom;
    __time64_t          m_tTo;
    int                 m_limit;
    int                 m_nSortColumn;
    bool                m_bAscending;
    int                 m_nSortColumnPathList;
    bool                m_bAscendingPathList;
    CRegDWORD           m_regLastStrict;
    CRegDWORD           m_regMaxBugIDColWidth;
    CButton             m_cShowPaths;
    bool                m_bShowedAll;
    CString             m_sTitle;
    bool                m_bSelect;
    bool                m_bShowBugtraqColumn;
    CString             m_sLogInfo;
    std::set<svn_revnum_t> m_mergedRevs;
    SVNRev              m_copyfromrev;

    CToolTips           m_tooltips;

    CTime               m_timFrom;
    CTime               m_timTo;
    CColors             m_Colors;
    CImageList          m_imgList;
    HICON               m_hModifiedIcon;
    HICON               m_hReplacedIcon;
    HICON               m_hAddedIcon;
    HICON               m_hDeletedIcon;
    HICON               m_hMergedIcon;
    HICON               m_hReverseMergedIcon;
    int                 m_nIconFolder;

    HACCEL              m_hAccel;

    CStoreSelection*    m_pStoreSelection;
    CLogDataVector      m_logEntries;
    size_t              m_prevLogEntriesSize;

    CComPtr<ITaskbarList3>  m_pTaskbarList;

    async::CJobScheduler netScheduler;
    async::CJobScheduler diskScheduler;

    ListViewAccServer * m_pLogListAccServer;
    ListViewAccServer * m_pChangedListAccServer;
};
static UINT WM_REVSELECTED = RegisterWindowMessage(_T("TORTOISESVN_REVSELECTED_MSG"));
static UINT WM_REVLIST = RegisterWindowMessage(_T("TORTOISESVN_REVLIST_MSG"));
static UINT WM_REVLISTONERANGE = RegisterWindowMessage(_T("TORTOISESVN_REVLISTONERANGE_MSG"));
