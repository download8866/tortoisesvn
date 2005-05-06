// TortoiseSVN - a Windows shell extension for easy version control

// Copyright (C) 2003-2005 - Stefan Kueng

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
#include "stdafx.h"
#include "TortoiseProc.h"
#include "SetOverlayPage.h"
#include "SetOverlayIcons.h"
#include "Globals.h"
#include "ShellUpdater.h"
#include ".\setoverlaypage.h"


// CSetOverlayPage dialog

IMPLEMENT_DYNAMIC(CSetOverlayPage, CPropertyPage)
CSetOverlayPage::CSetOverlayPage()
	: CPropertyPage(CSetOverlayPage::IDD)
	, m_bInitialized(FALSE)
	, m_bRemovable(FALSE)
	, m_bNetwork(FALSE)
	, m_bFixed(FALSE)
	, m_bCDROM(FALSE)
	, m_bRAM(FALSE)
	, m_bUnknown(FALSE)
	, m_bOnlyExplorer(FALSE)
	, m_sExcludePaths(_T(""))
	, m_sIncludePaths(_T(""))
{
	m_regOnlyExplorer = CRegDWORD(_T("Software\\TortoiseSVN\\OverlaysOnlyInExplorer"), FALSE);
	m_regDriveMaskRemovable = CRegDWORD(_T("Software\\TortoiseSVN\\DriveMaskRemovable"));
	m_regDriveMaskRemote = CRegDWORD(_T("Software\\TortoiseSVN\\DriveMaskRemote"));
	m_regDriveMaskFixed = CRegDWORD(_T("Software\\TortoiseSVN\\DriveMaskFixed"), TRUE);
	m_regDriveMaskCDROM = CRegDWORD(_T("Software\\TortoiseSVN\\DriveMaskCDROM"));
	m_regDriveMaskRAM = CRegDWORD(_T("Software\\TortoiseSVN\\DriveMaskRAM"));
	m_regDriveMaskUnknown = CRegDWORD(_T("Software\\TortoiseSVN\\DriveMaskUnknown"));
	m_regTopmenu = CRegDWORD(_T("Software\\TortoiseSVN\\ContextMenuEntries"), MENUCHECKOUT | MENUUPDATE | MENUCOMMIT);
	m_regExcludePaths = CRegString(_T("Software\\TortoiseSVN\\OverlayExcludeList"));
	m_regIncludePaths = CRegString(_T("Software\\TortoiseSVN\\OverlayIncludeList"));

	m_bOnlyExplorer = m_regOnlyExplorer;
	m_bRemovable = m_regDriveMaskRemovable;
	m_bNetwork = m_regDriveMaskRemote;
	m_bFixed = m_regDriveMaskFixed;
	m_bCDROM = m_regDriveMaskCDROM;
	m_bRAM = m_regDriveMaskRAM;
	m_bUnknown = m_regDriveMaskUnknown;
	m_topmenu = m_regTopmenu;
	m_sExcludePaths = m_regExcludePaths;
	m_sExcludePaths.Replace(_T("\n"), _T("\r\n"));
	m_sIncludePaths = m_regIncludePaths;
	m_sIncludePaths.Replace(_T("\n"), _T("\r\n"));
}

CSetOverlayPage::~CSetOverlayPage()
{
}

void CSetOverlayPage::DoDataExchange(CDataExchange* pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	DDX_Check(pDX, IDC_REMOVABLE, m_bRemovable);
	DDX_Check(pDX, IDC_NETWORK, m_bNetwork);
	DDX_Check(pDX, IDC_FIXED, m_bFixed);
	DDX_Check(pDX, IDC_CDROM, m_bCDROM);
	DDX_Check(pDX, IDC_RAM, m_bRAM);
	DDX_Check(pDX, IDC_UNKNOWN, m_bUnknown);
	DDX_Control(pDX, IDC_DRIVEGROUP, m_cDriveGroup);
	DDX_Check(pDX, IDC_ONLYEXPLORER, m_bOnlyExplorer);
	DDX_Control(pDX, IDC_MENULIST, m_cMenuList);
	DDX_Text(pDX, IDC_EXCLUDEPATHS, m_sExcludePaths);
	DDX_Text(pDX, IDC_INCLUDEPATHS, m_sIncludePaths);
}


BEGIN_MESSAGE_MAP(CSetOverlayPage, CPropertyPage)
	ON_BN_CLICKED(IDC_REMOVABLE, OnBnClickedRemovable)
	ON_BN_CLICKED(IDC_NETWORK, OnBnClickedNetwork)
	ON_BN_CLICKED(IDC_FIXED, OnBnClickedFixed)
	ON_BN_CLICKED(IDC_CDROM, OnBnClickedCdrom)
	ON_BN_CLICKED(IDC_UNKNOWN, OnBnClickedUnknown)
	ON_BN_CLICKED(IDC_RAM, OnBnClickedRam)
	ON_BN_CLICKED(IDC_ONLYEXPLORER, OnBnClickedOnlyexplorer)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_MENULIST, OnLvnItemchangedMenulist)
	ON_EN_CHANGE(IDC_EXCLUDEPATHS, OnEnChangeExcludepaths)
	ON_EN_CHANGE(IDC_INCLUDEPATHS, OnEnChangeIncludepaths)
