#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
set -euo pipefail

# grants the current build's cdhash on the "Atrium Safe Storage" item.
#
# MANUAL USE ONLY - run-atrium.sh no longer calls this. Granting the cdhash did
# NOT stop the startup prompt (user-reported); it only added a second, terminal
# one.
#
# The startup prompt is still genuinely unanswerable: run-atrium.sh launches with
# --no-startup-window, so Atrium has no windows. The prompt is owned by a
# windowless background app, never takes activation, and clicks route to whatever
# app is actually frontmost - it renders but cannot be answered. That half held.
# What was wrong was the other half - that granting first would prevent it. It
# does not, and why is uninvestigated.
#
# What actually mismatches (read off the item, not inferred): the ACL's trusted-
# application entry already matches every build - `identifier "org.atrium.Atrium"
# and certificate leaf = H"..."`, both stable, because setup-signing-cert.sh pins
# a self-signed "Atrium Dev" identity. Only the partition_id list is keyed on
# cdhash, since a self-signed identity carries no Team ID and macOS falls back to
# cdhash when there is no stable "teamid:". Every code-changing build produces a
# cdhash the list has never seen. A Developer ID signature would retire
# this entirely by making the partition "teamid:", which does not change.
#
# So this updates the partition list; it does not remove the startup prompt.
#
# Must run BEFORE run-atrium.sh's setsid re-exec: setsid(2) leaves the new session
# with no controlling terminal, so `security` could not read the password from the
# tty and would fall back to the same unanswerable GUI dialog.
#
# Env:
#   ATRIUM_SKIP_KEYCHAIN_GRANT=1  skip entirely
#   ATRIUM_KEYCHAIN_KEEP_ALL=1    append to the existing list instead of replacing
#                                 it (adds ~25s: needs a full `security dump-keychain`)

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
# shellcheck disable=SC1091
source "${REPO}/build/scripts/env.sh"

APP="${SRC}/out/atrium/Atrium.app"
KEYCHAIN="${HOME}/Library/Keychains/login.keychain-db"
SERVICE="Atrium Safe Storage"
ACCOUNT="Atrium"
# Sits next to the bundle it describes, not in the worktree: out/atrium/Atrium.app
# is shared by every worktree, and so is the cdhash it carries.
MARKER="${SRC}/out/atrium/.atrium-keychain-cdhash"

[ "${ATRIUM_SKIP_KEYCHAIN_GRANT:-0}" = "1" ] && exit 0
# A mock keychain never touches Safe Storage, so there is nothing to grant.
[ "${ATRIUM_MOCK_KEYCHAIN:-0}" = "1" ] && exit 0
[ -d "${APP}" ] || exit 0

# 21ms. The whole point of the marker below is to avoid paying anything more
# than this on a launch that is already granted.
# Reads to EOF deliberately: an `exit` here closes the pipe early and kills
# codesign with SIGPIPE, which pipefail then turns into a failed launch step.
CDHASH="$(codesign -d --verbose=4 "${APP}" 2>&1 | awk -F= '$1 == "CDHash" {h = $2} END {print h}')"
[ -n "${CDHASH}" ] || exit 0

[ -f "${MARKER}" ] && [ "$(cat "${MARKER}")" = "${CDHASH}" ] && exit 0

# Nothing to grant until Atrium has created the item on its first real-keychain
# launch. That launch does not prompt - creating an item grants its creator.
if ! security find-generic-password -s "${SERVICE}" -a "${ACCOUNT}" "${KEYCHAIN}" >/dev/null 2>&1; then
  exit 0
fi

# `security` prompts for the login keychain password on the tty. Without one
# (make run-detached, VM scenarios, CI) that prompt has nowhere to go, so warn
# and let the launch proceed rather than hanging it.
if [ ! -t 0 ]; then
  echo "warning: new build cdhash ${CDHASH} is not granted on \"${SERVICE}\" and there is no terminal to ask on." >&2
  echo "         Atrium may raise a Safe Storage prompt it cannot show (--no-startup-window)." >&2
  echo "         Run ./build/scripts/authorize-keychain.sh from a terminal, or set ATRIUM_MOCK_KEYCHAIN=1." >&2
  exit 0
fi

# Replace rather than append: out/atrium/Atrium.app holds exactly one build, so
# older hashes authorize bundles that no longer exist. The list had reached 79
# entries, one per past Always Allow, and nothing was ever pruning it.
PARTITIONS="cdhash:${CDHASH}"
if [ "${ATRIUM_KEYCHAIN_KEEP_ALL:-0}" = "1" ]; then
  EXISTING="$(security dump-keychain -a "${KEYCHAIN}" 2>/dev/null |
    awk -v svc="\"svce\"<blob>=\"${SERVICE}\"" '
      index($0, svc) {inblock = 1}
      inblock && /description: cdhash:/ {sub(/^ *description: /, ""); print; exit}')"
  [ -n "${EXISTING}" ] && PARTITIONS="${EXISTING}, cdhash:${CDHASH}"
fi

echo "=== granting keychain access for this build (cdhash ${CDHASH}) ==="
echo "=== enter your macOS login password; this updates the partition list only ==="
if security set-generic-password-partition-list \
  -S "${PARTITIONS}" -s "${SERVICE}" -a "${ACCOUNT}" "${KEYCHAIN}" >/dev/null; then
  printf '%s' "${CDHASH}" > "${MARKER}"
  echo "=== granted ==="
else
  # A failed grant is not fatal: Atrium still launches, it just may raise the
  # prompt. Leaving the marker unwritten means the next launch retries.
  echo "warning: could not grant keychain access; Atrium may raise a Safe Storage prompt it cannot show." >&2
  echo "         Fallback: Keychain Access > \"${SERVICE}\" > Access Control (see docs/BUILD.md)." >&2
fi
