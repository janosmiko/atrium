#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Apply the Atrium patch series to a clean Chromium checkout.
#
# Reconstructs the `atrium/<tag>` commit stack via `git am`, then copies the
# owned roots in from overlay/ and commits them as one more commit on the
# branch, since the series excludes them. The final tree-hash assert catches
# a drifted base or bad patch instead of silently producing a different binary.
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck disable=SC1091
source ./env.sh
REPO="$(cd ../.. && pwd)"

SRC="${ATRIUM_CHROMIUM_SRC:-$SRC}"

ver() { grep "^$1=" "$REPO/build/VERSION" | cut -d= -f2; }
TAG="$(ver chromium_tag)"
COMMIT="$(ver chromium_commit)"
WANT_TREE="$(ver atrium_tree)"
BRANCH="atrium/${TAG}"

cd "$SRC"

# The checkout must sit on the exact pinned base, clean. We instruct rather
# than mutate - never destroy a user's uncommitted state.
head="$(git rev-parse HEAD)"
if [ "$head" != "$COMMIT" ]; then
  echo "ERROR: \$SRC is at $head, expected the pinned base $COMMIT ($TAG)." >&2
  echo "       Run: git -C \"$SRC\" checkout $TAG" >&2
  exit 1
fi
if [ -n "$(git status --porcelain)" ]; then
  echo "ERROR: \$SRC working tree is not clean. Commit/stash/clean it first." >&2
  exit 1
fi

# Idempotent: if the branch already reproduces the wanted tree, done.
if git rev-parse --verify --quiet "$BRANCH" >/dev/null; then
  have_tree="$(git rev-parse "${BRANCH}^{tree}")"
  if [ "$have_tree" = "$WANT_TREE" ]; then
    echo "already applied: $BRANCH is up to date (tree $WANT_TREE)"
    exit 0
  fi
  echo "ERROR: branch $BRANCH exists but its tree ($have_tree) != expected ($WANT_TREE)." >&2
  echo "       Delete it and re-run: git -C \"$SRC\" branch -D $BRANCH" >&2
  exit 1
fi

git checkout -b "$BRANCH" "$COMMIT"
if ! git am --3way "$REPO/patches"/0*.patch; then
  echo "ERROR: git am failed (conflict). Resolve and 'git -C \"$SRC\" am --continue'," >&2
  echo "       or 'git -C \"$SRC\" am --abort' to bail out." >&2
  exit 1
fi

count="$(find "$REPO/patches" -maxdepth 1 -name '0*.patch' | wc -l | tr -d ' ')"

# The series excludes the owned roots, so the overlay has to be copied in on
# top of `git am` to reach atrium_tree.
# shellcheck disable=SC1091
source "$REPO/build/scripts/owned-roots.sh"
for root in "${OWNED_ROOTS[@]}"; do
  [ -d "$REPO/overlay/$root" ] || continue
  mkdir -p "$SRC/$root"
  rsync -a --delete --exclude target --exclude .serena "$REPO/overlay/$root/" "$SRC/$root/"
done
git add -A -- "${OWNED_ROOTS[@]}"
name="$(git -C "$REPO" config user.name 2>/dev/null || true)"
email="$(git -C "$REPO" config user.email 2>/dev/null || true)"
if [ -z "$name" ] || [ -z "$email" ]; then
  echo "ERROR: set user.name and user.email in $REPO" >&2
  exit 1
fi
git -c "user.name=$name" -c "user.email=$email" \
  commit -q -m "chore: add the atrium source overlay"

got_tree="$(git rev-parse "HEAD^{tree}")"
if [ "$got_tree" != "$WANT_TREE" ]; then
  echo "ERROR: applied tree ($got_tree) != expected ($WANT_TREE). Series is inconsistent." >&2
  exit 1
fi
echo "applied: $count patches + overlay -> $BRANCH (tree $WANT_TREE)"
