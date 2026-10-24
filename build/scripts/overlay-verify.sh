#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Drift guard between overlay/ and the owned roots in the live $SRC checkout.
# Walks git-tracked paths only, so a gitignored build dir (e.g. a Rust
# target/) never counts as an extra file.
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck disable=SC1091
source ./env.sh
# shellcheck disable=SC1091
source ./owned-roots.sh
REPO="$(cd ../.. && pwd)"
OVERLAY="$REPO/overlay"

src_list="$(mktemp)"
overlay_list="$(mktemp)"
trap 'rm -f "$src_list" "$overlay_list"' EXIT

git -C "$SRC" ls-tree -r --name-only HEAD -- "${OWNED_ROOTS[@]}" | sort > "$src_list"

existing_roots=()
for root in "${OWNED_ROOTS[@]}"; do
  [ -d "$OVERLAY/$root" ] && existing_roots+=("$root")
done
if [ "${#existing_roots[@]}" -gt 0 ]; then
  # A file deleted from disk but still in the index must not count.
  (cd "$OVERLAY" && git ls-files --cached --others --exclude-standard -- "${existing_roots[@]}" \
    | while IFS= read -r p; do if [ -f "$p" ]; then printf '%s\n' "$p"; fi; done) | sort > "$overlay_list"
else
  : > "$overlay_list"
fi

fail=0

while IFS= read -r path; do
  [ -z "$path" ] && continue
  echo "EXTRA: $path is tracked under an owned root in \$SRC but missing from overlay/" >&2
  fail=1
done < <(comm -23 "$src_list" "$overlay_list")

while IFS= read -r path; do
  [ -z "$path" ] && continue
  echo "ORPHAN: overlay/$path has no matching tracked file under its owned root in \$SRC" >&2
  fail=1
done < <(comm -13 "$src_list" "$overlay_list")

while IFS= read -r path; do
  [ -z "$path" ] && continue
  if ! cmp -s "$SRC/$path" "$OVERLAY/$path"; then
    echo "DRIFT: $path differs between \$SRC and overlay/" >&2
    fail=1
  fi
done < <(comm -12 "$src_list" "$overlay_list")

if [ "$fail" -eq 0 ]; then
  echo "overlay-verify OK: $(wc -l < "$src_list" | tr -d ' ') files match"
else
  exit 1
fi
