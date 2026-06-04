//
// memwriter.h
//
// Growable little-endian byte buffer - the write counterpart to the read-only
// MemoryStream.  Used to assemble OMF records (header, body, back-patched
// BYTECNT / SUPER record lengths, etc.) before flushing them to disk.
//
// Vanilla C++17, no external dependencies.  Builds on Win64, Linux and macOS.
// Output is explicitly little-endian: integers are emitted byte-by-byte via
// (value >> (8*i)) so the result is correct regardless of host endianness.
//

#ifndef MEMWRITER_H_
#define MEMWRITER_H_

#include "bctypes.h"

#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

//------------------------------------------------------------------------------

class MemoryWriter
{
public:
	MemoryWriter() = default;

	// Append sizeof(T) bytes of 'value' in little-endian order.
	// Written byte-by-byte so the output is correct on any host endianness.
	template<class T>
	void Write(T value)
	{
		typedef typename std::make_unsigned<T>::type UT;
		// Accumulate in at least an unsigned int so the shift count is always
		// less than the operand width (avoids UB / -Wshift-count-overflow when
		// T is a single byte).
		u64 bits = static_cast<u64>(static_cast<UT>(value));

		for (size_t idx = 0; idx < sizeof(T); ++idx)
		{
			m_buffer.push_back(static_cast<u8>((bits >> (8 * idx)) & 0xFF));
		}
	}

	// Append raw bytes.
	void WriteBytes(const u8* pSrc, size_t numBytes)
	{
		if (pSrc && numBytes)
		{
			m_buffer.insert(m_buffer.end(), pSrc, pSrc + numBytes);
		}
	}

	// Append a single byte.
	void WriteU8(u8 value)
	{
		m_buffer.push_back(value);
	}

	// Append an OMF Pascal string: 1 length byte (clamped to 255) then the chars.
	void WritePString(const std::string& s)
	{
		size_t len = s.size();
		if (len > 255)
		{
			len = 255;
		}

		m_buffer.push_back(static_cast<u8>(len));
		WriteBytes(reinterpret_cast<const u8*>(s.data()), len);
	}

	// Append exactly fieldLen bytes, space-padded (0x20), truncating if longer.
	// Used for the fixed-width 10-byte OMF LOADNAME field.
	void WriteFixedName(const std::string& s, size_t fieldLen)
	{
		size_t copyLen = s.size();
		if (copyLen > fieldLen)
		{
			copyLen = fieldLen;
		}

		WriteBytes(reinterpret_cast<const u8*>(s.data()), copyLen);

		for (size_t idx = copyLen; idx < fieldLen; ++idx)
		{
			m_buffer.push_back(0x20);
		}
	}

	// Reserve 4 placeholder bytes (for a value not yet known) and return the
	// offset where they were written, for a later PatchU32().
	size_t ReserveU32()
	{
		size_t offset = m_buffer.size();
		for (size_t idx = 0; idx < 4; ++idx)
		{
			m_buffer.push_back(0);
		}
		return offset;
	}

	// Overwrite the 4 bytes at 'offset' with 'value' little-endian.
	void PatchU32(size_t offset, u32 value)
	{
		// offset must reference 4 bytes already present in the buffer.
		for (size_t idx = 0; idx < 4; ++idx)
		{
			m_buffer[offset + idx] = static_cast<u8>(value & 0xFF);
			value >>= 8;
		}
	}

	// Current number of bytes in the buffer.
	size_t Size() const
	{
		return m_buffer.size();
	}

	// Pointer to the buffer start (valid until the next mutating call).
	const u8* Data() const
	{
		return m_buffer.data();
	}

	// Access to the underlying bytes (e.g. to move them out to the caller).
	const std::vector<u8>& Bytes() const
	{
		return m_buffer;
	}

	std::vector<u8> TakeBytes()
	{
		return std::move(m_buffer);
	}

	// Drop all contents, keeping any reserved capacity.
	void Clear()
	{
		m_buffer.clear();
	}

private:
	std::vector<u8> m_buffer;
};

#endif // MEMWRITER_H_

// EOF - memwriter.h
