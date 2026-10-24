#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Verify the committed patch series is consistent - the drift guard.
#
# Asserts, using side-effect-free temp indexes (never touches $SRC's real
# index or working tree):
#   OVERLAY_TREE - series (the upstream half) applied to the pinned base, then
#                 overlay/ staged on top
#   BRANCH_TREE - the atrium/<tag> branch tip tree (if the branch exists)
#   WT_TREE     - the live working tree (only when the atrium branch is the
#                 checked-out HEAD; catches uncommitted edits not yet exported)
# all equal atrium_tree in build/VERSION. A WT mismatch is exactly the
# "working tree has atrium changes not in the patches" failure this guards.
#
# Also runs verify-rust.sh: the two atrium-core crate copies (repo and Chromium
# tree) must stay identical - a separate drift this script is well placed to
# catch since it already asserts the tree hasn't drifted.
#
# It used to run verify-abi.sh too, for the two atrium_bridge.h copies. The
# Swift copy went with the cut, so there is nothing left to
# compare.
set -euo pipefail

# Fails if any patch under patches_dir touches a path in $OWNED_ROOTS. The
# tree hash comparison below can't catch a path that lives in a patch AND in
# overlay/, since a byte-identical file staged twice still hashes the same.
check_both_halves() {
  local patches_dir="$1"
  local fail=0
  local p header a_path b_path path root
  for p in "$patches_dir"/0*.patch; do
    [ -e "$p" ] || continue
    while IFS= read -r header; do
      # git quotes the header (diff --git "a/x y" "b/x y") when a path holds a
      # space or another char the plain a/... b/... form can't carry.
      if [[ "$header" =~ ^diff\ --git\ \"a/(.*)\"\ \"b/(.*)\"$ ]]; then
        a_path="${BASH_REMATCH[1]}"
        b_path="${BASH_REMATCH[2]}"
      elif [[ "$header" =~ ^diff\ --git\ a/(.*)\ b/(.*)$ ]]; then
        a_path="${BASH_REMATCH[1]}"
        b_path="${BASH_REMATCH[2]}"
      else
        continue
      fi
      while IFS= read -r path; do
        [ -z "$path" ] && continue
        for root in "${OWNED_ROOTS[@]}"; do
          if [ "$path" = "$root" ] || [ "${path#"$root"/}" != "$path" ]; then
            echo "BOTH: $(basename "$p"): $path is under an owned root, it belongs in overlay/" >&2
            fail=1
          fi
        done
      done < <(printf '%s\n%s\n' "$a_path" "$b_path" | sort -u)
    done < <(grep -E '^diff --git (a/|"a/)' "$p")
  done
  return "$fail"
}

# Guards the real run so the test can source this file for check_both_halves
# without triggering a full verify.
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then

cd "$(dirname "$0")"
# shellcheck disable=SC1091
source ./env.sh
# shellcheck disable=SC1091
source ./owned-roots.sh
REPO="$(cd ../.. && pwd)"

ver() { grep "^$1=" "$REPO/build/VERSION" | cut -d= -f2; }
TAG="$(ver chromium_tag)"
COMMIT="$(ver chromium_commit)"
WANT_TREE="$(ver atrium_tree)"
BRANCH="atrium/${TAG}"

fail=0

# Rust core drift: the two atrium-core crate copies must stay identical.
if ! "$REPO/build/scripts/verify-rust.sh"; then
  fail=1
fi

# Overlay drift: overlay/ and the owned roots in the Chromium tree must match.
if ! "$REPO/build/scripts/overlay-verify.sh"; then
  fail=1
fi

cd "$SRC"

# Scratch index: series applied to the pinned base. overlay/ is staged on
# top below to produce OVERLAY_TREE.
idx="$(mktemp)"; err="$(mktemp)"
ovl_paths="$(mktemp)"; ovl_shas="$(mktemp)"; ovl_objdir="$(mktemp -d)"
trap 'rm -f "$idx" "$err" "$ovl_paths" "$ovl_shas"; rm -rf "$ovl_objdir"' EXIT
GIT_INDEX_FILE="$idx" git read-tree "$COMMIT"
for p in "$REPO"/patches/0*.patch; do
  # An amend can leave a diffless commit in the stack, which git apply rejects
  # as "no valid patches". The tree comparison below is what proves the series.
  if ! GIT_INDEX_FILE="$idx" git apply --cached --allow-empty "$p" 2>"$err"; then
    echo "ERROR: patch does not apply cleanly to base: $(basename "$p")" >&2
    cat "$err" >&2; exit 1
  fi
done
if ! check_both_halves "$REPO/patches"; then
  fail=1
fi

# OVERLAY_TREE: every overlay file staged into the same scratch index at its
# destination path, on top of the applied series.
(cd "$REPO/overlay" && git ls-files --cached --others --exclude-standard -- "${OWNED_ROOTS[@]}" \
  | while IFS= read -r p; do if [ -f "$p" ]; then printf '%s\n' "$p"; fi; done) | sort > "$ovl_paths"

# New blobs and trees go to a throwaway dir. $SRC/.git only lends objects for
# reads, so this staging leaves no loose objects behind.
export GIT_OBJECT_DIRECTORY="$ovl_objdir"
export GIT_ALTERNATE_OBJECT_DIRECTORIES="$SRC/.git/objects"
# --stdin-paths resolves each path from the repo root and ignores the cwd, so
# the list has to carry the overlay/ prefix here and nowhere else.
sed 's|^|overlay/|' "$ovl_paths" | git -C "$REPO" hash-object -w --no-filters --stdin-paths > "$ovl_shas"
while IFS= read -r path && IFS= read -r sha <&3; do
  mode=100644
  [ -x "$REPO/overlay/$path" ] && mode=100755
  GIT_INDEX_FILE="$idx" git update-index --add --cacheinfo "$mode,$sha,$path"
done < "$ovl_paths" 3< "$ovl_shas"
OVERLAY_TREE="$(GIT_INDEX_FILE="$idx" git write-tree)"
if [ "$OVERLAY_TREE" != "$WANT_TREE" ]; then
  echo "DRIFT: series + overlay tree ($OVERLAY_TREE) != recorded atrium_tree ($WANT_TREE) -" >&2
  echo "       Offending paths:" >&2
  git diff-tree -r --name-status "$WANT_TREE" "$OVERLAY_TREE" | sed 's/^/         /' >&2
  fail=1
fi
unset GIT_OBJECT_DIRECTORY GIT_ALTERNATE_OBJECT_DIRECTORIES

# BRANCH_TREE (if present).
if git rev-parse --verify --quiet "$BRANCH" >/dev/null; then
  BRANCH_TREE="$(git rev-parse "${BRANCH}^{tree}")"
  if [ "$BRANCH_TREE" != "$WANT_TREE" ]; then
    echo "DRIFT: branch $BRANCH tree ($BRANCH_TREE) != recorded atrium_tree ($WANT_TREE)" >&2
    fail=1
  fi
fi

# WT_TREE: only meaningful when the atrium branch is checked out. Captures the
# live working tree (tracked + untracked-unignored) without disturbing state.
if [ "$(git symbolic-ref --quiet --short HEAD 2>/dev/null || true)" = "$BRANCH" ]; then
  widx="$(mktemp)"
  GIT_INDEX_FILE="$widx" git read-tree "$COMMIT"
  GIT_INDEX_FILE="$widx" git add -A
  WT_TREE="$(GIT_INDEX_FILE="$widx" git write-tree)"
  rm -f "$widx"
  if [ "$WT_TREE" != "$WANT_TREE" ]; then
    echo "DRIFT: working tree ($WT_TREE) != recorded atrium_tree ($WANT_TREE) -" >&2
    echo "       uncaptured edits. Offending paths:" >&2
    git diff-tree -r --name-status "$WANT_TREE" "$WT_TREE" | sed 's/^/         /' >&2
    fail=1
  fi
fi

if [ "$fail" -eq 0 ]; then
  echo "verify OK: patch series + overlay == branch == working tree == atrium_tree ($WANT_TREE)"
  echo "verify OK: overlay staged, $(wc -l < "$ovl_paths" | tr -d ' ') files"
else
  exit 1
fi

fi
