#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Regenerates the app icon variant PNGs from their SVG sources (Brand Book v3
# murmuration mark). SVG sources: build/icons-src/<id>.svg, generated
# by build/scripts/gen-brand-icons.py from docs/brand/brand-book.html's `flow()` mark.
# Re-run only when a variant's SVG changes; build/icons/<id>.png is what
# build-icns.sh turns into the bundle icon.
#
# Renderer: qlmanage (Quick Look), not rsvg-convert - this machine doesn't
# have librsvg installed and qlmanage ships with every macOS install, so the
# pipeline needs nothing extra. It renders via WebKit's SVG support, which
# handles the gradients/clip-paths these icons use cleanly at 1024px.
set -euo pipefail
cd "$(dirname "$0")"

src_dir="../icons-src"
out_dir="../icons"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

qlmanage -t -s 1024 -o "$tmp_dir" "$src_dir"/*.svg >/dev/null

for svg in "$src_dir"/*.svg; do
  id="$(basename "$svg" .svg)"
  # qlmanage composites onto opaque white - punch the shared rounded-rect
  # frame's alpha back in, or the corners ship opaque.
  # The Dock shows through the light tile. Arc ships 0.55, but that goes gray
  # over a dark Dock, and a dark tile goes muddy over a light one.
  alpha=()
  case "$id" in *-paper) alpha=(--tile-alpha 0.85) ;; esac
  python3 ../../build/scripts/mask-icon-alpha.py "${alpha[@]}" "$tmp_dir/$id.svg.png" "$out_dir/$id.png"
  echo "rendered+masked: $out_dir/$id.png"
done

# Remove any PNG whose SVG source is gone (e.g. a retired variant) so the
# resource bundle never ships a stale asset alongside the current set.
for png in "$out_dir"/*.png; do
  id="$(basename "$png" .png)"
  if [ ! -f "$src_dir/$id.svg" ]; then
    rm "$png"
    echo "removed stale: $png"
  fi
done
