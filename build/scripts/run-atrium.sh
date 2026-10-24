#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
set -euo pipefail

# run everything below in a new session/process group, detached
# from the caller's. Without this, the browser stays in the CALLER's process
# group - a SIGTERM to that group (e.g. an agent tool call that times out and
# has its whole shell tree reaped) reaches Atrium too, and Chromium's stock
# POSIX handler (InstallShutdownSignalHandlers -> chrome::AttemptExit())
# turns that into an ordinary, orderly quit - indistinguishable from a real
# one. That once looked like Atrium "exiting on its own". macOS
# ships no setsid(1), so re-exec through a python3 shim that calls
# os.setsid() before handing back to this same script. Detaching this early
# also pulls the `tee` pipeline below into the new group, so it survives a
# caller-group SIGTERM instead of dying and truncating the log.
if [ -z "${_ATRIUM_DETACHED:-}" ]; then
  # No cdhash grant runs here: it asks for the login
  # password on the terminal in order to PREVENT a Safe Storage prompt, and
  # Atrium asked for the password anyway afterwards. Paying one prompt to avoid
  # another that still arrives is worse than paying neither, so the grant is
  # gone rather than merely quieted.
  #
  # build/scripts/authorize-keychain.sh is still there and can be run by hand if the
  # partition list ever needs updating. The real fix is a Developer
  # ID signature, which makes the partition "teamid:" - stable across builds -
  # instead of the per-build cdhash a self-signed identity forces.
  export _ATRIUM_DETACHED=1
  # Forked first: an interactive shell makes its foreground job a process
  # group leader, and setsid() refuses a leader with EPERM. The parent waits
  # so the terminal stays attached to the run.
  exec python3 -c '
import os, sys
pid = os.fork()
if pid:
    _, status = os.waitpid(pid, 0)
    sys.exit(os.waitstatus_to_exitcode(status))
os.setsid()
os.execvp(sys.argv[1], sys.argv[1:])
' "$0" "$@"
fi

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
# shellcheck disable=SC1091
source "${REPO}/build/scripts/env.sh"
# A cross-profile space switch always swaps the incoming
# profile's content into the one already-visible window per slot - no second
# NSWindow is ever ordered on screen, avoiding the "jump" a tiling WM
# (AeroSpace) would otherwise cause by tiling a transient second window.
# capture the run. A crash here left no artifact on disk at all -
# the build ships chrome_crashpad_handler, whose exception handler suppresses
# macOS's own ReportCrash .ips, and no Crashpad database is configured, so
# DiagnosticReports stayed empty for a confirmed crash and the only evidence
# was terminal scrollback. Chromium's StackDumpSignalHandler DOES print a
# fully symbolized trace to stderr, so teeing that is enough to diagnose;
# --enable-crash-reporter was considered and rejected - that switch does not
# exist in this tree (not in components/crash nor content_switches), so
# passing it would be a silently ignored no-op.
# Keeps the previous run: a crash is normally diagnosed after relaunching.
LOG="${REPO}/build/atrium-run.log"
# Only rotate when no instance is already running. Chromium's singleton makes a
# second launch print "Opening in existing browser session" and exit, but the
# ROTATION still happened - which renamed the log out from under the live
# process (it keeps writing to the moved file by fd) and left the new log with
# two useless lines.
if ! pgrep -f "Atrium.app/Contents/MacOS/Atrium" >/dev/null 2>&1; then
  [ -f "${LOG}" ] && mv -f "${LOG}" "${LOG}.prev"
fi
# dev launches default to the REAL keychain now, not a mock one.
# A mock keychain kills the once-per-build Safe Storage
# prompt (see the cdhash explanation below, still accurate) - but OSCrypt
# derives its encryption key from whichever keychain is in play, so a launch
# via this script (mock key) and a Finder/Dock/`open -a` launch (always the
# real key, since only this script can pass --use-mock-keychain) were two
# launch paths permanently fighting over the OSCrypt key. Chromium's tracked-
# preference enforcement (PrefHashStoreImpl::CheckValueInternal) gives the
# ENCRYPTED hash of a protected pref absolute priority over the legacy MAC,
# so every time a launch's key didn't match the previous launch's, it silently
# wiped every protected pref with a value - including
# default_search_provider_data.template_url_data, which is why the first-run
# search-engine picker kept reappearing no matter how many times it was
# answered. Unifying on one real key stops that reset from firing. The user
# has explicitly accepted the reintroduced cost below in exchange for that.
#
# Why the prompt happens at all (securityd's own words, not a guess): the
# "Atrium Safe Storage" item's ACL is partitioned by CDHASH, because a self-
# signed "Atrium Dev" identity carries no Team ID and macOS falls back to
# cdhash when there is no stable "teamid:" to key on. Every build that
# changes the app's code produces a new cdhash, which is not in the item's
# partition list, so the next launch prompts and the granted hash is
# appended. That is the whole of the "random" in the original report: it
# prompts exactly once after each code-changing build and never in between.
# The proper fix is a Developer ID signature, whose stable "teamid:" partition
# survives every rebuild, and it retires the need for
# the prompt entirely (and thus this whole trade-off).
#
# Set ATRIUM_MOCK_KEYCHAIN=1 to deliberately opt back into the mock
# keychain (no prompt, but cookies/passwords written under the real key won't
# decrypt, and alternating this flag between launches reproduces the
# pref-wiping bug on purpose) - useful for a fast UI-only loop that doesn't
# need working sessions or persisted settings.
KEYCHAIN_FLAGS=()
KEYCHAIN_NOTE="REAL keychain (the default; expect one Safe Storage prompt after a code-changing build until a Developer ID signature lands)"
if [ "${ATRIUM_MOCK_KEYCHAIN:-0}" = "1" ]; then
  KEYCHAIN_FLAGS=(--use-mock-keychain)
  KEYCHAIN_NOTE="mock keychain (ATRIUM_MOCK_KEYCHAIN=1; no Safe Storage prompt, but sessions/passwords will not decrypt and protected prefs may get wiped if this differs from the previous launch's mode)"
fi
{
  echo "=== atrium run $(date '+%Y-%m-%d %H:%M:%S') ==="
  echo "=== ${KEYCHAIN_NOTE} ==="
  "${SRC}/out/atrium/Atrium.app/Contents/MacOS/Atrium" \
    --user-data-dir="${HOME}/Library/Application Support/Atrium/Chromium" \
    ${KEYCHAIN_FLAGS[@]+"${KEYCHAIN_FLAGS[@]}"} \
    --no-startup-window --no-first-run --no-default-browser-check "$@" 2>&1
} | tee "${LOG}"
# pipefail (set at the top) makes this the app's status, not tee's.
exit "${PIPESTATUS[0]}"
