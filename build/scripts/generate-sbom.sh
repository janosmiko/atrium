#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Generate an SPDX SBOM for the shipped bundle.
#
# Reuses Chromium's own third-party license scanner
# ($SRC/tools/licenses/licenses.py, format=spdx) rather than a new tool: it
# already knows every third_party/ package, its license and its metadata, and
# it is the same data Chromium's own about://credits is built from. This is a
# read-only scan of the checkout - no gn gen, no ninja, nothing written under
# out/atrium.
#
# The scan omits `--gn-out-dir`/`--gn-target` on purpose: those need an
# existing build to ask "what did this binary actually link", which this
# script must not create (see build/scripts/build-shell.sh). Without them the scanner
# lists every third_party/ package in the checkout instead of only the linked
# subset - a superset of the shipped bundle, not a subset, so it stays a safe
# (if conservative) SBOM without a build.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
# shellcheck disable=SC1091
source "$REPO/build/scripts/env.sh"

if [ ! -d "$SRC" ]; then
  echo "generate-sbom: no Chromium checkout at $SRC; nothing to scan" >&2
  exit 1
fi

VERSION_FILE="$REPO/build/VERSION"
CHROMIUM_TAG="$(grep '^chromium_tag=' "$VERSION_FILE" | cut -d= -f2)"

OUT_DIR="$REPO/build/sbom"
OUT_FILE="$OUT_DIR/atrium.spdx.json"
mkdir -p "$OUT_DIR"

echo "generate-sbom: scanning $SRC (chromium_tag=$CHROMIUM_TAG)"
python3 "$SRC/tools/licenses/licenses.py" license_file \
  --scan-root "$SRC" \
  --target-os mac \
  --extra-third-party-dirs "third_party/atrium" \
  --format spdx \
  --spdx-doc-name "Atrium-${CHROMIUM_TAG}" \
  --spdx-doc-namespace "https://github.com/janosmiko/atrium/sbom/${CHROMIUM_TAG}" \
  "$OUT_FILE"

count=$(python3 -c "import json,sys; print(len(json.load(open(sys.argv[1]))['packages']))" "$OUT_FILE")
echo "generate-sbom: wrote $OUT_FILE ($count packages, chromium_tag=$CHROMIUM_TAG)"
