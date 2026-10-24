#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# KEPT MANUAL TOOL. Nothing runs this and nothing should: it serves
# the lab beside the implementation so a person can compare the two. Its output
# is a pair of things to look at, not a verdict.
#
# Serving the lab to Atrium gives both sides one renderer, size and font set,
# so a surviving difference is behaviour rather than text shaping.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
REPO="$(dirname "${REPO}")"
PAIR="${1:-labcmp}"
OUT="${REPO}/build/vm-fixtures/results/lab-compare-$(date +%Y%m%d-%H%M%S)"

command -v ffmpeg >/dev/null || { echo "lab-compare: ffmpeg is required" >&2; exit 1; }

# Staged rather than committed: the lab lives in docs/ and one copy is the
# source of truth. A stale duplicate under vm-fixtures would be compared
# against without anyone noticing it had drifted.
LAB_SRC="${REPO}/docs/lab/swipe-lab.html"
LAB_DST="${REPO}/build/vm-fixtures/swipe-lab.html"
trap 'rm -f "${LAB_DST}"' EXIT
cp "${LAB_SRC}" "${LAB_DST}"
echo "lab-compare: staged $(wc -c < "${LAB_DST}" | tr -d ' ') bytes of lab for the VM"

mkdir -p "${OUT}"

run_half() {
  local scenario="$1"
  echo "lab-compare: running ${scenario}" >&2
  # The scenario runner exits non-zero when an assertion fails. Both halves
  # assert only that a window rendered, so a failure here means the capture is
  # empty and there is nothing to compare - worth stopping for.
  TART_HOME="${TART_HOME:-$HOME/.tart}" make -C "${REPO}" vm-scenario \
      SCENARIO="${scenario}" >"${OUT}/${scenario}.log" 2>&1 || {
    echo "lab-compare: ${scenario} failed, see ${OUT}/${scenario}.log" >&2
    return 1
  }
  find "${REPO}/build/vm-fixtures/results" -maxdepth 1 -type d \
      -name "${scenario}-*" -print0 | xargs -0 ls -dt | head -1
}

LAB_DIR="$(run_half "${PAIR}-lab")"
IMPL_DIR="$(run_half "${PAIR}-impl")"

pick_last_frame() {
  find "$1/frames" -name "*.png" -path "*rest*" 2>/dev/null | sort | tail -1
}

LAB_FRAME="$(pick_last_frame "${LAB_DIR}")"
IMPL_FRAME="$(pick_last_frame "${IMPL_DIR}")"
[ -n "${LAB_FRAME}" ] && [ -n "${IMPL_FRAME}" ] || {
  echo "lab-compare: one side captured no frames" >&2
  exit 1
}

# A red divider rather than captions: this ffmpeg has no drawtext filter, and
# the reader still needs to see where one side ends. Lab is always LEFT.
ffmpeg -y -loglevel error -i "${LAB_FRAME}" -i "${IMPL_FRAME}" \
  -filter_complex "[0:v]pad=iw+8:ih:0:0:color=red[a];[a][1:v]hstack=inputs=2" \
  "${OUT}/side-by-side.png"

# The whole capture, not just its last frame, because timing is the half a
# still cannot show - when a row settles, whether a surface paints late.
seq_dir() { find "$1/frames" -type d -name "*rest*" | head -1; }
LAB_SEQ="$(seq_dir "${LAB_DIR}")"
IMPL_SEQ="$(seq_dir "${IMPL_DIR}")"
if [ -n "${LAB_SEQ}" ] && [ -n "${IMPL_SEQ}" ]; then
  ffmpeg -y -loglevel error -framerate 10 -pattern_type glob \
    -i "${LAB_SEQ}/*.png" -framerate 10 -pattern_type glob -i "${IMPL_SEQ}/*.png" \
    -filter_complex "[0:v]pad=iw+8:ih:0:0:color=red[a];[a][1:v]hstack=inputs=2,format=yuv420p" \
    -r 10 "${OUT}/side-by-side.mp4" 2>/dev/null \
    && echo "lab-compare: wrote ${OUT}/side-by-side.mp4" \
    || echo "lab-compare: mp4 skipped, the two captures hold different frame counts"
fi

echo "lab-compare: wrote ${OUT}/side-by-side.png"
echo "lab-compare: lab  frame ${LAB_FRAME}"
echo "lab-compare: impl frame ${IMPL_FRAME}"
