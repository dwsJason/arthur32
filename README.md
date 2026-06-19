# arthur32

A companion tool to [merlin32](https://github.com/apple2-iigs/merlin32) for the
Apple IIgs, to make it easy to ship **data larger than 64K**.

merlin32 can't natively place a single data object bigger than one 64K bank.
arthur32 post-processes merlin32's OMF load file (`.s16`): you author a small
*placeholder* segment whose body is just a pathname, and arthur32 replaces it
**in place** with a real OMF data segment containing that file's contents — bank
aligned, with no 64K limit — and rewrites the file's `~ExpressLoad` directory if
present, so it keeps loading fast.

```
merlin32 . myprog.link      # produces myprog.s16 with a 'bigdata' placeholder
arthur32 myprog.s16 myprog.out   # splices the data file(s) in
```

## How to use it

In your merlin32 link file, give a segment one of the **placeholder load names**
— `bigdata`, `incbin`, `bindata` or `data` — and put the data file's path in its
body. The segment name is yours to choose.

```
            asm   sprites.s
            knd   #$0001        ; a data segment (or a DYNAMIC kind)
            lna   bigdata       ; <-- THE MARKER (load name)
            sna   sprites       ; <-- your name, kept
* sprites.s:
            asc   'art/sprites.bin'   ; path, relative to the .s16's directory
```

arthur32 finds every segment whose LOADNAME is one of those markers and swaps its
body for the contents of the referenced file. The path may use `/`, `\` or `:` as a
separator and is resolved relative to the input `.s16`. The segment's KIND is
preserved (so a DYNAMIC placeholder stays a dynamic load segment); `ALIGN` is set
to `$10000` and `BANKSIZE` to `0` so the data may exceed 64K and span banks.

It also **trims** merlin32's fixed-width segment names to variable length by
default (saving a few bytes per segment); pass `--no-trim-names` to keep them.

## Options

```
arthur32 [options] <OMF_File> <Out_File>
  -v               verbose
  --dry-run        list the placeholders and data files; write nothing
  --no-trim-names  keep merlin32's fixed-width segment names
```

With `--dry-run`, arthur32 reports each placeholder segment and the data file it
references without modifying or writing anything, and exits non-zero if any
referenced data file is missing — handy as a pre-flight check in a build script.

## Building

Vanilla C++17, no dependencies. Builds on Windows (Win64), Linux and macOS.

```
cmake -B build
cmake --build build
```

## Notes

- Run arthur32 **once** on merlin32's output. Injection is not idempotent (the
  injected segment keeps its `bigdata` load name).
- ExpressLoad (`XPL`) files are fully supported: the `~ExpressLoad` directory is
  regenerated to match the new layout.
- Only OMF v2 (what merlin32 emits) is supported.

## Tests

```
tests/run_tests.sh        # full end-to-end matrix (needs merlin32 to assemble fixtures)
```
