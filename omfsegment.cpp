//
// omfsegment.cpp
//

#include "omfsegment.h"

#include <stdio.h>

//------------------------------------------------------------------------------
// Trim trailing spaces from a fixed-width name field.
static std::string TrimTrailingSpaces( const char* pChars, size_t len )
{
	while (len > 0 && ' ' == pChars[len - 1])
	{
		--len;
	}
	return std::string( pChars, len );
}

// Case-insensitive ASCII compare.
static bool EqualsIgnoreCase( const std::string& a, const char* b )
{
	size_t i = 0;
	for (; i < a.size(); ++i)
	{
		char ca = a[i];
		char cb = b[i];
		if (0 == cb)
		{
			return false;  // b is shorter
		}
		if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
		if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
		if (ca != cb)
		{
			return false;
		}
	}
	return 0 == b[i];  // both ended together
}

//------------------------------------------------------------------------------
OMFSegment::OMFSegment( MemoryStream stream )
	: m_bModified( false )
	, m_bValid( false )
{
	u8* pSegStart = stream.GetPointer();

	// Fixed header ($00 .. $2B) - read in order so the cursor lands at 44
	m_byteCnt    = stream.Read<u32>();   // $00
	m_resSpc     = stream.Read<u32>();   // $04
	m_length     = stream.Read<u32>();   // $08
	m_undefined0 = stream.Read<u8>();    // $0C
	m_labLen     = stream.Read<u8>();    // $0D
	m_numLen     = stream.Read<u8>();    // $0E
	m_version    = stream.Read<u8>();    // $0F
	m_bankSize   = stream.Read<u32>();   // $10
	m_kind       = stream.Read<u16>();   // $14
	m_undefined1 = stream.Read<u16>();   // $16
	m_org        = stream.Read<u32>();   // $18
	m_align      = stream.Read<u32>();   // $1C
	m_numSex     = stream.Read<u8>();    // $20
	m_undefined2 = stream.Read<u8>();    // $21
	m_segNum     = stream.Read<u16>();   // $22
	m_entry      = stream.Read<u32>();   // $24
	m_dispName   = stream.Read<u16>();   // $28
	m_dispData   = stream.Read<u16>();   // $2A

	// Capture the raw segment bytes for byte-identical round-tripping.
	m_rawSegment.assign( pSegStart, pSegStart + m_byteCnt );

	// Validate the displacements before any displacement-driven read.  Defends
	// against malformed / non-merlin32 input (a bad DISPDATA would otherwise
	// underflow the body length into a giant memcpy).  Well-formed files pass.
	{
		u32 nameStart = (u32)m_dispName + 10u;     // SEGNAME begins here

		if (m_dispName < OMF_FIXED_HEADER_SIZE) return;   // LOADNAME before fixed hdr
		if (nameStart > m_byteCnt)              return;   // LOADNAME must fit
		if (m_dispData < nameStart)             return;   // body after the name area
		if (m_dispData > m_byteCnt)             return;   // body within the segment

		// SEGNAME must also fit within the segment.
		u32 nameField;
		if (0 == m_labLen)
		{
			if (nameStart >= m_byteCnt) return;           // need the length byte
			nameField = 1u + m_rawSegment[nameStart];     // Pascal: len byte + chars
		}
		else
		{
			nameField = m_labLen;
		}
		if (nameStart + nameField > m_byteCnt) return;
	}
	m_bValid = true;

	// Optional TempOrg blob between the fixed header and LOADNAME.
	if (m_dispName > OMF_FIXED_HEADER_SIZE)
	{
		size_t tempLen = (size_t)m_dispName - OMF_FIXED_HEADER_SIZE;
		m_tempOrg.resize( tempLen );
		stream.ReadBytes( m_tempOrg.data(), tempLen );
	}

	// LOADNAME: fixed 10 bytes at DISPNAME (navigate dynamically, never hardcode 44).
	stream.SeekSet( m_dispName );
	{
		char loadChars[10];
		stream.ReadBytes( (u8*)loadChars, 10 );
		m_loadName = TrimTrailingSpaces( loadChars, 10 );
	}

	// SEGNAME at DISPNAME+10: fixed LABLEN bytes, or a 1-byte-length Pascal string.
	if (0 == m_labLen)
	{
		m_segName = stream.ReadPString();
	}
	else
	{
		std::vector<char> nameChars( m_labLen );
		stream.ReadBytes( (u8*)nameChars.data(), m_labLen );
		m_segName = TrimTrailingSpaces( nameChars.data(), m_labLen );
	}

	// Body: opaque bytes from DISPDATA to BYTECNT.
	stream.SeekSet( m_dispData );
	{
		size_t bodyLen = (size_t)m_byteCnt - m_dispData;
		m_body.resize( bodyLen );
		stream.ReadBytes( m_body.data(), bodyLen );
	}
}

