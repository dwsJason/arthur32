#!/bin/bash
# Arthur32 end-to-end test matrix.
#
# Covers: round-trip (verbatim + forced rebuild), bigdata injection (small,
# >64K, dynamic KIND, multiple), name-trim (default + --no-trim-names), and
# ExpressLoad (identity rebuild + inject/trim directory rebuild).
#
# Needs merlin32 on PATH (or at ~/bin/merlin32) to (re)assemble the bigdata
# fixtures.  Run from anywhere.
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
A32="$ROOT/build/arthur32"
MERLIN="${MERLIN32:-$HOME/bin/merlin32}"
FIX="$HERE/fixtures"
BD="$HERE/bigdata_project"
TMP="$(mktemp -d)"
pass=0; fail=0
ok()   { echo "  PASS  $1"; pass=$((pass+1)); }
bad()  { echo "  FAIL  $1"; fail=$((fail+1)); }
ident(){ cmp -s "$1" "$2" && ok "$3" || bad "$3"; }

echo "=== build ==="
cmake --build "$ROOT/build" >/dev/null 2>&1 || { echo "build FAILED"; exit 1; }

echo "=== round-trip: verbatim pass-through (byte-identical) ==="
for f in "$FIX/test.s16" "$FIX/fun2gs.sys16"; do
  "$A32" --no-trim-names "$f" "$TMP/v_$(basename $f)" 2>/dev/null
  ident "$f" "$TMP/v_$(basename $f)" "verbatim $(basename $f)"
done

echo "=== round-trip: forced header rebuild (byte-identical) ==="
for f in "$FIX/test.s16" "$FIX/fun2gs.sys16"; do
  "$A32" --no-trim-names --force-rebuild "$f" "$TMP/r_$(basename $f)" 2>/dev/null
  ident "$f" "$TMP/r_$(basename $f)" "force-rebuild $(basename $f)"
done

echo "=== ExpressLoad: identity rebuild (byte-identical) ==="
for f in "$FIX/gng.sys16" "$FIX/paddler.sys16" "$FIX/ghosts.sys16"; do
  "$A32" --no-trim-names --force-xpress "$f" "$TMP/x_$(basename $f)" 2>/dev/null
  ident "$f" "$TMP/x_$(basename $f)" "xpress-identity $(basename $f)"
done

echo "=== ExpressLoad: name-trim + directory rebuild (self-consistent) ==="
for f in "$FIX/gng.sys16" "$FIX/paddler.sys16" "$FIX/ghosts.sys16"; do
  "$A32" "$f" "$TMP/t_$(basename $f)" 2>/dev/null
  if python3 "$HERE/verify_xpress.py" "$TMP/t_$(basename $f)" >/dev/null 2>&1; then
    ok "xpress-trim $(basename $f)"; else bad "xpress-trim $(basename $f)"; fi
done

echo "=== bigdata injection ==="
# merlin32 writes its output relative to the CWD, so assemble from inside $BD.
assemble() { ( cd "$BD" && "$MERLIN" . "$1" ) >/dev/null 2>&1; }

# Non-express fixture (data segment).
assemble bigdata.link
"$A32" "$BD/bigdata.s16" "$TMP/bd.s16" 2>/dev/null
strings "$TMP/bd.s16" | grep -q ARTHUR32-BIGDATA && ok "inject: payload present" || bad "inject: payload present"

# >64K payload.
perl -e 'print "Z" x 70000' > "$BD/data.bin"
"$A32" "$BD/bigdata.s16" "$TMP/bd64.s16" 2>/dev/null
sz=$(stat -f%z "$TMP/bd64.s16" 2>/dev/null || stat -c%s "$TMP/bd64.s16")
[ "$sz" -gt 70000 ] && ok "inject: >64K payload ($sz bytes)" || bad "inject: >64K payload"
printf 'ARTHUR32-BIGDATA-PAYLOAD-0123456789ABCDEF' > "$BD/data.bin"   # restore

