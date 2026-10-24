#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Copy rust/atrium-core over its overlay and Chromium-tree copies. The repo
# copy is canonical, the only direction safe to automate.
#
# Deletes files the other two copies have that the repo copy no longer does,
# so a removed source file does not linger and keep compiling.
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck disable=SC1091
source ./env.sh
REPO="$(cd ../.. && pwd)"

CANON="$REPO/rust/atrium-core"
OVERLAY_COPY="$REPO/overlay/third_party/atrium/core"
SRC_COPY="$SRC/third_party/atrium/core"

if [ ! -d "$SRC" ]; then
  echo "error: no Chromium checkout at $SRC (run build/scripts/bootstrap.sh)" >&2
  exit 1
fi

mkdir -p "$OVERLAY_COPY"
rsync -a --delete --exclude target --exclude .git "$CANON/" "$OVERLAY_COPY/"

mkdir -p "$SRC_COPY"
rsync -a --delete --exclude target --exclude .git "$CANON/" "$SRC_COPY/"

echo "synced: $CANON -> $OVERLAY_COPY, $SRC_COPY"