//------------------------------------------------------------------------------
void OMFSegment::Serialize( MemoryWriter& out ) const
{
	// Unmodified -> emit the original bytes verbatim (guarantees byte-identity).
	if (!m_bModified)
	{
		out.WriteBytes( m_rawSegment.data(), m_rawSegment.size() );
		return;
	}

	// Rebuild the header from fields, then append the body, recomputing BYTECNT.
	size_t segStart = out.Size();
	size_t byteCntOffset = 0;
	AppendRebuiltHeader( out, byteCntOffset );

	out.WriteBytes( m_body.data(), m_body.size() );

	// Patch BYTECNT now that the full segment length is known.
	u32 byteCnt = (u32)(out.Size() - segStart);
	out.PatchU32( byteCntOffset, byteCnt );
}

//------------------------------------------------------------------------------
// SEGNAME field width as it will be written (Pascal: 1 + len; or fixed LABLEN).
u16 OMFSegment::SegNameFieldLength() const
{
	if (0 == m_labLen)
	{
		size_t segLen = m_segName.size();
		if (segLen > 255)
		{
			segLen = 255;
		}
		return (u16)(1 + segLen);
	}
	return (u16)m_labLen;
}

//------------------------------------------------------------------------------
// Rebuilt header length == DISPDATA == 44 + TempOrg + 10 (LOADNAME) + SEGNAME.
u16 OMFSegment::RebuiltHeaderLength() const
{
	return (u16)(OMF_FIXED_HEADER_SIZE + m_tempOrg.size() + 10 + SegNameFieldLength());
}

//------------------------------------------------------------------------------
// Append the rebuilt header (everything up to the body).  Returns the offset of
// the BYTECNT field (still a placeholder) via byteCntOffset for later patching.
void OMFSegment::AppendRebuiltHeader( MemoryWriter& out, size_t& byteCntOffset ) const
{
	byteCntOffset = out.ReserveU32();   // $00 BYTECNT - patched by the caller

	out.Write<u32>( m_resSpc );      // $04
	out.Write<u32>( m_length );      // $08
	out.Write<u8>( m_undefined0 );   // $0C
	out.Write<u8>( m_labLen );       // $0D
	out.Write<u8>( m_numLen );       // $0E
	out.Write<u8>( m_version );      // $0F
	out.Write<u32>( m_bankSize );    // $10
	out.Write<u16>( m_kind );        // $14
	out.Write<u16>( m_undefined1 );  // $16
	out.Write<u32>( m_org );         // $18
	out.Write<u32>( m_align );       // $1C
	out.Write<u8>( m_numSex );       // $20
	out.Write<u8>( m_undefined2 );   // $21
	out.Write<u16>( m_segNum );      // $22
	out.Write<u32>( m_entry );       // $24

	u16 dispName = (u16)(OMF_FIXED_HEADER_SIZE + m_tempOrg.size());
	u16 dispData = (u16)(dispName + 10 + SegNameFieldLength());

	out.Write<u16>( dispName );      // $28
	out.Write<u16>( dispData );      // $2A

	// TempOrg (verbatim), LOADNAME (fixed 10), SEGNAME.
	out.WriteBytes( m_tempOrg.data(), m_tempOrg.size() );
	out.WriteFixedName( m_loadName, 10 );

	if (0 == m_labLen)
	{
		out.WritePString( m_segName );
	}
	else
	{
		out.WriteFixedName( m_segName, m_labLen );
	}
}

//------------------------------------------------------------------------------
bool OMFSegment::IsBigData() const
{
	return EqualsIgnoreCase( m_loadName, "bigdata" );
}

//------------------------------------------------------------------------------
void OMFSegment::Dump() const
{
	printf( "  seg %u  '%s'  (load '%s')\n",
		(unsigned)m_segNum, m_segName.c_str(), m_loadName.c_str() );
	printf( "    kind=$%04X%s  type=$%02X  banksize=$%X  align=$%X  org=$%X\n",
		(unsigned)m_kind, (m_kind & 0x8000) ? " DYNAMIC" : "",
		(unsigned)(m_kind & 0x001F), (unsigned)m_bankSize,
		(unsigned)m_align, (unsigned)m_org );
	printf( "    bytecnt=%u  length=%u  body=%u  dispname=%u  dispdata=%u  ver=%u  lablen=%u%s\n",
		(unsigned)m_byteCnt, (unsigned)m_length, (unsigned)m_body.size(),
		(unsigned)m_dispName, (unsigned)m_dispData, (unsigned)m_version,
		(unsigned)m_labLen, IsBigData() ? "  [BIGDATA]" : "" );
}
