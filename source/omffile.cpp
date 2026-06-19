//
// omffile.cpp
//

#include "omffile.h"
#include "fileio.h"
#include "memstream.h"
#include "memwriter.h"

#include <stdio.h>
#include <string>

//------------------------------------------------------------------------------
// Directory portion of a path (including the trailing separator), or "" if none.
static std::string DirOf( const std::string& path )
{
	size_t slash = path.find_last_of( "/\\" );
	if (std::string::npos == slash)
	{
		return std::string();
	}
	return path.substr( 0, slash + 1 );
}

// Normalize an authored data path - accept '/', '\\' and ':' as directory
// separators - then resolve it relative to the input .s16's directory unless it
// is absolute.
static std::string ResolveDataPath( const std::string& raw, const std::string& baseDir )
{
	std::string p = raw;
	for (char& c : p)
	{
		if ('\\' == c || ':' == c)
		{
			c = '/';
		}
	}

	bool bAbsolute = (!p.empty() && '/' == p[0]);
	if (bAbsolute)
	{
		return p;
	}
	return baseDir + p;
}

//------------------------------------------------------------------------------
OMFFile::OMFFile( const char* pPath, bool bVerbose )
	: m_bVerbose( bVerbose )
	, m_bValid( false )
{
	size_t length = 0;
	u8* pData = LoadFile( pPath, length );

	if (nullptr == pData)
	{
		printf( "ERROR: could not open '%s'\n", pPath );
		return;
	}

	MemoryStream stream( pData, length );

	// Walk the file: each segment's size is its BYTECNT at offset $00.
	while (stream.NumBytesAvailable() >= OMF_FIXED_HEADER_SIZE)
	{
		u8* pSegStart = stream.GetPointer();

		u32 segSize = stream.Read<u32>();                 // peek BYTECNT
		stream.SeekCurrent( -(int)sizeof( u32 ) );        // rewind

		if (segSize < OMF_FIXED_HEADER_SIZE || segSize > stream.NumBytesAvailable())
		{
			printf( "ERROR: bad/truncated segment (size %u, %zu available)\n",
				(unsigned)segSize, stream.NumBytesAvailable() );
			delete[] pData;
			return;
		}

		OMFSegment seg( MemoryStream( pSegStart, segSize ) );

		if (!seg.m_bValid)
		{
			printf( "ERROR: segment %u has invalid header displacements\n",
				(unsigned)seg.m_segNum );
			delete[] pData;
			return;
		}

		// Guard: only OMF v2 is supported.  In v0/v1 the $00 field is a 512-byte
		// BLOCK count, which would wreck this byte-stride walk.
		if (2 != seg.m_version)
		{
			printf( "ERROR: segment %u is OMF version %u; only v2 is supported\n",
				(unsigned)seg.m_segNum, (unsigned)seg.m_version );
			delete[] pData;
			return;
		}

		m_segments.push_back( seg );
		stream.SeekCurrent( (int)segSize );
	}

	delete[] pData;
	m_bValid = true;

	if (m_bVerbose)
	{
		Dump();
	}
}

//------------------------------------------------------------------------------
void OMFFile::Dump() const
{
	printf( "OMF file: %zu segment(s)\n", m_segments.size() );
	for (const OMFSegment& seg : m_segments)
	{
		seg.Dump();
	}
}

