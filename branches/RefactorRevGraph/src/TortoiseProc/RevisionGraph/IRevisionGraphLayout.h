// TortoiseSVN - a Windows shell extension for easy version control

// Copyright (C) 2003-2008 - TortoiseSVN

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

#include "LogCacheGlobals.h"

using namespace LogCache;

class ILayoutItemList
{
public:

    /// standard data access

    virtual index_t GetCount() const = 0;

    virtual CString GetToolTip (index_t index) const = 0;

    /// lookup (return NO_INDEX if not found)

    virtual index_t GetFirstVisible (const CRect& viewRect) const = 0;
    virtual index_t GetNextVisible (index_t prev, const CRect& viewRect) const = 0;
    virtual index_t GetAt (const CPoint& point, long delta) const = 0;
};

class ILayoutConnectionList : public ILayoutItemList
{
public:

    /// Connections are Bezier lines.

    struct SConnection
    {
        /// stype (usually pen) index. 
        /// To be interpreted by drawing code. Starts with 0.

        index_t style;

        /// number of Bezier points valid in @ref points.

        index_t numberOfPoints;

        /// Bezier points to draw the curve for.

        const CPoint* points;
    };

    /// standard data access

    virtual SConnection GetConnection (index_t index) const = 0;
};

class ILayoutNodeList : public ILayoutItemList
{
public:

    /// Nodes occupy a rectangular area.

    struct SNode
    {
        /// style (shape, border, filling) index. 
        /// To be interpreted by drawing code. Starts with 0.

        index_t style;

        /// extended style info (usually presense of sub-structures). 
        /// To be interpreted by drawing code. Starts with 0.

        DWORD styleFlags;

        /// Area occupied by this node.

        CRect rect;
    };

    /// standard data access

    virtual SNode GetNode (index_t index) const = 0;
};

class ILayoutTextList : public ILayoutItemList
{
public:

    /// Texts occupy a rectangular area.

    struct SText
    {
        /// style (shape, font, size) index. 
        /// To be interpreted by drawing code. Starts with 0.

        index_t style;

        /// style info (bold, etc.).
        /// To be interpreted by drawing code. Starts with 0.

        long rotation;

        /// The text.

        CString text;

        /// Area occupied by this text.

        CRect rect;
    };

    /// standard data access

    virtual SText GetText (index_t index) const = 0;
};

/** The 'graph' layout contains of three collections:
* One for the nodes, one for the connecting lines and
* one for the texts to be shown.
* 
* The whole graph uses a discrete coordinate system
* confined in a rect.
*/

class IRevisionGraphLayout
{
public:

    /// total graph size (logical units)

    virtual CRect GetRect() const = 0;

    /// access to the sub-structures

    virtual const ILayoutNodeList* GetNodes() const = 0;
    virtual const ILayoutConnectionList* GetConnections() const = 0;
    virtual const ILayoutTextList* GetTexts() const = 0;
};
