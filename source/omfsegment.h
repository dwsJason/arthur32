//
// omfsegment.h
//
// One OMF (Object Module Format) segment from an Apple IIgs load file.
//
// The header fields are parsed individually (so we can edit them); the segment
// BODY is kept as an opaque blob and copied verbatim - we never need to interpret
// relocation / SUPER records.  The original raw bytes are also retained so an
// unmodified segment round-trips byte-for-byte.
//
// Verified header layout (little-endian), reconciled from merlin32 a65816_OMF.c
// and OMFAnalyzer OMF_Load.c:
//
//   $00 BYTECNT(4) $04 RESSPC(4) $08 LENGTH(4) $0C undef(1) $0D LABLEN(1)
//   $0E NUMLEN(1)  $0F VERSION(1) $10 BANKSIZE(4) $14 KIND(2) $16 undef(2)
//   $18 ORG(4) $1C ALIGN(4) $20 NUMSEX(1) $21 undef(1) $22 SEGNUM(2)
//   $24 ENTRY(4) $28 DISPNAME(2) $2A DISPDATA(2)  -> fixed header = 44 bytes
//   [44, DISPNAME) TempOrg   DISPNAME: LOADNAME(10)   DISPNAME+10: SEGNAME(var)
//   DISPDATA: body
//

#ifndef OMFSEGMENT_H_
#define OMFSEGMENT_H_

#include "bctypes.h"
#include "memstream.h"
#include "memwriter.h"

#include <string>
#include <vector>

// Fixed portion of a v2 segment header, up to and including DISPDATA.
#define OMF_FIXED_HEADER_SIZE 0x2C  // 44

class OMFSegment
{
public:
	// Parse a single segment.  `stream` must be scoped to exactly this segment
	// (its size == this segment's BYTECNT).
	OMFSegment( MemoryStream stream );

	// Append this segment to the output.  If it has not been modified, the
	// original bytes are emitted verbatim; otherwise the header is rebuilt from
	// the fields (recomputing DISPNAME/DISPDATA/BYTECNT) and the body appended.
	void Serialize( MemoryWriter& out ) const;

	// Append just the rebuilt header (up to the body); returns the BYTECNT field
	// offset via byteCntOffset.  Used by Serialize and the ExpressLoad rebuild.
	void AppendRebuiltHeader( MemoryWriter& out, size_t& byteCntOffset ) const;

	// Rebuilt header length (== DISPDATA) and the SEGNAME field width.
	u16 RebuiltHeaderLength() const;
	u16 SegNameFieldLength() const;

	// Print a one-segment summary (verbose / sanity).
	void Dump() const;

	// True when LOADNAME is one of the injection-marker keywords
	// ("bigdata", "incbin", "bindata", "data"), case-insensitive.
	bool IsPlaceholder() const;

	// Header fields
	u32 m_byteCnt;     // $00
	u32 m_resSpc;      // $04
	u32 m_length;      // $08
	u8  m_undefined0;  // $0C  (reserved, merlin32 writes 0; LCBANK is m_undefined2 @ $21)
	u8  m_labLen;      // $0D
	u8  m_numLen;      // $0E
	u8  m_version;     // $0F
	u32 m_bankSize;    // $10
	u16 m_kind;        // $14
	u16 m_undefined1;  // $16
	u32 m_org;         // $18
	u32 m_align;       // $1C
	u8  m_numSex;      // $20
	u8  m_undefined2;  // $21
	u16 m_segNum;      // $22
	u32 m_entry;       // $24
	u16 m_dispName;    // $28
	u16 m_dispData;    // $2A

	std::string m_loadName;   // 10-byte field at $DISPNAME (trailing spaces trimmed)
	std::string m_segName;    // at $DISPNAME+10

	std::vector<u8> m_tempOrg;      // bytes [44, DISPNAME) - preserved verbatim
	std::vector<u8> m_body;         // bytes [DISPDATA, BYTECNT) - opaque

	std::vector<u8> m_rawSegment;   // the original bytes, for byte-identical round-trip
	bool            m_bModified;    // false -> emit m_rawSegment verbatim on Serialize
	bool            m_bValid;       // false -> header displacements failed validation
	bool            m_bInjected;    // true  -> body was replaced with file data (no relocs)
};

#endif // OMFSEGMENT_H_
