#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Atrium-owned functions with no caller anywhere in the build.
#
# A grep over the sources is rejected: Atrium's method names collide with
# Chromium's, so `Show` returned 3395 hits and buried the two genuinely uncalled
# methods the check was built from. Attributing a reference to its declaring
# class needs type resolution - and the compiler has already done it. A mangled
# C++ symbol carries the namespace and the class, so __ZN6atrium26Atrium...4Show
# cannot be confused with any other Show in the binary.
#
# A caller in a DIFFERENT translation unit leaves an undefined (U) symbol. A
# caller in the SAME one leaves no U at all, only a relocation, so the defining
# object's relocation records are read too - without them every helper called
# only by its own file would be reported as an orphan.
#
# WHAT THIS CANNOT SEE, and it is the reason the output is a lead list rather
# than a guard: an INLINED call leaves no symbol reference and no relocation, so
# a function whose every caller was inlined looks identical to one with no caller
# at all. Measured on this build: of the reported names sampled, CallPage,
# UpdateStripTracking, PushFoyerSnapshot and IsResizeDragActive each have real
# call sites in their own file and all of them were inlined away. Treat a
# reported name as a question, not an answer.
#
# Closing that gap needs call-site information the binary does not carry here.
# out/atrium builds with symbol_level = 0, so there is no DWARF to read the
# inlined call sites from.
#
# Constructors, destructors and the views MetaData hooks are dropped: they are
# reached implicitly or by the framework, so reporting them buries the entry
# points this exists to find under several hundred rows of compiler machinery.
#
# Usage: orphan-symbols.sh [out-dir]        (default: out/atrium in the checkout)
#        orphan-symbols.sh --selftest       (prove it can report - see below)
set -euo pipefail

SRC="${ATRIUM_CHROMIUM_SRC:-$HOME/chromium-atrium/src}"
ARG="${1:-}"
[ "$ARG" = "--selftest" ] && ARG=""
OUT="${ARG:-$SRC/out/atrium}"
OBJDUMP="$SRC/third_party/llvm-build/Release+Asserts/bin/llvm-objdump"
CXXFILT="$SRC/third_party/llvm-build/Release+Asserts/bin/llvm-cxxfilt"

[ -d "$OUT/obj" ] || { echo "no objects at $OUT/obj - build first" >&2; exit 2; }
[ -x "$OBJDUMP" ] || { echo "no llvm-objdump at $OBJDUMP" >&2; exit 2; }
[ -x "$CXXFILT" ] || { echo "no llvm-cxxfilt at $CXXFILT" >&2; exit 2; }

# Proves the check can report, by hiding the one object that calls a function
# known to have exactly one caller and watching that function appear. It hides
# the reference rather than deleting the call and rebuilding, so it exercises
# the real pipeline in seconds instead of ten minutes - it does not prove the
# compiler would emit what a deleted call implies, only that an absent caller
# is detected.
if [ "${1:-}" = "--selftest" ]; then
  probe='atrium::IsLittleWindow'
  caller=profile_browser_collection.o
  plain_out="$("$0")"
  if printf '%s' "$plain_out" | grep -q "$probe"; then
    echo "SELFTEST FAILED: $probe is already reported, so hiding its caller proves nothing" >&2
    exit 1
  fi
  hidden_out="$(HIDE_CALLER="$caller" "$0")"
  if printf '%s' "$hidden_out" | grep -q "$probe"; then
    echo "SELFTEST PASSED: $probe appears once $caller is withheld"
    exit 0
  fi
  echo "SELFTEST FAILED: $probe stayed hidden with no caller left" >&2
  exit 1
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# The objects that DEFINE Atrium code. Everything else is only ever a caller.
find "$OUT/obj/chrome/browser/ui" -name '*atrium*.o' > "$work/atrium-objs" 2>/dev/null || true
find "$OUT/obj/chrome/browser/ui/atrium" -name '*.o' >> "$work/atrium-objs" 2>/dev/null || true
sort -u "$work/atrium-objs" -o "$work/atrium-objs"
[ -s "$work/atrium-objs" ] || { echo "no Atrium objects found under $OUT" >&2; exit 2; }

# Defined, non-test, in namespace atrium. Templates and base:: machinery
# instantiated INTO these objects are not Atrium's to audit, so the symbol must
# open with the atrium namespace rather than merely mention it.
xargs nm -o < "$work/atrium-objs" 2>/dev/null \
  | awk '$3 == "T" || $3 == "t" {print $4}' \
  | grep -E '^__ZN?K?6atrium' \
  | grep -Ev 'unittest|BrowserTest|ForTesting' \
  | sort -u > "$work/defined" || true

