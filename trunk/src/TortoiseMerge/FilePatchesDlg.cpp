// TortoiseMerge - a Diff/Patch program

// Copyright (C) 2006 - Stefan Kueng

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
#include "TortoiseMerge.h"
#include "FilePatchesDlg.h"
#include "Patch.h"
#include "Utils.h"
#include ".\filepatchesdlg.h"


IMPLEMENT_DYNAMIC(CFilePatchesDlg, CDialog)
CFilePatchesDlg::CFilePatchesDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CFilePatchesDlg::IDD, pParent)
{
	m_ImgList.Create(16, 16, ILC_COLOR16 | ILC_MASK, 4, 1);
	m_bMinimized = FALSE;
}

CFilePatchesDlg::~CFilePatchesDlg()
{
}

void CFilePatchesDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_FILELIST, m_cFileList);
}

BOOL CFilePatchesDlg::SetFileStatusAsPatched(CString sPath)
{
	for (int i=0; i<m_arFileStates.GetCount(); i++)
	{
		if (sPath.CompareNoCase(GetFullPath(i))==0)
		{
			m_arFileStates.SetAt(i, FPDLG_FILESTATE_PATCHED);
			Invalidate();
			return TRUE;
		} // if (sPath.CompareNoCase(GetFullPath(i))==0) 
	} // for (int i=0; i<m_arFileStates.GetCount(); i++) 
	return FALSE;
}

CString CFilePatchesDlg::GetFullPath(int nIndex)
{
	CString temp = m_pPatch->GetFilename(nIndex);
	temp.Replace('/', '\\');
	//temp = temp.Mid(temp.Find('\\')+1);
	if (PathIsRelative(temp))
		temp = m_sPath + temp;
	return temp;
}

