#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Regenerate the numbered patch series from the atrium commit stack.
#
# The branch `atrium/<tag>` in $SRC is the source of truth. Dev loop:
# edit in $SRC -> commit/amend into the owning subsystem commit -> run this ->
# commit the refreshed patches in the atrium repo. Refuses to run on a dirty
# checkout so uncommitted work is never silently dropped from the export.
#
# The export excludes the owned roots: overlay/ carries those files.
#
# Pre-overlay-migration state is archived at tag atrium-archive/151.0.7922.47.
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck disable=SC1091
source ./env.sh
REPO="$(cd ../.. && pwd)"

ver() { grep "^$1=" "$REPO/build/VERSION" | cut -d= -f2; }
TAG="$(ver chromium_tag)"
COMMIT="$(ver chromium_commit)"
BRANCH="atrium/${TAG}"

OUT_PATH="$REPO/patches"

# shellcheck disable=SC1091
source ./owned-roots.sh

cd "$SRC"
if [ -n "$(git status --porcelain)" ]; then
  echo "ERROR: \$SRC has uncommitted changes. Commit them into the stack first" >&2
  echo "       (git commit --amend / --fixup into the owning subsystem commit)." >&2
  exit 1
fi
git rev-parse --verify --quiet "$BRANCH" >/dev/null || {
  echo "ERROR: branch $BRANCH not found in \$SRC." >&2; exit 1; }

series_hash() {
  find "$OUT_PATH" -maxdepth 1 -type f \( -name '0*.patch' -o -name series \) -print0 \
    | sort -z | xargs -0 shasum | shasum | cut -d' ' -f1
}

mkdir -p "$OUT_PATH"
before="$(series_hash)"
rm -f "$OUT_PATH"/0*.patch
exclude_pathspecs=()
for root in "${OWNED_ROOTS[@]}"; do
  exclude_pathspecs+=(":(exclude)$root")
done
git format-patch --zero-commit --no-signature -o "$OUT_PATH" "${COMMIT}..${BRANCH}" \
  -- "${exclude_pathspecs[@]}" >/dev/null
find "$OUT_PATH" -maxdepth 1 -name '0*.patch' -exec basename {} \; | sort > "$OUT_PATH/series"

new_tree="$(git rev-parse "${BRANCH}^{tree}")"
count="$(find "$OUT_PATH" -maxdepth 1 -name '0*.patch' | wc -l | tr -d ' ')"

# Rewrite the derived keys in build/VERSION in place. This runs unconditionally,
# even when the series below turns out unchanged, since atrium_tree can still differ.
tmp="$(mktemp)"
sed -e "s/^atrium_tree=.*/atrium_tree=${new_tree}/" \
    -e "s/^patch_count=.*/patch_count=${count}/" "$REPO/build/VERSION" > "$tmp"
mv "$tmp" "$REPO/build/VERSION"

if [ "$before" = "$(series_hash)" ]; then
  echo "patches unchanged, atrium_tree=$new_tree"
else
  echo "regenerated: $count patches, atrium_tree=$new_tree"
fi
"$REPO/build/scripts/verify-patches.sh"
