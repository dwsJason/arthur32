/*
	bctypes.h

	Because I need Standard Types

	NOTE: unlike the original omf2hex bctypes.h, the 32-bit (and other) types are
	backed by <cstdint> rather than `long`.  `long` is 64-bit on macOS/Linux (LP64)
	and 32-bit on Win64 (LLP64); since this tool reads fixed-width little-endian
	fields with sizeof(T), a `long`-based u32 would read 8 bytes and desync the
	parser on macOS/Linux.  The type *names* and usage are identical to the
	template - only the underlying typedefs are made fixed-width.
*/

#ifndef _bctypes_h
#define _bctypes_h

#include <cstdint>

typedef	int8_t				i8;
typedef uint8_t				u8;
typedef int16_t				i16;
typedef uint16_t			u16;
typedef int32_t				i32;
typedef uint32_t			u32;

typedef int64_t				i64;
typedef uint64_t			u64;

// If we're using C, I still like having a bool around
#ifndef __cplusplus
typedef	i32					bool;
#define false (0)
#define true (!false)
#define nullptr 0
#endif

typedef float				f32;
typedef float				r32;
typedef double				f64;
typedef double				r64;


#define null (0)

// Odd Types
typedef union {
//  u128       ul128;
  u64        ul64[2];
  u32        ui32[4];
} QWdata;


#endif // _bctypes_h

// EOF - bctypes.h
