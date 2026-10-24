#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Marks the last commit that still contains the Swift arm, in BOTH repos.
#
# The hard cut deletes 60k lines and later phases
# build on top of it, so it is not revertible in practice. A tag makes the
# Swift arm recoverable by checkout if two months without a usable browser
# stops being acceptable.
#
# Run it again whenever Swift changes. The tag means "the last commit that has
# Swift in it", so it moves until the cut and then stops. Run it one last time
# immediately before the cut.
#
# Both repos are tagged because the Swift dylib and the Chromium tree that
# loads it are two halves of one build. A tag on either alone recovers nothing.
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck disable=SC1091
source ./env.sh
REPO="$(cd ../.. && pwd)"

TAG="${ATRIUM_SWIFT_TAG:-last-swift}"

die() { echo "error: $*" >&2; exit 1; }

[ -d "$SRC" ] || die "no Chromium checkout at $SRC (run build/scripts/bootstrap.sh)"
[ -d "$REPO/swift/AtriumUI" ] || die "no swift/AtriumUI - the cut has already happened, and this tag can no longer be moved"

# A tag on a dirty tree points at a commit that is not what was tested.
for dir in "$REPO" "$SRC"; do
  if [ -n "$(git -C "$dir" status --porcelain)" ]; then
    die "$dir has uncommitted changes; commit them before tagging"
  fi
done

for dir in "$REPO" "$SRC"; do
  git -C "$dir" tag -f -a "$TAG" -m "The last commit that still contains the Swift arm.

Recover with:
  git -C <this repo> checkout $TAG
See docs/BUILD.md, 'Recovering the Swift arm'." >/dev/null
  # ^{commit}: these are annotated tags, so a bare rev-parse prints the tag
  # object's hash and not the commit anyone would look for in a log.
  printf '%s  %s  %s\n' "$TAG" \
    "$(git -C "$dir" rev-parse --short "${TAG}^{commit}")" "$dir"
done

echo
echo "Tagged. Neither tag is pushed - this is a local recovery point."
echo "Run this again after any Swift change, and once more just before the cut."
