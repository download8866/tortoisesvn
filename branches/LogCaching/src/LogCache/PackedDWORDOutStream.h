#pragma once

///////////////////////////////////////////////////////////////
// include base class
///////////////////////////////////////////////////////////////

#include "BinaryOutStream.h"

///////////////////////////////////////////////////////////////
//
// CPackedDWORDOutStreamBase
//
//		Base class for write streams that store DWORD-sized
//		data in a space efficient format. CBinaryOutStreamBase
//		is used as a base class for stream data handling.
//
//		DWORDs are stored as follows:
//
//		* a sequence of 1 .. 5 bytes in 7/8 codes
//		  (i.e. 7 data bits per byte)
//		* the highest bit is used to indicate whether this
//		  is the last byte of the value (0) or if more bytes
//		  follow (1)
//		* the data is stored in little-endian order
//		  (i.e the first byte contains bits 0..6, the next
//		   byte 7 .. 13 and so on)
//
//		However, the incomming data gets run-length encoded
//		before being written to the stream. This is because 
//		most integer streams have larger sections of constant
//		values (saves approx. 10% of total log cache size).
//
//		Encoding scheme:
//
//		* 0 indicates a packed value. It is followed by the
//		  the number of repititions followed by the value itself
//		* an compressed value is stored as value+1
//		* initial lastValue is 0
//
///////////////////////////////////////////////////////////////

class CPackedDWORDOutStreamBase : public CBinaryOutStreamBase
{
protected:

	// differential storage:
	// the last value we received through Add() and the
	// how many times we already got that value in a row

	DWORD lastValue;
	DWORD count;

	// add data to the stream

	void InternalAdd (DWORD value);
	void FlushLastValue();

	// compression (RLE) support

	void Add (DWORD value);
	void WriteThisStream (CCacheFileOutBuffer* buffer);

public:

	// construction / destruction: nothing special to do

	CPackedDWORDOutStreamBase ( CCacheFileOutBuffer* aBuffer
						      , SUB_STREAM_ID anID);
	virtual ~CPackedDWORDOutStreamBase() {};
};

///////////////////////////////////////////////////////////////
// compress incomming data and write it to the stream
///////////////////////////////////////////////////////////////

inline void CPackedDWORDOutStreamBase::Add (DWORD value)
{
	if (value == lastValue)
	{
		++count;
	}
	else
	{
		if (count == 1)
		{
			InternalAdd (lastValue+1);
		}
		else if ((count == 2) && (lastValue < 0x80))
		{
			InternalAdd (lastValue+1);
			InternalAdd (lastValue+1);
			count = 1;
		}
		else
		{
			FlushLastValue();
			count = 1;
		}

		lastValue = value;
	}
}

///////////////////////////////////////////////////////////////
//
// CPackedDWORDOutStream
//
//		instantiable sub-class of CPackedDWORDOutStreamBase.
//
///////////////////////////////////////////////////////////////

class CPackedDWORDOutStream 
	: public COutStreamImplBase< CPackedDWORDOutStream
							   , CPackedDWORDOutStreamBase
		                       , PACKED_DWORD_STREAM_TYPE_ID>
{
public:

	typedef COutStreamImplBase< CPackedDWORDOutStream
							  , CPackedDWORDOutStreamBase
							  , PACKED_DWORD_STREAM_TYPE_ID> TBase;

	typedef DWORD value_type;

	// construction / destruction: nothing special to do

	CPackedDWORDOutStream ( CCacheFileOutBuffer* aBuffer
						  , SUB_STREAM_ID anID);
	virtual ~CPackedDWORDOutStream() {};

	// public Add() methods

	using TBase::Add;
};

///////////////////////////////////////////////////////////////
//
// operator<< 
//
//		for CPackedDWORDOutStreamBase derived streams and vectors.
//
///////////////////////////////////////////////////////////////

template<class S, class V>
S& operator<< (S& stream, const std::vector<V>& data)
{
	typedef typename std::vector<V>::const_iterator IT;

	// write total entry count and entries

	stream.Add ((typename S::value_type)data.size());
	for (IT iter = data.begin(), end = data.end(); iter != end; ++iter)
		stream.Add ((typename S::value_type)(*iter));

	// just close the stream 
	// (i.e. flush to disk and empty internal buffers)

	stream.AutoClose();

	return stream;
}