END_MESSAGE_MAP()


void CSetOverlayPage::SaveData()
{
	if (m_bInitialized)
	{
		m_regOnlyExplorer = m_bOnlyExplorer;
		m_regDriveMaskRemovable = m_bRemovable;
		m_regDriveMaskRemote = m_bNetwork;
		m_regDriveMaskFixed = m_bFixed;
		m_regDriveMaskCDROM = m_bCDROM;
		m_regDriveMaskRAM = m_bRAM;
		m_regDriveMaskUnknown = m_bUnknown;
		m_regTopmenu = m_topmenu;
		m_sExcludePaths.Replace(_T("\r"), _T(""));
		if (m_sExcludePaths.Right(1).Compare(_T("\n"))!=0)
			m_sExcludePaths += _T("\n");
		m_regExcludePaths = m_sExcludePaths;
		m_sExcludePaths.Replace(_T("\n"), _T("\r\n"));
		m_sIncludePaths.Replace(_T("\r"), _T(""));
		if (m_sIncludePaths.Right(1).Compare(_T("\n"))!=0)
			m_sIncludePaths += _T("\n");
		m_regIncludePaths = m_sIncludePaths;
		m_sIncludePaths.Replace(_T("\n"), _T("\r\n"));
	}
}

BOOL CSetOverlayPage::OnInitDialog()
{
	CPropertyPage::OnInitDialog();

	m_cDriveGroup.SetIcon(IDI_DRIVES);

	m_tooltips.Create(this);
	m_tooltips.AddTool(IDC_ONLYEXPLORER, IDS_SETTINGS_ONLYEXPLORER_TT);
	m_tooltips.AddTool(IDC_MENULIST, IDS_SETTINGS_MENULAYOUT_TT);
	m_tooltips.AddTool(IDC_EXCLUDEPATHS, IDS_SETTINGS_EXCLUDELIST_TT);	
	m_tooltips.AddTool(IDC_INCLUDEPATHS, IDS_SETTINGS_INCLUDELIST_TT);	

	m_cMenuList.SetExtendedStyle(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);

	m_cMenuList.DeleteAllItems();
	int c = ((CHeaderCtrl*)(m_cMenuList.GetDlgItem(0)))->GetItemCount()-1;
	while (c>=0)
		m_cMenuList.DeleteColumn(c--);
	m_cMenuList.InsertColumn(0, _T(""));

	m_cMenuList.SetRedraw(false);

	m_imgList.Create(16, 16, ILC_COLOR16 | ILC_MASK, 4, 1);

	InsertItem(IDS_MENUCHECKOUT, IDI_CHECKOUT, MENUCHECKOUT);
	InsertItem(IDS_MENUUPDATE, IDI_UPDATE, MENUUPDATE);
	InsertItem(IDS_MENUCOMMIT, IDI_COMMIT, MENUCOMMIT);
	InsertItem(IDS_MENUDIFF, IDI_DIFF, MENUDIFF);
	InsertItem(IDS_MENULOG, IDI_LOG, MENULOG);
	InsertItem(IDS_MENUSHOWCHANGED, IDI_SHOWCHANGED, MENUSHOWCHANGED);
	InsertItem(IDS_MENUREVISIONGRAPH, IDI_REVISIONGRAPH, MENUREVISIONGRAPH);
	InsertItem(IDS_MENUREPOBROWSE, IDI_REPOBROWSE, MENUREPOBROWSE);
	InsertItem(IDS_MENUCONFLICT, IDI_CONFLICT, MENUCONFLICTEDITOR);
	InsertItem(IDS_MENURESOLVE, IDI_RESOLVE, MENURESOLVE);
	InsertItem(IDS_MENUUPDATEEXT, IDI_UPDATE, MENUUPDATEEXT);
	InsertItem(IDS_MENURENAME, IDI_RENAME, MENURENAME);
	InsertItem(IDS_MENUREMOVE, IDI_DELETE, MENUREMOVE);
	InsertItem(IDS_MENUREVERT, IDI_REVERT, MENUREVERT);
	InsertItem(IDS_MENUCLEANUP, IDI_CLEANUP, MENUCLEANUP);
	InsertItem(IDS_MENU_LOCK, IDI_LOCK, MENULOCK);
	InsertItem(IDS_MENU_UNLOCK, IDI_UNLOCK, MENUUNLOCK);
	InsertItem(IDS_MENUBRANCH, IDI_COPY, MENUCOPY);
	InsertItem(IDS_MENUSWITCH, IDI_SWITCH, MENUSWITCH);
	InsertItem(IDS_MENUMERGE, IDI_MERGE, MENUMERGE);
	InsertItem(IDS_MENUEXPORT, IDI_EXPORT, MENUEXPORT);
	InsertItem(IDS_MENURELOCATE, IDI_RELOCATE, MENURELOCATE);
	InsertItem(IDS_MENUCREATEREPOS, IDI_CREATEREPOS, MENUCREATEREPOS);
	InsertItem(IDS_MENUADD, IDI_ADD, MENUADD);
	InsertItem(IDS_MENUIMPORT, IDI_IMPORT, MENUIMPORT);
	InsertItem(IDS_MENUBLAME, IDI_BLAME, MENUBLAME);
	InsertItem(IDS_MENUIGNORE, IDI_IGNORE, MENUIGNORE);
	InsertItem(IDS_MENUCREATEPATCH, IDI_CREATEPATCH, MENUCREATEPATCH);
	InsertItem(IDS_MENUAPPLYPATCH, IDI_PATCH, MENUAPPLYPATCH);

	m_cMenuList.SetImageList(&m_imgList, LVSIL_SMALL);
	int mincol = 0;
	int maxcol = ((CHeaderCtrl*)(m_cMenuList.GetDlgItem(0)))->GetItemCount()-1;
	int col;
	for (col = mincol; col <= maxcol; col++)
	{
		m_cMenuList.SetColumnWidth(col,LVSCW_AUTOSIZE_USEHEADER);
	}
	m_cMenuList.SetRedraw(true);

	m_bInitialized = TRUE;

	UpdateData(FALSE);

	return TRUE;  // return TRUE unless you set the focus to a control
	// EXCEPTION: OCX Property Pages should return FALSE
}

