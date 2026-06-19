//
// fileio.cpp
//

#include "fileio.h"

#include <stdio.h>

//------------------------------------------------------------------------------
u8* LoadFile(const char* pPath, size_t& outSize)
{
	outSize = 0;

	FILE* pFile = fopen(pPath, "rb");
	if (nullptr == pFile)
	{
		return nullptr;
	}

	fseek(pFile, 0, SEEK_END);
	long length = ftell(pFile);
	fseek(pFile, 0, SEEK_SET);

	if (length < 0)
	{
		fclose(pFile);
		return nullptr;
	}

	u8* pData = new u8[ (size_t)length ];

	size_t numRead = fread(pData, sizeof(u8), (size_t)length, pFile);
	fclose(pFile);

	if (numRead != (size_t)length)
	{
		delete[] pData;
		return nullptr;
	}

	outSize = (size_t)length;
	return pData;
}

//------------------------------------------------------------------------------
bool SaveFile(const char* pPath, const u8* pData, size_t size)
{
	FILE* pFile = fopen(pPath, "wb");
	if (nullptr == pFile)
	{
		return false;
	}

	size_t numWritten = fwrite(pData, sizeof(u8), size, pFile);
	fclose(pFile);

	return (numWritten == size);
}

//------------------------------------------------------------------------------
bool FileSize(const char* pPath, size_t& outSize)
{
	outSize = 0;

	FILE* pFile = fopen(pPath, "rb");
	if (nullptr == pFile)
	{
		return false;
	}

	fseek(pFile, 0, SEEK_END);
	long length = ftell(pFile);
	fclose(pFile);

	if (length < 0)
	{
		return false;
	}

	outSize = (size_t)length;
	return true;
}
