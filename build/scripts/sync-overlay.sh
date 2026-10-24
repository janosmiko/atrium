#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Copy overlay/ over the owned roots in $SRC. The overlay copy is canonical;
# this direction is the only one that is safe to automate.
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

for root in "${OWNED_ROOTS[@]}"; do
  [ -d "$OVERLAY/$root" ] || continue
  mkdir -p "$SRC/$root"
  rsync -a --delete --exclude target --exclude .serena "$OVERLAY/$root/" "$SRC/$root/"
done
echo "synced: $OVERLAY -> $SRC (${ROOTS[*]})"