//------------------------------------------------------------------------------
int OMFFile::InjectBigData( const char* pInputPath )
{
	std::string baseDir = DirOf( pInputPath );
	int injected = 0;

	for (OMFSegment& seg : m_segments)
	{
		if (!seg.IsBigData())
		{
			continue;
		}

		// The placeholder body is an LCONST holding the pathname:
		//   $F2  <count:u32>  <count path bytes>  [END]
		const std::vector<u8>& body = seg.m_body;
		if (body.size() < 5 || 0xF2 != body[0])
		{
			printf( "ERROR: bigdata segment %u ('%s') body is not an LCONST pathname\n",
				(unsigned)seg.m_segNum, seg.m_segName.c_str() );
			return -1;
		}

		u32 count = (u32)body[1] | ((u32)body[2] << 8) | ((u32)body[3] << 16) | ((u32)body[4] << 24);
		if ((size_t)5 + count > body.size())
		{
			printf( "ERROR: bigdata segment %u ('%s') LCONST length exceeds its body\n",
				(unsigned)seg.m_segNum, seg.m_segName.c_str() );
			return -1;
		}

		std::string rawPath( (const char*)&body[5], count );
		// Trim trailing NUL / whitespace (tolerant of ASC vs null-terminated).
		while (!rawPath.empty())
		{
			char c = rawPath.back();
			if (0 == c || ' ' == c || '\t' == c || '\r' == c || '\n' == c)
			{
				rawPath.pop_back();
			}
			else
			{
				break;
			}
		}

		std::string fullPath = ResolveDataPath( rawPath, baseDir );

		size_t dataSize = 0;
		u8* pData = LoadFile( fullPath.c_str(), dataSize );
		if (nullptr == pData)
		{
			printf( "ERROR: bigdata segment %u ('%s'): cannot open data file '%s'\n",
				(unsigned)seg.m_segNum, seg.m_segName.c_str(), fullPath.c_str() );
			return -1;
		}

		// Build the replacement body: a single LCONST of the file, then END.
		// One LCONST carries the whole file (its count is 32-bit, so >64K is fine).
		MemoryWriter bodyOut;
		bodyOut.Write<u8>( 0xF2 );
		bodyOut.Write<u32>( (u32)dataSize );
		bodyOut.WriteBytes( pData, dataSize );
		bodyOut.Write<u8>( 0x00 );   // END
		delete[] pData;

		seg.m_body.assign( bodyOut.Data(), bodyOut.Data() + bodyOut.Size() );

		// Header edits: PRESERVE KIND (type + DYNAMIC); bank-align the start; clear
		// BANKSIZE so the segment may exceed 64K / cross banks; LENGTH = data size.
		seg.m_align    = 0x00010000;
		seg.m_bankSize = 0;
		seg.m_resSpc   = 0;
		seg.m_length   = (u32)dataSize;
		seg.m_bModified = true;

		++injected;

		if (m_bVerbose)
		{
			printf( "Injected '%s' (%zu bytes) -> segment %u '%s'\n",
				fullPath.c_str(), dataSize, (unsigned)seg.m_segNum, seg.m_segName.c_str() );
		}
	}

	return injected;
}

//------------------------------------------------------------------------------
// Read a little-endian u32 from a byte vector at the given offset.
static u32 ReadU32( const std::vector<u8>& b, size_t off )
{
	return (u32)b[off] | ((u32)b[off + 1] << 8) | ((u32)b[off + 2] << 16) | ((u32)b[off + 3] << 24);
}

