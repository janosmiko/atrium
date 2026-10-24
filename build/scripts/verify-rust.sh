#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Guard against the three copies of the atrium-core crate drifting apart:
#   - rust/atrium-core                    (Atrium repo, canonical - edit here)
#   - overlay/third_party/atrium/core     (Atrium repo, a copy - feeds overlay-sync)
#   - $SRC/third_party/atrium/core        (Chromium tree, a copy - GN builds it)
#
# The tree copy exists because GN cannot reach outside its own checkout. No
# direction of drift is loud, so fix any of it with `make rust-sync`, which
# copies the repo over the other two.
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck disable=SC1091
source ./env.sh
REPO="$(cd ../.. && pwd)"

CANON="$REPO/rust/atrium-core"
OVERLAY_COPY="$REPO/overlay/third_party/atrium/core"
SRC_COPY="$SRC/third_party/atrium/core"
EXCLUDES=(-x target -x .git -x .serena -x '*.rs.bk')

if [ ! -d "$SRC" ]; then
  echo "warning: no Chromium checkout at $SRC - skipping the atrium-core" \
       "drift check (run build/scripts/bootstrap.sh to get one)" >&2
  exit 0
fi

if [ ! -d "$OVERLAY_COPY" ]; then
  echo "DRIFT: $OVERLAY_COPY does not exist." >&2
  echo "  Run \`make rust-sync\` to create it from $CANON." >&2
  exit 1
fi

if [ ! -d "$SRC_COPY" ]; then
  echo "DRIFT: $SRC_COPY does not exist." >&2
  echo "  Run \`make rust-sync\` to create it from $CANON." >&2
  exit 1
fi

fail=0

# Ignore build output and anything the tooling drops in. Everything the crate
# needs to build is tracked, so an untracked file is never part of the answer.
if ! diff -r "${EXCLUDES[@]}" "$CANON" "$OVERLAY_COPY" >/dev/null 2>&1; then
  echo "DRIFT: the atrium-core copies differ:" >&2
  echo "  canonical: $CANON" >&2
  echo "  overlay:   $OVERLAY_COPY" >&2
  echo >&2
  diff -r "${EXCLUDES[@]}" "$CANON" "$OVERLAY_COPY" >&2 || true
  echo >&2
  fail=1
fi

if ! diff -r "${EXCLUDES[@]}" "$OVERLAY_COPY" "$SRC_COPY" >/dev/null 2>&1; then
  echo "DRIFT: the atrium-core copies differ:" >&2
  echo "  overlay: $OVERLAY_COPY" >&2
  echo "  tree:    $SRC_COPY" >&2
  echo >&2
  diff -r "${EXCLUDES[@]}" "$OVERLAY_COPY" "$SRC_COPY" >&2 || true
  echo >&2
  fail=1
fi

if [ "$fail" -ne 0 ]; then
  echo "  Edit the canonical copy, then run \`make rust-sync\`. If the change" \
       "was made in the Chromium tree instead, copy it back by hand first -" \
       "\`make rust-sync\` overwrites the other copies and would throw it away." >&2
  exit 1
fi

echo "verify OK: atrium-core copies are identical"