BOOL CSetOverlayPage::PreTranslateMessage(MSG* pMsg)
{
	m_tooltips.RelayEvent(pMsg);
	return CPropertyPage::PreTranslateMessage(pMsg);
}

void CSetOverlayPage::OnBnClickedRemovable()
{
	SetModified();
}

void CSetOverlayPage::OnBnClickedNetwork()
{
	SetModified();
}

void CSetOverlayPage::OnBnClickedFixed()
{
	SetModified();
}

void CSetOverlayPage::OnBnClickedCdrom()
{
	SetModified();
}

void CSetOverlayPage::OnBnClickedRam()
{
	SetModified();
}

void CSetOverlayPage::OnBnClickedUnknown()
{
	SetModified();
}

void CSetOverlayPage::OnBnClickedOnlyexplorer()
{
	SetModified();
}

BOOL CSetOverlayPage::OnApply()
{
	UpdateData();
	SaveData();
	SetModified(FALSE);
	return CPropertyPage::OnApply();
}

void CSetOverlayPage::InsertItem(UINT nTextID, UINT nIconID, DWORD dwFlags)
{
	HICON hIcon = reinterpret_cast<HICON>(::LoadImage(AfxGetResourceHandle(),
		MAKEINTRESOURCE(nIconID),
		IMAGE_ICON, 16, 16, LR_LOADTRANSPARENT ));
	int nImage = m_imgList.Add(hIcon);
	CString temp;
	temp.LoadString(nTextID);
	int nIndex = m_cMenuList.GetItemCount();
	m_cMenuList.InsertItem(nIndex, temp, nImage);
	DWORD topmenu = CRegDWORD(_T("Software\\TortoiseSVN\\ContextMenuEntries"), MENUCHECKOUT | MENUUPDATE | MENUCOMMIT);
	m_cMenuList.SetCheck(nIndex, !(topmenu & dwFlags));
}

void CSetOverlayPage::OnLvnItemchangedMenulist(NMHDR * /*pNMHDR*/, LRESULT *pResult)
{
	//LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	SetModified(TRUE);
	if (m_cMenuList.GetItemCount() > 0)
	{
		int i=0;
		m_topmenu = 0;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENUCHECKOUT;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENUUPDATE;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENUCOMMIT;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENUDIFF;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENULOG;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENUSHOWCHANGED;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENUREVISIONGRAPH;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENUREPOBROWSE;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENUCONFLICTEDITOR;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENURESOLVE;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENUUPDATEEXT;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENURENAME;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENUREMOVE;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENUREVERT;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENUCLEANUP;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENULOCK;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENUUNLOCK;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENUCOPY;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENUSWITCH;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENUMERGE;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENUEXPORT;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENURELOCATE;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENUCREATEREPOS;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENUADD;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENUIMPORT;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENUBLAME;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENUIGNORE;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENUCREATEPATCH;
		m_topmenu |= m_cMenuList.GetCheck(i++) ? 0 : MENUAPPLYPATCH;
	} // if (m_cMenuList.GetItemCount() > 0) 
	*pResult = 0;
}

void CSetOverlayPage::OnEnChangeExcludepaths()
{
	SetModified();
}

void CSetOverlayPage::OnEnChangeIncludepaths()
{
	SetModified();
}