//------------------------------------------------------------------------------
// Regenerate the ~ExpressLoad directory after the real segments have been
// injected / name-trimmed.  The directory caches, per segment, ABSOLUTE file
// offsets to that segment's data and relocation bytes, the data/reloc lengths,
// and a copy of the segment's header from $0C on.  We rebuild it from scratch:
// re-using merlin32's own data/reloc lengths (read from the ORIGINAL directory)
// for untouched segments, fresh values for injected ones, and recomputing every
// offset and header copy from the final layout.
//
// Directory body layout (after the leading LCONST $F2 + u32 length):
//   u32 reserved
//   u16 (segment count - 1)
//   N * { u16 relOffset, u16 reserved, u32 reserved }     (Header Entry Table)
//   N * { u16 segNum }                                    (Segnum Conversion)
//   N * { u32 dataOff, u32 dataLen, u32 relocOff, u32 relocLen, header-copy }
//   u8  END
bool OMFFile::RebuildExpressLoad()
{
	// Locate the ExpressLoad index segment; collect the real segments in order.
	int xpressIdx = -1;
	for (size_t i = 0; i < m_segments.size(); ++i)
	{
		if (m_segments[i].m_segName == "~ExpressLoad" || 0x8001 == m_segments[i].m_kind)
		{
			xpressIdx = (int)i;
			break;
		}
	}
	if (xpressIdx < 0)
	{
		return true;   // not an ExpressLoad file
	}

	OMFSegment& xpress = m_segments[xpressIdx];

	std::vector<OMFSegment*> reals;
	for (size_t i = 0; i < m_segments.size(); ++i)
	{
		if ((int)i != xpressIdx)
		{
			reals.push_back( &m_segments[i] );
		}
	}
	const size_t N = reals.size();

	// --- New header bytes / lengths / byte counts for every real segment.
	//     AppendRebuiltHeader is faithful, so for an untouched segment this
	//     reproduces its original header exactly.
	std::vector<std::vector<u8> > newHeader( N );
	std::vector<u32> newHeaderLen( N ), newByteCnt( N );
	for (size_t i = 0; i < N; ++i)
	{
		MemoryWriter hw;
		size_t dummy = 0;
		reals[i]->AppendRebuiltHeader( hw, dummy );
		newHeader[i].assign( hw.Data(), hw.Data() + hw.Size() );
		newHeaderLen[i] = (u32)hw.Size();
		newByteCnt[i]   = newHeaderLen[i] + (u32)reals[i]->m_body.size();
	}

	// --- Parse the ORIGINAL directory for each segment's data/reloc offset+length.
	std::vector<u32> origDataOff( N, 0 ), origDataLen( N, 0 );
	std::vector<u32> origRelocOff( N, 0 ), origRelocLen( N, 0 );
	{
		const std::vector<u8>& b = xpress.m_body;
		if (b.size() < 11 || 0xF2 != b[0])
		{
			printf( "ERROR: malformed ~ExpressLoad segment body\n" );
			return false;
		}
		size_t p = 5;          // skip LCONST $F2 + u32 length
		p += 4;                // reserved
		p += 2;                // segment-count word
		p += N * 8;            // Header Entry Table
		p += N * 2;            // Segnum Conversion Table
		for (size_t i = 0; i < N; ++i)
		{
			if (p + 16 > b.size())
			{
				printf( "ERROR: ~ExpressLoad header table is truncated\n" );
				return false;
			}
			origDataOff[i]  = ReadU32( b, p + 0 );
			origDataLen[i]  = ReadU32( b, p + 4 );
			origRelocOff[i] = ReadU32( b, p + 8 );
			origRelocLen[i] = ReadU32( b, p + 12 );
			p += 16;
			p += (u32)reals[i]->m_dispData - 12;   // skip the original header copy
		}
	}

	// --- Original segment bases (ExpressLoad is first), to recover the
	//     body-relative position of each segment's data / reloc bytes.
	std::vector<u32> origBase( N, 0 );
	{
		u32 base = (u32)xpress.m_rawSegment.size();
		for (size_t i = 0; i < N; ++i)
		{
			origBase[i] = base;
			base += (u32)reals[i]->m_rawSegment.size();
		}
	}

	// --- Per-segment data/reloc length and body-relative offsets.
	std::vector<u32> dataLen( N ), relocLen( N ), dataRel( N ), relocRel( N );
	for (size_t i = 0; i < N; ++i)
	{
		if (reals[i]->IsBigData())
		{
			// Injected: body is a single LCONST(data) + END, no relocations.
			const std::vector<u8>& body = reals[i]->m_body;
			u32 lcount = (body.size() >= 5 && 0xF2 == body[0]) ? ReadU32( body, 1 ) : 0;
			dataLen[i]  = lcount;
			dataRel[i]  = 5;     // after $F2 + u32 count
			relocLen[i] = 0;
			relocRel[i] = 0;
		}
		else
		{
			// Untouched body: re-use merlin32's lengths; recover body-relative
			// offsets (which survive a header-size change, since the body moves
			// only by the header delta).
			u32 origHdr = (u32)reals[i]->m_dispData;
			dataLen[i]  = origDataLen[i];
			relocLen[i] = origRelocLen[i];
			dataRel[i]  = origDataOff[i]  - origBase[i] - origHdr;
			relocRel[i] = origRelocOff[i] - origBase[i] - origHdr;
		}
	}

	// --- ExpressLoad's own header length and the new directory / body sizes.
	u32 xpressHeaderLen = xpress.RebuiltHeaderLength();

	u32 bodyLConstLen = 4 + 2 + (u32)(N * 8) + (u32)(N * 2);
	for (size_t i = 0; i < N; ++i)
	{
		bodyLConstLen += 16 + (newHeaderLen[i] - 12);
	}
	u32 xpressBodyLen = 1 + 4 + bodyLConstLen + 1;     // $F2 + u32 len + dir + END
	u32 xpressTotal   = xpressHeaderLen + xpressBodyLen;

	// --- Final file bases for every real segment.
	std::vector<u32> newBase( N, 0 );
	{
		u32 base = xpressTotal;
		for (size_t i = 0; i < N; ++i)
		{
			newBase[i] = base;
			base += newByteCnt[i];
		}
	}

	// --- Header Entry Table relative-offset pointers (body offsets).
	//     The table starts at body offset 11; each entry is 8 bytes; the Segment
	//     Header Table follows the segnum table.
	u32 headerTableStart = 11 + (u32)(N * 8) + (u32)(N * 2);
	std::vector<u32> entryPos( N );
	{
		u32 pos = headerTableStart;
		for (size_t i = 0; i < N; ++i)
		{
			entryPos[i] = pos;
			pos += 16 + (newHeaderLen[i] - 12);
		}
	}

	// --- Emit the directory.
	MemoryWriter dir;
	dir.Write<u8>( 0xF2 );
	dir.Write<u32>( bodyLConstLen );
	dir.Write<u32>( 0 );                 // reserved
	dir.Write<u16>( (u16)(N - 1) );      // segment count - 1

	for (size_t i = 0; i < N; ++i)       // Header Entry Table
	{
		dir.Write<u16>( (u16)(entryPos[i] - (11 + i * 8)) );
		dir.Write<u16>( 0 );
		dir.Write<u32>( 0 );
	}
	for (size_t i = 0; i < N; ++i)       // Segnum Conversion Table
	{
		dir.Write<u16>( reals[i]->m_segNum );
	}
	for (size_t i = 0; i < N; ++i)       // Segment Header Table
	{
		dir.Write<u32>( newBase[i] + newHeaderLen[i] + dataRel[i] );   // data offset
		dir.Write<u32>( dataLen[i] );
		dir.Write<u32>( newBase[i] + newHeaderLen[i] + relocRel[i] );  // reloc offset
		dir.Write<u32>( relocLen[i] );
		dir.WriteBytes( newHeader[i].data() + 12, newHeaderLen[i] - 12 );
	}
	dir.Write<u8>( 0x00 );               // END

	// Replace the ExpressLoad body and mark it for rebuild.
	xpress.m_body.assign( dir.Data(), dir.Data() + dir.Size() );
	xpress.m_length    = bodyLConstLen;
	xpress.m_bModified = true;

	return true;
}

