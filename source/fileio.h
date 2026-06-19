//
// fileio.h
//
// Tiny cross-platform whole-file load/save helpers.  Binary mode, manual memory
// (caller owns the returned buffer), no MSVC-only APIs - builds on Win64, Linux
// and macOS.
//

#ifndef FILEIO_H_
#define FILEIO_H_

#include "bctypes.h"
#include <cstddef>

// Load an entire file into a heap buffer (binary mode).
// Returns a new[]-allocated buffer the caller must delete[], and sets outSize.
// Returns nullptr (and outSize 0) on failure.
u8* LoadFile(const char* pPath, size_t& outSize);

// Write a buffer to a file (binary mode).  Returns true on success.
bool SaveFile(const char* pPath, const u8* pData, size_t size);

// Get a file's size in bytes without reading its contents (binary mode).
// Returns true and sets outSize on success; false (outSize 0) if it cannot be
// opened.  Used by --dry-run to report sizes without loading large files.
bool FileSize(const char* pPath, size_t& outSize);

#endif // FILEIO_H_
