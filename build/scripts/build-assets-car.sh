#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Compiles build/icons-src/*.icon into build/Assets.car and build/atrium.icns.
# A package's directory name becomes its stack name in the catalog, which is
# how [NSImage imageNamed:@"<slug>"] reaches one variant at runtime.
set -euo pipefail
REPO_BUILD="$(cd "$(dirname "$0")/.." && pwd)"

SRC_DIR="$REPO_BUILD/icons-src"
OUT_CAR="$REPO_BUILD/Assets.car"
OUT_ICNS="$REPO_BUILD/atrium.icns"
APP_ICON="AppIcon"

if ! xcrun --find actool >/dev/null 2>&1; then
  echo "error: actool not found; install Xcode and run 'sudo xcode-select -s /Applications/Xcode.app'" >&2
  exit 1
fi

icons=("$SRC_DIR"/*.icon)
if [ ! -d "${icons[0]}" ]; then
  echo "error: no .icon packages in $SRC_DIR; run 'make icons' first" >&2
  exit 1
fi

if [ ! -d "$SRC_DIR/$APP_ICON.icon" ]; then
  echo "error: $SRC_DIR/$APP_ICON.icon missing; --app-icon has nothing to select" >&2
  exit 1
fi

tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT
compiled="$tmp_dir/compiled"
mkdir -p "$compiled"

# One invocation for the whole set: actool overwrites Assets.car instead of
# merging, so a loop would leave only the last variant. Paths are absolute
# because actool resolves relative ones against its compiler service's cwd.
xcrun actool "${icons[@]}" \
  --compile "$compiled" \
  --app-icon "$APP_ICON" \
  --include-all-app-icons \
  --output-partial-info-plist "$tmp_dir/partial-info.plist" \
  --output-format human-readable-text \
  --notices --warnings --errors \
  --enable-on-demand-resources NO \
  --development-region en \
  --platform macosx \
  --minimum-deployment-target 26.0 \
  --target-device mac

if [ ! -f "$compiled/Assets.car" ]; then
  echo "error: actool exited 0 but produced no Assets.car" >&2
  exit 1
fi

cp "$compiled/Assets.car" "$OUT_CAR"
cp "$compiled/$APP_ICON.icns" "$OUT_ICNS"
echo "built: $OUT_CAR ($(xcrun assetutil --info "$OUT_CAR" | grep -c IconImageStack) icon stacks)"
echo "built: $OUT_ICNS"
