#include "StdAfx.h"
#include "StrictLogInterator.h"

// implement as no-op

bool CStrictLogInterator::HandleCopyAndDelete()
{
	return false;
}

// construction / destruction 
// (nothing special to do)

CStrictLogInterator::CStrictLogInterator ( const CCachedLogInfo* cachedLog
										 , size_t startRevision
										 , const CDictionaryBasedPath& startPath)
	: CLogIteratorBase (cachedLog, startRevision, startPath)
{
}

CStrictLogInterator::~CStrictLogInterator(void)
{
}