# KIND preservation: the placeholder segment's kind line (just below its
# 'myart' seg line in the -v dump) must be the same before and after the rewrite.
assemble bigdata.link
kind_of() { "$A32" -v "$1" /dev/null 2>/dev/null | grep -A1 "'myart'" | grep -oE 'kind=\$[0-9A-Fa-f]+' | head -1; }
ink=$(kind_of "$BD/bigdata.s16")
"$A32" "$BD/bigdata.s16" "$TMP/bdk.s16" 2>/dev/null
outk=$(kind_of "$TMP/bdk.s16")
[ -n "$ink" ] && [ "$ink" = "$outk" ] && ok "inject: KIND preserved ($ink)" || bad "inject: KIND preserved (in=$ink out=$outk)"

# Load-name copy: after injection the LOADNAME must equal the SEGNAME ("myart"),
# so the segment is loadable by name; the "bigdata" marker must be gone.
dump_out=$("$A32" -v "$TMP/bdk.s16" /dev/null 2>/dev/null)
echo "$dump_out" | grep -q "'myart'  (load 'myart')" && ok "inject: SEGNAME copied to LOADNAME" || bad "inject: SEGNAME copied to LOADNAME"
echo "$dump_out" | grep -q "(load 'bigdata')" && bad "inject: marker keyword removed" || ok "inject: marker keyword removed"

# Idempotency: re-running on injected output finds no placeholders (the load name
# is now "myart", not a marker keyword) and succeeds.
"$A32" -v "$TMP/bdk.s16" "$TMP/bd_rerun.s16" 2>/dev/null | grep -q "injected 0 placeholder" \
  && ok "inject: re-run is idempotent (0 placeholders)" || bad "inject: re-run is idempotent"

# Non-"bigdata" keyword: "incbin" must inject identically.
assemble keyword.link
if [ -f "$BD/keyword.s16" ]; then
  "$A32" "$BD/keyword.s16" "$TMP/kw.s16" 2>/dev/null
  strings "$TMP/kw.s16" | grep -q ARTHUR32-BIGDATA && ok "inject: 'incbin' keyword works" || bad "inject: 'incbin' keyword works"
  rm -f "$BD/keyword.s16"
else
  bad "inject: keyword - merlin32 did not produce a file"
fi

echo "=== dry-run ==="
# --dry-run reports the placeholder + data file and writes no output file.
rm -f "$TMP/dry_out.s16"
dry=$("$A32" --dry-run "$BD/bigdata.s16" "$TMP/dry_out.s16" 2>/dev/null)
[ ! -f "$TMP/dry_out.s16" ] && ok "dry-run: no output file written" || bad "dry-run: no output file written"
echo "$dry" | grep -q "data.bin" && echo "$dry" | grep -q "myart" && ok "dry-run: reports placeholder + data file" || bad "dry-run: reports placeholder + data file"

# --dry-run exits non-zero when a referenced data file is missing.
mv "$BD/data.bin" "$BD/data.bin.bak"
"$A32" --dry-run "$BD/bigdata.s16" >/dev/null 2>&1; rc=$?
mv "$BD/data.bin.bak" "$BD/data.bin"
[ "$rc" -ne 0 ] && ok "dry-run: missing data file -> non-zero exit" || bad "dry-run: missing data file -> non-zero exit"

echo "=== bigdata injection INTO an ExpressLoad file ==="
assemble bigdata_xpl.link
if [ -f "$BD/bigdata_xpl.s16" ]; then
  "$A32" "$BD/bigdata_xpl.s16" "$TMP/bd_xpl_out.s16" 2>/dev/null
  if python3 "$HERE/verify_xpress.py" "$TMP/bd_xpl_out.s16" >/dev/null 2>&1 \
     && strings "$TMP/bd_xpl_out.s16" | grep -q ARTHUR32-BIGDATA; then
    ok "inject+xpress: consistent & payload present"; else bad "inject+xpress"; fi
  rm -f "$BD/bigdata_xpl.s16"
else
  bad "inject+xpress: merlin32 did not produce a file"
fi

echo
echo "=== RESULTS: $pass passed, $fail failed ==="
rm -rf "$TMP"
exit $([ "$fail" -eq 0 ] && echo 0 || echo 1)
