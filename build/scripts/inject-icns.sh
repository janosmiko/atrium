#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Stages the per-variant PNGs into the just-built Atrium.app and re-signs it.
# app.icns and Assets.car reach the bundle through chrome/BUILD.gn's own copy
# targets now, not this script - see atrium_repo_dir there.
set -euo pipefail
REPO_BUILD="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$REPO_BUILD/env.sh"

ICONS_SRC="$REPO_BUILD/../icons"
APP_BUNDLE="$SRC/${ATRIUM_OUT_DIR:-out/atrium}/Atrium.app"
ICONS_DEST="$APP_BUNDLE/Contents/Resources/atrium-icons"

if [ ! -d "$APP_BUNDLE" ]; then
  echo "inject-icns: $APP_BUNDLE not found, skipping (not an app-bundle build)"
  exit 0
fi

# The bundle stack is Dock-facing only, so the picker and the runtime Dock
# swap still need PNGs. AppIconPath in atrium_icon_source.cc resolves them here.
if [ -d "$ICONS_SRC" ]; then
  mkdir -p "$ICONS_DEST"
  for png in "$ICONS_SRC"/*.png; do
    [ -f "$png" ] || continue
    dest="$ICONS_DEST/$(basename "$png")"
    if ! cmp -s "$png" "$dest" 2>/dev/null; then
      cp "$png" "$dest"
      echo "inject-icns: wrote $dest"
    fi
  done
  # A retired variant stays in the bundle otherwise, and the picker keeps
  # offering a slug settings.rs no longer accepts.
  for stale in "$ICONS_DEST"/*.png; do
    [ -f "$stale" ] || continue
    if [ ! -f "$ICONS_SRC/$(basename "$stale")" ]; then
      rm "$stale"
      echo "inject-icns: removed stale $stale"
    fi
  done
fi

# ninja's own bundle step ad-hoc-signs without sealing resources, which fails
# codesign --verify --strict - every build needs a real re-sign, not only
# ones that happened to change a PNG.
atrium_codesign "$APP_BUNDLE"
echo "inject-icns: re-signed $APP_BUNDLE"
