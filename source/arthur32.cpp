// arthur32.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
// Arthur32 is a companion tool to merlin32 for the Apple IIgs.  It post-processes
// a merlin32 OMF load file (.s16), finding "placeholder" segments (load name
// "bigdata", "incbin", "bindata" or "data") whose body is just a pathname, and
// replacing each one in place with a real OMF data segment containing the contents
// of the referenced file - making it easy to ship data larger than 64K.  The
// segment's name is copied into its load name so it can be loaded by name at
// runtime.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "omffile.h"

//------------------------------------------------------------------------------
void helpText()
{
	printf("arthur32 - v1.0\n");
	printf("---------------\n");
	printf("merlin32 companion tool: inject >64K data files into an OMF load file.\n");
	printf("Replaces each placeholder segment - one whose load name is 'bigdata',\n");
	printf("'incbin', 'bindata' or 'data' and whose body is a pathname - with the\n");
	printf("contents of the referenced file, as a valid OMF data segment.\n");
	printf("\narthur32 [options] <OMF_File> <Out_File>\n");
	printf("  -v               verbose\n");
	printf("  --dry-run        list the placeholders and data files; write nothing\n");
	printf("  --no-trim-names  keep merlin32's fixed-width segment names\n\n");

	exit(-1);
}

//------------------------------------------------------------------------------

int main(int argc, char* argv[])
{
	char* pInfilePath  = nullptr;
	char* pOutfilePath = nullptr;
	bool  bVerbose      = false;
	bool  bTrimNames    = true;
	bool  bDryRun       = false;
	bool  bForceRebuild = false;   // test hook: rebuild every segment header
	bool  bForceXpress  = false;   // test hook: rebuild ExpressLoad even if unmodified

	for (int idx = 1; idx < argc; ++idx )
	{
		char* arg = argv[ idx ];

		if ('-' == arg[0])
		{
			// Parse as an option
			if (0 == strcmp(arg, "--no-trim-names"))
			{
				bTrimNames = false;
			}
			else if (0 == strcmp(arg, "--dry-run"))
			{
				bDryRun = true;
			}
			else if (0 == strcmp(arg, "--force-rebuild"))
			{
				bForceRebuild = true;
			}
			else if (0 == strcmp(arg, "--force-xpress"))
			{
				bForceXpress = true;
			}
			else if ('v' == arg[1])
			{
				bVerbose = true;
			}
			else
			{
				printf("ERROR: Unknown option, Arg %d = %s\n\n", idx, arg);
				helpText();
			}
		}
		else if (nullptr == pInfilePath)
		{
			// Assume the first non-option is an input file path
			pInfilePath = argv[ idx ];
		}
		else if (nullptr == pOutfilePath)
		{
			// Assume second non-option is an output file path
			pOutfilePath = argv[ idx ];
		}
		else
		{
			// Oh Crap, we have a non-option, but we don't know what to do with it
			printf("ERROR: Invalid option, Arg %d = %s\n\n", idx, argv[ idx ]);
			helpText();
		}
	}

	if (nullptr == pInfilePath)
	{
		helpText();
	}

	// Load and parse the OMF load file.
	OMFFile omf_file( pInfilePath, bVerbose );
	if (!omf_file.IsValid())
	{
		return -1;
	}

	// Dry run: report each placeholder and its data file, then stop without
	// modifying or writing anything.  Exit non-zero if any data file is missing.
	if (bDryRun)
	{
		printf( "arthur32: dry run - no output will be written\n" );
		int missing = 0;
		int found = omf_file.InjectBigData( pInfilePath, /*bDryRun=*/true, &missing );
		if (found < 0)
		{
			return -1;   // malformed placeholder
		}
		printf( "arthur32: %d placeholder segment(s), %d missing - no output written\n",
			found, missing );
		return (missing > 0) ? 1 : 0;
	}

	// Inject every placeholder segment's referenced data file.
	int injected = omf_file.InjectBigData( pInfilePath );
	if (injected < 0)
	{
		return -1;
	}
	if (bVerbose)
	{
		printf( "arthur32: injected %d placeholder segment(s)\n", injected );
	}

	// Trim fixed-width segment names (default on; --no-trim-names disables).
	if (bTrimNames)
	{
		omf_file.TrimNames();
	}

	// If the file uses ExpressLoad and we changed any segment (or we are forcing
	// it for testing), regenerate the ~ExpressLoad directory so it matches the
	// new layout.  A pure pass-through leaves it byte-identical and is skipped.
	if (omf_file.HasExpressLoad() && (omf_file.AnyModified() || bForceXpress))
	{
		if (!omf_file.RebuildExpressLoad())
		{
			printf( "ERROR: failed to rebuild the ExpressLoad directory\n" );
			return -1;
		}
		if (bVerbose)
		{
			printf( "arthur32: rebuilt ExpressLoad directory\n" );
		}
	}

	// Test hook: force every segment through the rebuild path.
	if (bForceRebuild)
	{
		for (OMFSegment& seg : omf_file.Segments())
		{
			seg.m_bModified = true;
		}
	}

	// Write the (currently pass-through) result.
	if (pOutfilePath)
	{
		if (!omf_file.Write( pOutfilePath ))
		{
			printf( "ERROR: could not write '%s'\n", pOutfilePath );
			return -1;
		}
	}

	return 0;
}