BOOL CFilePatchesDlg::Init(CPatch * pPatch, CPatchFilesDlgCallBack * pCallBack, CString sPath, CWnd * pParent)
{
	if ((pCallBack==NULL)||(pPatch==NULL))
	{
		m_cFileList.DeleteAllItems();
		return FALSE;
	}
	m_arFileStates.RemoveAll();
	m_pPatch = pPatch;
	m_pCallBack = pCallBack;
	m_sPath = sPath;
	if (m_sPath.IsEmpty())
	{
		CString title(MAKEINTRESOURCE(IDS_DIFF_TITLE));
		SetWindowText(title);
	}
	else
	{
		CString title;
		title.LoadString(IDS_PATCH_TITLE);
		title += _T("  ") + m_sPath;
		CRect rect;
		GetClientRect(&rect);
		PathCompactPath(GetDC()->m_hDC, title.GetBuffer(), rect.Width());
		title.ReleaseBuffer();
		SetWindowText(title);
		if (m_sPath.Right(1).Compare(_T("\\"))==0)
			m_sPath = m_sPath.Left(m_sPath.GetLength()-1);

		m_sPath = m_sPath + _T("\\");
		for (int i=m_ImgList.GetImageCount();i>0;i--)
		{
			m_ImgList.Remove(0);
		}
	}

	m_cFileList.SetExtendedStyle(LVS_EX_INFOTIP | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	m_cFileList.DeleteAllItems();
	int c = ((CHeaderCtrl*)(m_cFileList.GetDlgItem(0)))->GetItemCount()-1;
	while (c>=0)
		m_cFileList.DeleteColumn(c--);
	m_cFileList.InsertColumn(0, _T(""));

	m_cFileList.SetRedraw(false);

	for(int i=0; i<m_pPatch->GetNumberOfFiles(); i++)
	{
		CString sFile = CUtils::GetFileNameFromPath(m_pPatch->GetFilename(i));
		DWORD state;
		if (m_sPath.IsEmpty())
			state = FPDLG_FILESTATE_GOOD;
		else
		{
			if (m_pPatch->PatchFile(GetFullPath(i)))
				state = FPDLG_FILESTATE_GOOD;
			else
				state = FPDLG_FILESTATE_CONFLICTED;
		}
		m_arFileStates.Add(state);
		SHFILEINFO    sfi;
		SHGetFileInfo(
			GetFullPath(i), 
			FILE_ATTRIBUTE_NORMAL,
			&sfi, 
			sizeof(SHFILEINFO), 
			SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
		m_cFileList.InsertItem(i, sFile, m_ImgList.Add(sfi.hIcon));

	} // for(int i=0; i<m_pPatch->GetNumberOfFiles(); i++) 
	int mincol = 0;
	int maxcol = ((CHeaderCtrl*)(m_cFileList.GetDlgItem(0)))->GetItemCount()-1;
	int col;
	for (col = mincol; col <= maxcol; col++)
	{
		m_cFileList.SetColumnWidth(col,LVSCW_AUTOSIZE_USEHEADER);
	}

	m_cFileList.SetImageList(&m_ImgList, LVSIL_SMALL);
	m_cFileList.SetRedraw(true);

	RECT parentrect;
	pParent->GetWindowRect(&parentrect);
	RECT windowrect;
	GetWindowRect(&windowrect);

	int width = windowrect.right - windowrect.left;
	int height = windowrect.bottom - windowrect.top;
	windowrect.right = parentrect.left;
	windowrect.left = windowrect.right - width;
	if (windowrect.left < 0)
	{
		windowrect.left = 0;
		windowrect.right = width;
	}
	windowrect.top = parentrect.top;
	windowrect.bottom = windowrect.top + height;

	SetWindowPos(NULL, windowrect.left, windowrect.top, width, height, SWP_NOACTIVATE | SWP_NOZORDER);

	m_nWindowHeight = windowrect.bottom - windowrect.top;
	m_pMainFrame = pParent;
	return TRUE;
}

BEGIN_MESSAGE_MAP(CFilePatchesDlg, CDialog)
	ON_WM_SIZE()
	ON_NOTIFY(LVN_GETINFOTIP, IDC_FILELIST, OnLvnGetInfoTipFilelist)
	ON_NOTIFY(NM_DBLCLK, IDC_FILELIST, OnNMDblclkFilelist)
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_FILELIST, OnNMCustomdrawFilelist)
	ON_NOTIFY(NM_RCLICK, IDC_FILELIST, OnNMRclickFilelist)
	ON_WM_NCLBUTTONDBLCLK()
	ON_WM_MOVING()
END_MESSAGE_MAP()

void CFilePatchesDlg::OnSize(UINT nType, int cx, int cy)
{
	CDialog::OnSize(nType, cx, cy);
	if (this->IsWindowVisible())
	{
		CRect rect;
		GetClientRect(rect);
		GetDlgItem(IDC_FILELIST)->MoveWindow(rect.left, rect.top, cx, cy);
		m_cFileList.SetColumnWidth(0, cx);
	}
	CString title;
	title.LoadString(IDS_PATCH_TITLE);
	title += _T("  ") + m_sPath;
	PathCompactPath(GetDC()->m_hDC, title.GetBuffer(), cx);
	title.ReleaseBuffer();
	SetWindowText(title);
}

void CFilePatchesDlg::OnLvnGetInfoTipFilelist(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMLVGETINFOTIP pGetInfoTip = reinterpret_cast<LPNMLVGETINFOTIP>(pNMHDR);

	CString temp = GetFullPath(pGetInfoTip->iItem);
	_tcsncpy_s(pGetInfoTip->pszText, pGetInfoTip->cchTextMax, temp, pGetInfoTip->cchTextMax);
	*pResult = 0;
}

void CFilePatchesDlg::OnNMDblclkFilelist(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
	*pResult = 0;
	if ((pNMLV->iItem < 0) || (pNMLV->iItem >= m_arFileStates.GetCount()))
		return;
	if (m_pCallBack==NULL)
		return;
	if (m_sPath.IsEmpty())
	{
		m_pCallBack->DiffFiles(GetFullPath(pNMLV->iItem), m_pPatch->GetRevision(pNMLV->iItem),
							   m_pPatch->GetFilename2(pNMLV->iItem), m_pPatch->GetRevision2(pNMLV->iItem));
	}
	else
	{
		if (m_arFileStates.GetAt(pNMLV->iItem)!=FPDLG_FILESTATE_PATCHED)
		{
			m_pCallBack->PatchFile(GetFullPath(pNMLV->iItem), m_pPatch->GetRevision(pNMLV->iItem));
		}
	}
}

void CFilePatchesDlg::OnNMCustomdrawFilelist(NMHDR *pNMHDR, LRESULT *pResult)
{
	NMLVCUSTOMDRAW* pLVCD = reinterpret_cast<NMLVCUSTOMDRAW*>( pNMHDR );

	// Take the default processing unless we set this to something else below.
	*pResult = CDRF_DODEFAULT;

	// First thing - check the draw stage. If it's the control's prepaint
	// stage, then tell Windows we want messages for every item.

	if ( CDDS_PREPAINT == pLVCD->nmcd.dwDrawStage )
	{
		*pResult = CDRF_NOTIFYITEMDRAW;
	}
	else if ( CDDS_ITEMPREPAINT == pLVCD->nmcd.dwDrawStage )
	{
		// This is the prepaint stage for an item. Here's where we set the
		// item's text color. Our return value will tell Windows to draw the
		// item itself, but it will use the new color we set here.

		COLORREF crText = ::GetSysColor(COLOR_WINDOWTEXT);

		if (m_arFileStates.GetCount() > (INT_PTR)pLVCD->nmcd.dwItemSpec)
		{
			if (m_arFileStates.GetAt(pLVCD->nmcd.dwItemSpec)==FPDLG_FILESTATE_CONFLICTED)
			{
				crText = RGB(200, 0, 0);
			}
			if (m_arFileStates.GetAt(pLVCD->nmcd.dwItemSpec)==FPDLG_FILESTATE_PATCHED)
			{
				crText = ::GetSysColor(COLOR_GRAYTEXT);
			}
			// Store the color back in the NMLVCUSTOMDRAW struct.
			pLVCD->clrText = crText;
		} // if (m_arFileStates.GetCount() > (INT_PTR)pLVCD->nmcd.dwItemSpec) 

		// Tell Windows to paint the control itself.
		*pResult = CDRF_DODEFAULT;
	}
}

void CFilePatchesDlg::OnNMRclickFilelist(NMHDR * /*pNMHDR*/, LRESULT *pResult)
{
	*pResult = 0;
	if (m_sPath.IsEmpty())
		return;
	CString temp;
	CMenu popup;
	POINT point;
	GetCursorPos(&point);
	if (popup.CreatePopupMenu())
	{
		UINT nFlags;
		
		nFlags = MF_STRING | (m_cFileList.GetSelectedCount()==1 ? MF_ENABLED : MF_DISABLED | MF_GRAYED);
		temp.LoadString(IDS_PATCH_PREVIEW);
		popup.AppendMenu(nFlags, ID_PATCHPREVIEW, temp);
		popup.SetDefaultItem(ID_PATCHPREVIEW, FALSE);

		temp.LoadString(IDS_PATCH_ALL);
		popup.AppendMenu(MF_STRING | MF_ENABLED, ID_PATCHALL, temp);
		
		nFlags = MF_STRING | (m_cFileList.GetSelectedCount()>0 ? MF_ENABLED : MF_DISABLED | MF_GRAYED);
		temp.LoadString(IDS_PATCH_SELECTED);
		popup.AppendMenu(nFlags, ID_PATCHSELECTED, temp);
		
		int cmd = popup.TrackPopupMenu(TPM_RETURNCMD | TPM_LEFTALIGN | TPM_NONOTIFY, point.x, point.y, this, 0);
		switch (cmd)
		{
		case ID_PATCHPREVIEW:
			{
				if (m_pCallBack)
				{
					int nIndex = m_cFileList.GetSelectionMark();
					if ( m_arFileStates.GetAt(nIndex)!=FPDLG_FILESTATE_PATCHED)
					{
						m_pCallBack->PatchFile(GetFullPath(nIndex), m_pPatch->GetRevision(nIndex));
					}
				}
			}
			break;
		case ID_PATCHALL:
			{
				if (m_pCallBack)
				{
					for (int i=0; i<m_arFileStates.GetCount(); i++)
					{
						if (m_arFileStates.GetAt(i)!= FPDLG_FILESTATE_PATCHED)
							m_pCallBack->PatchFile(GetFullPath(i), m_pPatch->GetRevision(i), TRUE);
					} // for (int i=0; i<m_arFileStates.GetCount(); i++) 
				} // if ((m_pCallBack)&&(!temp.IsEmpty())) 
			} 
			break;
		case ID_PATCHSELECTED:
			{
				if (m_pCallBack)
				{
					// The list cannot be sorted by user, so the order of the
					// items in the list is identical to the order in the array
					// m_arFileStates.
					POSITION pos = m_cFileList.GetFirstSelectedItemPosition();
					int index;
					while ((index = m_cFileList.GetNextSelectedItem(pos)) >= 0)
					{
						if (m_arFileStates.GetAt(index)!= FPDLG_FILESTATE_PATCHED)
							m_pCallBack->PatchFile(GetFullPath(index), m_pPatch->GetRevision(index), TRUE);
					}
				} // if (m_pCallBack) 
			} 
			break;
		default:
			break;
		} // switch (cmd) 
	} // if (popup.CreatePopupMenu()) 
}

void CFilePatchesDlg::OnNcLButtonDblClk(UINT nHitTest, CPoint point)
{
	if (!m_bMinimized)
	{
		RECT windowrect;
		RECT clientrect;
		GetWindowRect(&windowrect);
		GetClientRect(&clientrect);
		m_nWindowHeight = windowrect.bottom - windowrect.top;
		MoveWindow(windowrect.left, windowrect.top, 
			windowrect.right - windowrect.left,
			m_nWindowHeight - (clientrect.bottom - clientrect.top));
	}
	else
	{
		RECT windowrect;
		GetWindowRect(&windowrect);
		MoveWindow(windowrect.left, windowrect.top, windowrect.right - windowrect.left, m_nWindowHeight);
	}
	m_bMinimized = !m_bMinimized;
	CDialog::OnNcLButtonDblClk(nHitTest, point);
}

void CFilePatchesDlg::OnMoving(UINT fwSide, LPRECT pRect)
{
#define STICKYSIZE 5
	RECT parentRect;
	m_pMainFrame->GetWindowRect(&parentRect);
	if (abs(parentRect.left - pRect->right) < STICKYSIZE)
	{
		int width = pRect->right - pRect->left;
		pRect->right = parentRect.left;
		pRect->left = pRect->right - width;
	}
	CDialog::OnMoving(fwSide, pRect);
}

void CFilePatchesDlg::OnCancel()
{
	return;
}

void CFilePatchesDlg::OnOK()
{
	return;
}
