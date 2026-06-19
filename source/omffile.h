//
// omffile.h
//
// A whole OMF load file: a list of segments parsed from a merlin32 .s16.
//

#ifndef OMFFILE_H_
#define OMFFILE_H_

#include "bctypes.h"
#include "omfsegment.h"

#include <vector>

class OMFFile
{
public:
	OMFFile( const char* pPath, bool bVerbose );

	bool IsValid() const { return m_bValid; }

	// Print every segment (sanity / -v).
	void Dump() const;

	// Replace every placeholder segment's pathname body with the contents of the
	// referenced data file.  A placeholder is a segment whose LOADNAME is one of
	// the marker keywords (see OMFSegment::IsPlaceholder).  pInputPath is the .s16
	// path, used to resolve data paths that are relative to it.  Returns the
	// number of placeholder segments processed, or -1 on error.
	//
	// On injection the (meaningful) SEGNAME is copied into the LOADNAME so the
	// segment is loadable by name at runtime (the loader matches on LOADNAME).
	// This also makes re-runs safe: the marker keyword no longer survives, so a
	// second pass finds no placeholders.
	//
	// When bDryRun is true, nothing is loaded or modified: each placeholder is
	// reported (resolved path + size, or MISSING) and, if pMissingOut is non-null,
	// the count of missing data files is returned through it.
	int InjectBigData( const char* pInputPath, bool bDryRun = false, int* pMissingOut = nullptr );

	// Convert every fixed-width SEGNAME (LABLEN=10, blank-padded) to a variable
	// length name (LABLEN=0, length-prefixed).  LOADNAME stays a fixed 10 bytes
	// (format requirement).  Default behaviour; suppressed by --no-trim-names.
	void TrimNames();

	// Serialize all segments and write them out in a single pass.
	bool Write( const char* pPath ) const;

	// True if the file carries a leading ~ExpressLoad segment.
	bool HasExpressLoad() const;

	// True if any segment has been modified (injected or name-trimmed).
	bool AnyModified() const;

	// Regenerate the ~ExpressLoad directory to match the (possibly resized /
	// renamed) segments.  Call after InjectBigData()/TrimNames().  Returns false
	// on a malformed directory.  No-op if there is no ExpressLoad segment.
	bool RebuildExpressLoad();

	std::vector<OMFSegment>&       Segments()       { return m_segments; }
	const std::vector<OMFSegment>& Segments() const { return m_segments; }

private:
	bool m_bVerbose;
	bool m_bValid;
	std::vector<OMFSegment> m_segments;
};

#endif // OMFFILE_H_
