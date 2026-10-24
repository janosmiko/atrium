#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# SUPERSEDED by build-assets-car.sh, which `make icns` runs. Running this
# instead overwrites build/atrium.icns with a flat, non-glass icns.
# Builds build/atrium.icns - the app bundle's Dock/Finder icon - from the
# DEFAULT AppIconVariant PNG (crimson-paper; see DEFAULT_APP_ICON
# in build/icons). Reuses
# mask-icon-alpha.py's rounded-rect alpha mask: the qlmanage-rendered source
# PNG bakes an opaque white background into the corners, so the mask punches
# real transparency back in before the .icns is assembled.
#
# Output is committed to git as a binary asset under build/, deliberately
# NOT under $SRC/patches/ - that keeps the Chromium patch series text-only.
# build/scripts/inject-icns.sh copies this file into the built app bundle post-build.
set -euo pipefail
REPO_BUILD="$(cd "$(dirname "$0")" && pwd)"

DEFAULT_VARIANT="crimson-paper"
SRC_PNG="$REPO_BUILD/../icons/${DEFAULT_VARIANT}.png"
OUT_ICNS="$REPO_BUILD/../atrium.icns"

if [ ! -f "$SRC_PNG" ]; then
  echo "error: $SRC_PNG not found; run 'make icons' first" >&2
  exit 1
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

# The variant PNGs are pre-masked by build-icons.sh (guarded by
# testEveryVariantHasTransparentCorners) - no second mask pass here, it
# would soften the antialiased ring and make the icns nondeterministic.
masked="$SRC_PNG"

iconset="$tmp_dir/atrium.iconset"
mkdir -p "$iconset"
for size in 16 32 128 256 512; do
  double=$((size * 2))
  sips -z "$size" "$size" "$masked" --out "$iconset/icon_${size}x${size}.png" >/dev/null
  sips -z "$double" "$double" "$masked" --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
done

iconutil -c icns "$iconset" -o "$OUT_ICNS"
echo "built: $OUT_ICNS"
