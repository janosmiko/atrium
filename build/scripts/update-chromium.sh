#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Track Chromium stable: rebase the atrium commit stack onto a newer release.
#
# Subcommands (run in order; conflict resolution + build are manual between
# `start` and `finish`):
#   check           - is a newer stable available? (ChromiumDash, read-only)
#   start <newtag>  - fetch the tag, branch, rebase the stack onto it
#   sync            - gclient sync to the new DEPS (after rebase succeeds)
#   finish <newtag> - re-export patches, bump build/VERSION, verify
#
# What is automatable: check, fetch, branch, rebase attempt, sync, re-export,
# verify, VERSION bump. Inherently manual: rebase conflict resolution and any
# compile-error fixes from upstream API drift (amend into the owning subsystem
# commit via `git commit --fixup=<sha>` + `git rebase -i --autosquash`).
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck disable=SC1091
source ./env.sh
REPO="$(cd ../.. && pwd)"

ver() { grep "^$1=" "$REPO/build/VERSION" | cut -d= -f2; }
CUR_TAG="$(ver chromium_tag)"
CUR_COMMIT="$(ver chromium_commit)"

# Latest stable for macOS, via ChromiumDash. Prints "version<TAB>commit".
latest_stable() {
  local url="https://chromiumdash.appspot.com/fetch_releases?channel=Stable&platform=Mac&num=1"
  curl -fsSL "$url" | python3 -c '
import json,sys
r=json.load(sys.stdin)
if not r: sys.exit("no release data")
print(r[0]["version"]+"\t"+r[0]["hashes"]["chromium"])'
}

cmd="${1:-check}"
case "$cmd" in
  check)
    echo "checked: $(date '+%Y-%m-%d %H:%M')"
    read -r NEW_VER NEW_COMMIT < <(latest_stable)
    echo "current pinned stable: $CUR_TAG"
    echo "latest stable (Mac):   $NEW_VER"
    if [ "$NEW_VER" = "$CUR_TAG" ]; then
      echo "up to date."
      exit 0
    fi
    cur_milestone="${CUR_TAG%%.*}"; new_milestone="${NEW_VER%%.*}"
    if [ "$cur_milestone" = "$new_milestone" ]; then
      echo "UPDATE AVAILABLE (same-milestone $cur_milestone -> low risk):"
    else
      echo "UPDATE AVAILABLE (milestone jump $cur_milestone -> $new_milestone -> budget a day):"
    fi
    echo "  ./build/scripts/update-chromium.sh start $NEW_VER"
    exit 10   # signal "update available" for CI/cron
    ;;

  start)
    NEW="${2:?usage: update-chromium.sh start <newtag>}"
    echo "== preflight =="
    "$REPO/build/scripts/verify-patches.sh"           # no drift enters the rebase
    cd "$SRC"
    [ -z "$(git status --porcelain)" ] || { echo "ERROR: \$SRC not clean" >&2; exit 1; }
    echo "== fetch $NEW =="
    git fetch origin "refs/tags/${NEW}:refs/tags/${NEW}" --no-tags
    git config rerere.enabled true            # replay conflict resolutions across bumps
    echo "== rebase atrium/$CUR_TAG -> atrium/$NEW onto $NEW =="
    git checkout -b "atrium/${NEW}" "atrium/${CUR_TAG}"
    if git rebase --onto "$NEW" "$CUR_COMMIT" "atrium/${NEW}"; then
      echo "rebase clean. Next: ./build/scripts/update-chromium.sh sync"
    else
      echo "CONFLICT. Resolve, then 'git -C \"$SRC\" rebase --continue' (repeat)." >&2
      echo "Abort with 'git -C \"$SRC\" rebase --abort' (atrium/$CUR_TAG is untouched)." >&2
      exit 1
    fi
    ;;

  sync)
    cd "$SRC"
    echo "== gclient sync to new DEPS =="
    gclient sync -D
    gclient runhooks
    echo "synced. Next: build + test (make build; make test), then finish."
    ;;

  finish)
    NEW="${2:?usage: update-chromium.sh finish <newtag>}"
    cd "$SRC"
    NEW_COMMIT="$(git rev-parse "${NEW}^{commit}")"
    NEW_TREE="$(git rev-parse "atrium/${NEW}^{tree}")"
    # Rewrite the pin, then re-export from the new branch.
    tmp="$(mktemp)"
    sed -e "s/^chromium_tag=.*/chromium_tag=${NEW}/" \
        -e "s/^chromium_commit=.*/chromium_commit=${NEW_COMMIT}/" \
        -e "s/^atrium_tree=.*/atrium_tree=${NEW_TREE}/" "$REPO/build/VERSION" > "$tmp"
    mv "$tmp" "$REPO/build/VERSION"
    "$REPO/build/scripts/regen-patches.sh"            # re-exports + verifies against the new pin
    echo "bumped to $NEW. Commit in the atrium repo:"
    echo "  git add build/VERSION patches && git commit -m 'chore: bump chromium to $NEW'"
    ;;

  *)
    echo "usage: update-chromium.sh {check|start <tag>|sync|finish <tag>}" >&2
    exit 2
    ;;
esac
