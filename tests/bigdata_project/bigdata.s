* bigdata.s - placeholder segment for arthur32 injection testing.
*
* The body is just the ASCII filename "data.bin". arthur32 is expected to
* recognise such a placeholder segment (by its LOADNAME marker keyword) and
* splice the real contents of data.bin into the segment body during its OMF
* rewrite, copying the SEGNAME into the load name so it is loadable by name.

            asc 'data.bin'
