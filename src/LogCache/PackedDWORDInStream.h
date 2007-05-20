// TortoiseSVN - a Windows shell extension for easy version control

// Copyright (C) 2007-2007 - TortoiseSVN

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

///////////////////////////////////////////////////////////////
// include base class
///////////////////////////////////////////////////////////////

#include "BinaryInStream.h"

///////////////////////////////////////////////////////////////
//
// CPackedDWORDInStreamBase
//
//		Base class for all read streams containing packed 
//		integer data. See CPackedDWORDOutStreamBase for datails 
//		of the storage format.
//
///////////////////////////////////////////////////////////////

class CPackedDWORDInStreamBase : public CBinaryInStreamBase
{
protected:

	DWORD lastValue;
	DWORD count;

	// not ment to be instantiated

	// construction: nothing to do here

	CPackedDWORDInStreamBase ( CCacheFileInBuffer* buffer
						     , STREAM_INDEX index);

	// data access

	DWORD InternalGetValue();
	DWORD GetValue();

public:

	// destruction

	virtual ~CPackedDWORDInStreamBase() {};

	// plain data access

	size_t GetSizeValue();
};

inline DWORD CPackedDWORDInStreamBase::GetValue()
{
	while (true)
	{
		if (count != 0)
		{
			--count;
			return lastValue;
		}
		else
		{
			DWORD result = InternalGetValue();
			if (result != 0)
				return result-1;

			count = InternalGetValue();
			lastValue = InternalGetValue();
		}
	}
}

// plain data access

inline size_t CPackedDWORDInStreamBase::GetSizeValue()
{
	return static_cast<size_t>(InternalGetValue());
}

///////////////////////////////////////////////////////////////
//
// CPackedDWORDInStream
//
//		instantiable sub-class of CPackedDWORDInStreamBase.
//
///////////////////////////////////////////////////////////////

class CPackedDWORDInStream 
	: public CInStreamImplBase< CPackedDWORDInStream
							  , CPackedDWORDInStreamBase
							  , PACKED_DWORD_STREAM_TYPE_ID>
{
public:

	typedef CInStreamImplBase< CPackedDWORDInStream
							 , CPackedDWORDInStreamBase
							 , PACKED_DWORD_STREAM_TYPE_ID> TBase;

	typedef DWORD value_type;

	// construction / destruction: nothing special to do

	CPackedDWORDInStream ( CCacheFileInBuffer* buffer
					     , STREAM_INDEX index);
	virtual ~CPackedDWORDInStream() {};

	// public data access methods

	using TBase::GetValue;
};

///////////////////////////////////////////////////////////////
//
// operator>> 
//
//		for CPackedDWORDInStreamBase derived streams and vectors.
//
///////////////////////////////////////////////////////////////

template<class S, class V>
S& operator>> (S& stream, std::vector<V>& data)
{
	typedef typename std::vector<V>::iterator IT;

	size_t count = stream.GetSizeValue();

	data.clear();
	data.resize (count);
	for (IT iter = data.begin(), end = data.end(); iter != end; ++iter)
		*iter = (V)stream.GetValue();

	return stream;
}
