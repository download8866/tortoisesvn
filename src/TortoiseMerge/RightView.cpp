// TortoiseMerge - a Diff/Patch program

// Copyright (C) 2006-2013 - TortoiseSVN

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
#include "resource.h"
#include "AppUtils.h"
#include "IconBitmapUtils.h"

#include "RightView.h"
#include "BottomView.h"

IMPLEMENT_DYNCREATE(CRightView, CBaseView)

CRightView::CRightView(void)
{
    m_pwndRight = this;
    m_pState = &m_AllState.right;
    m_nStatusBarID = ID_INDICATOR_RIGHTVIEW;
}

CRightView::~CRightView(void)
{
}

void CRightView::UseBothLeftFirst()
{
    if (!IsLeftViewGood())
        return;
    int nFirstViewLine = 0; // first view line in selection
    int nLastViewLine  = 0; // last view line in selection

    if (!IsWritable())
        return;
    if (!GetViewSelection(nFirstViewLine, nLastViewLine))
        return;

    int nNextViewLine = nLastViewLine + 1; // first view line after selected block

    CUndo::GetInstance().BeginGrouping();

    // right original become added
    for (int viewLine = nFirstViewLine; viewLine <= nLastViewLine; viewLine++)
    {
        if (!IsStateEmpty(GetViewState(viewLine)))
        {
            SetViewState(viewLine, DIFFSTATE_YOURSADDED);
        }
    }
    SaveUndoStep();

    for (int viewLine = nFirstViewLine; viewLine <= nLastViewLine; viewLine++)
    {
        viewdata line = m_pwndLeft->GetViewData(viewLine);
        if (IsStateEmpty(line.state))
        {
            line.state = DIFFSTATE_EMPTY;
        }
        else
        {
            line.state = DIFFSTATE_THEIRSADDED;
        }
        InsertViewData(viewLine, line);
    }

    // now insert an empty block in left view
    int nCount = nLastViewLine - nFirstViewLine + 1;
    m_pwndLeft->InsertViewEmptyLines(nNextViewLine, nCount);
    SaveUndoStep();

    // clean up
    int nRemovedLines = CleanEmptyLines();
    SaveUndoStep();
    UpdateViewLineNumbers();
    SaveUndoStep();

    CUndo::GetInstance().EndGrouping();

    // final clean up
    ClearSelection();
    SetupAllViewSelection(nFirstViewLine, 2*nLastViewLine - nFirstViewLine - nRemovedLines + 1);
    BuildAllScreen2ViewVector();
    m_pwndLeft->Invalidate();
    SetModified();
    RefreshViews();
}

void CRightView::UseBothRightFirst()
{
    if (!IsLeftViewGood())
        return;
    int nFirstViewLine = 0; // first view line in selection
    int nLastViewLine  = 0; // last view line in selection

    if (!IsWritable())
        return;
    if (!GetViewSelection(nFirstViewLine, nLastViewLine))
        return;

    int nNextViewLine = nLastViewLine + 1; // first view line after selected block

    CUndo::GetInstance().BeginGrouping();

    // right original become added
    for (int viewLine = nFirstViewLine; viewLine <= nLastViewLine; viewLine++)
    {
        if (!IsStateEmpty(GetViewState(viewLine)))
        {
            SetViewState(viewLine, DIFFSTATE_ADDED);
        }
    }
    SaveUndoStep();

    // your block is done, now insert their block
    int viewindex = nNextViewLine;
    for (int viewLine = nFirstViewLine; viewLine <= nLastViewLine; viewLine++)
    {
        viewdata line = m_pwndLeft->GetViewData(viewLine);
        if (IsStateEmpty(line.state))
        {
            line.state = DIFFSTATE_EMPTY;
        }
        else
        {
            line.state = DIFFSTATE_THEIRSADDED;
        }
        InsertViewData(viewindex++, line);
    }
    SaveUndoStep();

    // now insert an empty block in left view
    int nCount = nLastViewLine - nFirstViewLine + 1;
    m_pwndLeft->InsertViewEmptyLines(nFirstViewLine, nCount);
    SaveUndoStep();

    // clean up
    int nRemovedLines = CleanEmptyLines();
    SaveUndoStep();
    UpdateViewLineNumbers();
    SaveUndoStep();

    CUndo::GetInstance().EndGrouping();

    // final clean up
    ClearSelection();
    SetupAllViewSelection(nFirstViewLine, 2*nLastViewLine - nFirstViewLine - nRemovedLines + 1);
    BuildAllScreen2ViewVector();
    m_pwndLeft->Invalidate();
    SetModified();
    RefreshViews();
}

void CRightView::UseLeftBlock()
{
    int nFirstViewLine = 0;
    int nLastViewLine  = 0;

    if (!IsWritable())
        return;
    if (!GetViewSelection(nFirstViewLine, nLastViewLine))
        return;

    return UseBlock(nFirstViewLine, nLastViewLine);
}

void CRightView::UseLeftFile()
{
    int nFirstViewLine = 0;
    int nLastViewLine = GetViewCount()-1;

    if (!IsWritable())
        return;
    ClearSelection();
    return UseBlock(nFirstViewLine, nLastViewLine);
}


