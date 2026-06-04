#!/usr/bin/env python3
# Self-consistency check for an arthur32-rebuilt ExpressLoad file.
#
# Walks the OMF segments, then parses the ~ExpressLoad directory and verifies
# that, for every real segment, the directory's stored ABSOLUTE data offset
# actually lands just past that segment's leading LCONST ($F2 + 4-byte count),
# that the stored data length matches the LCONST count, and that the embedded
# header copy matches the segment's real header bytes from $0C on.
import sys, struct

def u16(b, o): return b[o] | (b[o+1] << 8)
def u32(b, o): return b[o] | (b[o+1] << 8) | (b[o+2] << 16) | (b[o+3] << 24)

def segments(data):
    segs, off = [], 0
    while off + 0x2C <= len(data):
        bytecnt = u32(data, off)
        if bytecnt < 0x2C or off + bytecnt > len(data): break
        dispname = u16(data, off + 0x28)
        dispdata = u16(data, off + 0x2A)
        segs.append({'off': off, 'bytecnt': bytecnt, 'dispname': dispname,
                     'dispdata': dispdata})
        off += bytecnt
    return segs

def main():
    data = open(sys.argv[1], 'rb').read()
    segs = segments(data)
    # find ~ExpressLoad (segnum 1 / kind $8001 at $14)
    xi = None
    for i, s in enumerate(segs):
        if u16(data, s['off'] + 0x14) == 0x8001:
            xi = i; break
    if xi is None:
        print("FAIL: no ~ExpressLoad segment"); sys.exit(1)
    reals = [s for i, s in enumerate(segs) if i != xi]
    N = len(reals)

    xb = data[segs[xi]['off'] + segs[xi]['dispdata'] : segs[xi]['off'] + segs[xi]['bytecnt']]
    assert xb[0] == 0xF2, "ExpressLoad body not LCONST"
    p = 5 + 4 + 2 + N*8 + N*2           # skip to Segment Header Table
    ok = True
    for i, s in enumerate(reals):
        dataoff = u32(xb, p); datalen = u32(xb, p+4)
        relocoff = u32(xb, p+8); reloclen = u32(xb, p+12)
        hxlen = s['dispdata'] - 12
        hdrcopy = xb[p+16 : p+16+hxlen]
        p += 16 + hxlen
        # check the header copy matches the real segment header from $0C
        real_hdr = data[s['off']+12 : s['off']+s['dispdata']]
        tag = f"seg#{i} @{s['off']}"
        if hdrcopy != real_hdr:
            print(f"FAIL {tag}: header copy != real header"); ok = False
        # check data offset points just past this seg's leading LCONST
        body0 = data[s['off'] + s['dispdata']]
        if body0 == 0xF2:
            lc = u32(data, s['off'] + s['dispdata'] + 1)
            expect = s['off'] + s['dispdata'] + 5
            if dataoff != expect:
                print(f"FAIL {tag}: dataoff {dataoff} != expected {expect}"); ok = False
            if datalen != lc:
                print(f"FAIL {tag}: datalen {datalen} != LCONST count {lc}"); ok = False
            if data[dataoff-5] != 0xF2:
                print(f"FAIL {tag}: byte before data is not $F2"); ok = False
            # reloc offset: when there are relocs, they start right after the data
            if reloclen > 0:
                expect_reloc = s['off'] + s['dispdata'] + 5 + datalen
                if relocoff != expect_reloc:
                    print(f"FAIL {tag}: relocoff {relocoff} != expected {expect_reloc}"); ok = False
                elif data[relocoff] not in (0xE2, 0xE3, 0xF5, 0xF6, 0xF7):
                    print(f"FAIL {tag}: relocoff does not point at a reloc record"); ok = False
        else:
            # no leading LCONST (e.g. DS-only) -> datalen should be 0
            if datalen != 0:
                print(f"FAIL {tag}: no LCONST but datalen={datalen}"); ok = False
        print(f"  {tag}: dataoff={dataoff} datalen={datalen} relocoff={relocoff} reloclen={reloclen}  OK")
    print("PASS: ExpressLoad directory is self-consistent" if ok else "FAILED")
    sys.exit(0 if ok else 1)

main()
