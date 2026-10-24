#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Rescue direction for an edit made in $SRC by mistake: copy the owned roots
# back from the tree into overlay/, and drop overlay files the tree no
# longer tracks.
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck disable=SC1091
source ./env.sh
# shellcheck disable=SC1091
source ./owned-roots.sh
REPO="$(cd ../.. && pwd)"
OVERLAY="$REPO/overlay"

if [ ! -d "$SRC" ]; then
  echo "error: no Chromium checkout at $SRC (run build/scripts/bootstrap.sh)" >&2
  exit 1
fi

tracked="$(mktemp)"
trap 'rm -f "$tracked"' EXIT
git -C "$SRC" ls-tree -r --name-only HEAD -- "${OWNED_ROOTS[@]}" > "$tracked"

while IFS= read -r path; do
  [ -z "$path" ] && continue
  mkdir -p "$OVERLAY/$(dirname "$path")"
  cp "$SRC/$path" "$OVERLAY/$path"
  echo "adopted: $path"
done < "$tracked"

for root in "${OWNED_ROOTS[@]}"; do
  [ -d "$OVERLAY/$root" ] || continue
  while IFS= read -r -d '' file; do
    path="${file#"$OVERLAY"/}"
    if ! grep -qxF "$path" "$tracked"; then
      rm -f "$file"
      echo "removed: $path"
    fi
  done < <(find "$OVERLAY/$root" -type f -print0)
done