void CRightView::AddContextItems(CIconMenu& popup, DiffStates state)
{
    const bool bShow = HasSelection() && (state != DIFFSTATE_UNKNOWN);

    if (IsBottomViewGood())
    {
        if (bShow)
            popup.AppendMenuIcon(POPUPCOMMAND_USEYOURBLOCK, IDS_VIEWCONTEXTMENU_USETHISBLOCK);
        popup.AppendMenuIcon(POPUPCOMMAND_USEYOURFILE, IDS_VIEWCONTEXTMENU_USETHISFILE);
        if (bShow)
        {
            popup.AppendMenuIcon(POPUPCOMMAND_USEYOURANDTHEIRBLOCK, IDS_VIEWCONTEXTMENU_USEYOURANDTHEIRBLOCK);
            popup.AppendMenuIcon(POPUPCOMMAND_USETHEIRANDYOURBLOCK, IDS_VIEWCONTEXTMENU_USETHEIRANDYOURBLOCK);
        }
    }
    else
    {
        if (bShow)
        {
            popup.AppendMenuIcon(POPUPCOMMAND_USELEFTBLOCK, IDS_VIEWCONTEXTMENU_USEOTHERBLOCK);
            popup.AppendMenuIcon(POPUPCOMMAND_USEBOTHRIGHTFIRST, IDS_VIEWCONTEXTMENU_USEBOTHTHISFIRST);
            popup.AppendMenuIcon(POPUPCOMMAND_USEBOTHLEFTFIRST, IDS_VIEWCONTEXTMENU_USEBOTHTHISLAST);
            if (IsLeftViewGood() && !m_pwndLeft->IsReadonly())
            {
                popup.AppendMenu(MF_SEPARATOR, NULL);
                popup.AppendMenuIcon(POPUPCOMMAND_PREPENDFROMRIGHT, IDS_VIEWCONTEXTMENU_PREPENDTHIS);
                popup.AppendMenuIcon(POPUPCOMMAND_REPLACEBYRIGHT, IDS_VIEWCONTEXTMENU_USETHIS);
                popup.AppendMenuIcon(POPUPCOMMAND_APPENDFROMRIGHT, IDS_VIEWCONTEXTMENU_APPENDTHIS);
            }
            popup.AppendMenu(MF_SEPARATOR, NULL);
        }
        popup.AppendMenuIcon(POPUPCOMMAND_USELEFTFILE, IDS_VIEWCONTEXTMENU_USEOTHERFILE);
        if (IsLeftViewGood() && !m_pwndLeft->IsReadonly())
        {
            popup.AppendMenuIcon(POPUPCOMMAND_USERIGHTFILE, IDS_VIEWCONTEXTMENU_USETHISFILE);
        }
    }

    CBaseView::AddContextItems(popup, state);
}


void CRightView::UseBlock(int nFirstViewLine, int nLastViewLine)
{
    if (!IsLeftViewGood())
        return;
    if (!IsWritable())
        return;
    CUndo::GetInstance().BeginGrouping();

    for (int viewLine = nFirstViewLine; viewLine <= nLastViewLine; viewLine++)
    {
        viewdata line = m_pwndLeft->GetViewData(viewLine);
        if ((line.ending != EOL_NOENDING)||(viewLine < (GetViewCount()-1)))
            line.ending = lineendings;
        switch (line.state)
        {
        case DIFFSTATE_CONFLICTEMPTY:
        case DIFFSTATE_UNKNOWN:
            line.state = DIFFSTATE_EMPTY;
        case DIFFSTATE_EMPTY:
            break;
        case DIFFSTATE_ADDED:
        case DIFFSTATE_MOVED_TO:
        case DIFFSTATE_CONFLICTADDED:
        case DIFFSTATE_CONFLICTED:
        case DIFFSTATE_CONFLICTED_IGNORED:
        case DIFFSTATE_IDENTICALADDED:
        case DIFFSTATE_THEIRSADDED:
        case DIFFSTATE_YOURSADDED:
        case DIFFSTATE_MOVED_FROM:
        case DIFFSTATE_IDENTICALREMOVED:
        case DIFFSTATE_REMOVED:
        case DIFFSTATE_THEIRSREMOVED:
        case DIFFSTATE_YOURSREMOVED:
            m_pwndLeft->SetViewState(viewLine, DIFFSTATE_NORMAL);
            line.state = DIFFSTATE_NORMAL;
        case DIFFSTATE_NORMAL:
            break;
        default:
            break;
        }
        SetViewData(viewLine, line);
    }
    // make sure previous (non empty) line have EOL set
    for (int nCheckViewLine = nFirstViewLine-1; nCheckViewLine > 0; nCheckViewLine--)
    {
        if (!IsViewLineEmpty(nCheckViewLine))
        {
            if (GetViewLineEnding(nCheckViewLine) == EOL_NOENDING)
            {
                SetViewLineEnding(nCheckViewLine, lineendings);
            }
            break;
        }
    }
    SaveUndoStep();

    int nRemovedLines = CleanEmptyLines();
    SaveUndoStep();
    UpdateViewLineNumbers();
    SaveUndoStep();

    CUndo::GetInstance().EndGrouping();

    if (nRemovedLines)
    {
        // some lines are gone update selection
        ClearSelection();
        SetupAllViewSelection(nFirstViewLine, nLastViewLine - nRemovedLines);
    }
    BuildAllScreen2ViewVector();
    m_pwndLeft->Invalidate();
    SetModified();
    RefreshViews();
}