//------------------------------------------------------------------------------
bool OMFFile::HasExpressLoad() const
{
	for (const OMFSegment& seg : m_segments)
	{
		// The ExpressLoad index segment is named "~ExpressLoad" (KIND $8001).
		if (seg.m_segName == "~ExpressLoad" || 0x8001 == seg.m_kind)
		{
			return true;
		}
	}
	return false;
}

//------------------------------------------------------------------------------
bool OMFFile::AnyModified() const
{
	for (const OMFSegment& seg : m_segments)
	{
		if (seg.m_bModified)
		{
			return true;
		}
	}
	return false;
}

//------------------------------------------------------------------------------
void OMFFile::TrimNames()
{
	for (OMFSegment& seg : m_segments)
	{
		// Only fixed-width names need converting; LABLEN==0 is already minimal.
		// LOADNAME is left untouched (the format fixes it at 10 bytes).
		if (0 != seg.m_labLen)
		{
			seg.m_labLen = 0;       // store SEGNAME as a length-prefixed string
			seg.m_bModified = true; // force the serializer to rebuild this header
		}
	}
}

//------------------------------------------------------------------------------
bool OMFFile::Write( const char* pPath ) const
{
	MemoryWriter out;

	for (const OMFSegment& seg : m_segments)
	{
		seg.Serialize( out );
	}

	return SaveFile( pPath, out.Data(), out.Size() );
}
