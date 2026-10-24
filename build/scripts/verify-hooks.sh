#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Prove the pre-push patch guard can FAIL. A guard nobody has watched fail is a
# guard nobody should trust - this project already lost six hours to an
# allowlist entry that stayed green while the fact under it changed.
#
# Drives the hook against a throwaway git worktree in a temp directory, so the
# tracked build/VERSION is never written. Skips when overlay/ is dirty: the
# worktree holds overlay/ at HEAD, and the guard compares it to the live tree.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
HOOK="${REPO}/.git/hooks/pre-push"

[ -x "${HOOK}" ] || { echo "SELFTEST FAILED: no executable ${HOOK} - run 'make hooks'" >&2; exit 1; }

if [ -n "$(git -C "${REPO}" status --porcelain overlay)" ]; then
  echo "SELFTEST SKIPPED: overlay/ has uncommitted edits, the worktree compares HEAD against the live tree"
  exit 0
fi

# The hook resolves its own repo root from cwd via `git rev-parse
# --show-toplevel`, so running it from a worktree redirects every path it
# reads (build/VERSION, patches/, rust/atrium-core) without changing the hook.
WORKTREE="$(mktemp -d)"
cleanup() { git worktree remove --force "${WORKTREE}" 2>/dev/null || rm -rf "${WORKTREE}"; }
trap cleanup EXIT
git -C "${REPO}" worktree add --detach --quiet "${WORKTREE}" HEAD
# local.env is gitignored, so without this copy env.sh falls back to the
# default CHROMIUM_DIR inside the worktree.
[ -f "${REPO}/build/scripts/local.env" ] && cp "${REPO}/build/scripts/local.env" "${WORKTREE}/build/scripts/"

# 1. It passes when the series is in step.
if ! ( cd "${WORKTREE}" && "${HOOK}" </dev/null >/dev/null 2>&1 ); then
  echo "SELFTEST FAILED: the guard refuses a tree that verify-patches accepts" >&2
  exit 1
fi

# 2. It refuses a drifted tree. Drift is induced by corrupting the RECORDED
# tree, which is what a missed regen looks like from the guard's side.
VERSION="${WORKTREE}/build/VERSION"
# Rewrite via a temp file rather than sed -i: the in-place flag differs between
# BSD and GNU sed, and a guard's own selftest must not be the thing that is
# platform-specific.
awk '{ if ($0 ~ /^atrium_tree=/) print "atrium_tree=0000000000000000000000000000000000000000"; else print }' \
  "${VERSION}" > "${VERSION}.tmp"
mv "${VERSION}.tmp" "${VERSION}"
if ( cd "${WORKTREE}" && "${HOOK}" </dev/null >/dev/null 2>&1 ); then
  echo "SELFTEST FAILED: the guard accepted a drifted atrium_tree" >&2
  exit 1
fi

# 3. The escape hatch works, or nobody can land an emergency fix.
if ! ( cd "${WORKTREE}" && ATRIUM_SKIP_PATCH_GUARD=1 "${HOOK}" </dev/null >/dev/null 2>&1 ); then
  echo "SELFTEST FAILED: ATRIUM_SKIP_PATCH_GUARD did not bypass the guard" >&2
  exit 1
fi

# 4. It refuses overlay drift, the export-not-run-through-patches failure
# overlay-verify.sh exists to catch.
OWNED_FILE="${WORKTREE}/overlay/chrome/browser/ui/atrium/atrium_feature_check.h"
echo "// selftest drift marker" >> "${OWNED_FILE}"
if ( cd "${WORKTREE}" && "${HOOK}" </dev/null >/dev/null 2>&1 ); then
  echo "SELFTEST FAILED: the guard accepted a drifted overlay file" >&2
  exit 1
fi
git -C "${WORKTREE}" show HEAD:overlay/chrome/browser/ui/atrium/atrium_feature_check.h > "${OWNED_FILE}"

echo "hooks-selftest OK: the guard passes in step, refuses a drifted tree, refuses overlay drift, and honours the override"