# Every cross-TU reference in the whole build, not only Atrium's own objects:
# a caller may live anywhere.
index="$OUT/atrium-symbol-index.txt"
newest=""
[ ! -s "$index" ] || [ "$OUT/.ninja_log" -nt "$index" ] && newest="stale"
if [ ! -s "$index" ] || [ -n "$newest" ]; then
  find "$OUT/obj" -name '*.o' -print0 \
    | xargs -0 -P 8 -n 400 nm -o 2>/dev/null \
    | awk '$2 == "U" && $3 ~ /^__ZN?K?6atrium/ {print $3, $1}' \
    | sort -u > "$index"
fi

# One object's references withheld, which is what --selftest drives. Filtered
# out of the index rather than rescanned, so the expensive half runs once.
if [ -n "${HIDE_CALLER:-}" ]; then
  grep -v " .*$HIDE_CALLER" "$index" | awk '{print $1}' | sort -u \
    > "$work/referenced-across" || true
else
  awk '{print $1}' "$index" | sort -u > "$work/referenced-across" || true
fi

# Same-TU callers leave a relocation and no U.
# Same-TU callers leave a relocation and no U, so the defining object's own
# relocations are read. Per object, and intersected with what that object
# DEFINES: crediting a relocation from any Atrium object would let a caller in
# another file vouch for a symbol through this path as well as through its U
# entry, which makes hiding that caller impossible and the whole check
# self-confirming.
within_index="$OUT/atrium-intra-tu-index.txt"
if [ ! -s "$within_index" ] || [ -n "$newest" ]; then
  : > "$within_index"
  while read -r obj; do
    nm "$obj" 2>/dev/null | awk '$2 == "T" || $2 == "t" {print $3}' | sort -u \
      > "$work/obj-defined"
    "$OBJDUMP" --reloc "$obj" 2>/dev/null | awk '{print $3}' | sort -u \
      > "$work/obj-reloc"
    comm -12 "$work/obj-defined" "$work/obj-reloc" \
      | sed "s|\$| ${obj##*/}|" >> "$within_index"
  done < "$work/atrium-objs"
  sort -u "$within_index" -o "$within_index"
fi

if [ -n "${HIDE_CALLER:-}" ]; then
  grep -v " $HIDE_CALLER\$" "$within_index" | awk '{print $1}' | sort -u \
    > "$work/referenced-within" || true
else
  awk '{print $1}' "$within_index" | sort -u > "$work/referenced-within" || true
fi

sort -u "$work/referenced-across" "$work/referenced-within" > "$work/referenced"
comm -23 "$work/defined" "$work/referenced" \
  | grep -Ev 'C[12]E|D[0-2]E|MetaData|_block_invoke' > "$work/orphans" || true

count=$(wc -l < "$work/orphans" | tr -d ' ')
echo "Atrium symbols defined: $(wc -l < "$work/defined" | tr -d ' ')"
echo "with no caller anywhere: $count"
echo
sed 's/^_//' "$work/orphans" | "$CXXFILT" > "$work/named"

# Second pass, over names rather than symbols. An INLINED call leaves no symbol
# and no relocation, so the symbol side alone reports a fully inlined function as
# dead - measured on this build, that was every name sampled. The bare-name grep
# this project rejected earlier failed for the opposite reason: `Show` collides
# with Chromium's and returns thousands of hits. Neither works alone. Together
# they do: the symbol side says WHICH class, and the source side only has to
# answer whether that class's own files mention the name anywhere other than its
# declaration and its definition.
#
# It errs toward silence. A name appearing only in a comment counts as a caller,
# so a truly dead function can be missed - which is the right way round for a
# list a human reads.
: > "$work/confirmed"
while IFS= read -r name; do
  method="${name%%(*}"
  method="${method##*::}"
  [ -n "$method" ] || continue
  uses=$({ grep -rn --include='*.cc' --include='*.mm' --include='*.h' \
           -e "$method(" -e "&[A-Za-z_]*::$method" "$SRC/chrome/browser/ui/atrium" \
           2>/dev/null \
         | grep -v "::$method(" \
         | grep -Ev "^[^:]+:[0-9]+:[A-Za-z_~]" \
         | wc -l | tr -d ' '; } || true)
  if [ "$uses" = "0" ]; then printf '%s\n' "$name" >> "$work/confirmed"; fi
done < "$work/named"

echo "after removing names their own sources still call: $(wc -l < "$work/confirmed" | tr -d ' ')"
echo
cat "$work/confirmed"

# Reporting orphans is the job, so a clean tree and a dirty one both exit 0.
# A caller of this script decides what a finding is worth.
exit 0
