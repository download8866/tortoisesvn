// TortoiseSVN - a Windows shell extension for easy version control

// Copyright (C) 2003-2004 - Stefan Kueng

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
#pragma once
#include "ResizableDialog.h"
#include "afxcmn.h"


// CSetOverlayIcons dialog

class CSetOverlayIcons : public CResizableDialog
{
	DECLARE_DYNAMIC(CSetOverlayIcons)

public:
	CSetOverlayIcons(CWnd* pParent = NULL);   // standard constructor
	virtual ~CSetOverlayIcons();

// Dialog Data
	enum { IDD = IDD_OVERLAYICONS };

protected:
	virtual void			DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	virtual BOOL			OnInitDialog();
	virtual void			OnOK();
	afx_msg void			OnPaint();
	afx_msg HCURSOR			OnQueryDragIcon();
	afx_msg void			OnBnClickedListradio();
	afx_msg void			OnBnClickedSymbolradio();
	afx_msg void			OnCbnSelchangeIconsetcombo();
	afx_msg void			OnBnClickedHelp();

	void					ShowIconSet(bool bSmallIcons);
	void					AddFileTypeGroup(CString sFileType, bool bSmallIcons);
	DECLARE_MESSAGE_MAP()
protected:
	HICON			m_hIcon;
	CComboBox		m_cIconSet;
	CListCtrl		m_cIconList;

	CString			m_sIconPath;
	CString			m_sOriginalIconSet;
	CString			m_sNormal;
	CString			m_sModified;
	CString			m_sConflicted;
	CString			m_sAdded;
	CString			m_sDeleted;
	CImageList		m_ImageList;
	CImageList		m_ImageListBig;

	CRegString		m_regInSubversion;
	CRegString		m_regModified;
	CRegString		m_regConflicted;
	CRegString		m_regAdded;
	CRegString		m_regDeleted;
};
