#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
set -euo pipefail
# Regression guard: launch Atrium detached, wait, and assert it is
# still running. Guards the real risk - that something one day genuinely
# starts dropping the last keep-alive - without being defeated by an agent
# harness SIGTERM-ing its own process group (run-atrium.sh detaches into a
# new session for exactly that reason; see its header comment).
#
# Needs a build already present (`make ui` + `make build`); this script does
# not build anything itself.
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
WAIT_SECS="${WAIT_SECS:-300}"
LAUNCH_TIMEOUT_SECS="${LAUNCH_TIMEOUT_SECS:-60}"

# Never assume a clean slate: another Atrium instance may already be running
# on this host. `pkill -x Atrium` cannot tell instances apart and has already
# killed an unrelated session during development of this guard - snapshot
# what is running BEFORE we launch, so cleanup only ever targets the PID this
# run started, never someone else's.
before_pids="$(pgrep -x Atrium || true)"

# Launch under a dedicated profile dir, not the default one. Chromium's
# single-instance behavior means a second launch against a profile that is
# ALREADY open just forwards to that instance and exits immediately - no new
# process ever appears, and this guard would fail every time a dev already
# has Atrium open on the default profile, for a reason that has nothing to
# do with detachment.
profile_dir="$(mktemp -d "${TMPDIR:-/tmp}/atrium-verify-detach.XXXXXX")"

echo "launching Atrium detached (profile: ${profile_dir})..."
"${REPO}/build/scripts/run-atrium.sh" --user-data-dir="${profile_dir}" --no-startup-window >/dev/null 2>&1 &

target_pid=""
cleanup() {
  # Sweep again rather than trusting only the pid found during detection -
  # if this exits before target_pid was ever set (e.g. the launch was still
  # starting up when the detection loop below gave up), an untracked Atrium
  # process must not be left running.
  new_pids="$(pgrep -x Atrium || true)"
  for pid in ${new_pids}; do
    if ! grep -qFx "${pid}" <<<"${before_pids}"; then
      echo "cleanup: killing pid ${pid}" >&2
      kill "${pid}" 2>/dev/null || true
    fi
  done
  rm -rf "${profile_dir}"
}
trap cleanup EXIT

# Give the launcher (dylib inject + re-sign + detach + exec) time to start
# the browser. Re-signing the bundle on every launch and a cold Chromium
# start can take well over a few seconds - do not shrink this without
# re-checking against an actual timed run.
for _ in $(seq 1 "${LAUNCH_TIMEOUT_SECS}"); do
  after_pids="$(pgrep -x Atrium || true)"
  target_pid="$(printf '%s\n' "${after_pids}" | grep -vFx -f <(printf '%s\n' "${before_pids}") | head -1 || true)"
  [ -n "${target_pid}" ] && break
  sleep 1
done

if [ -z "${target_pid}" ]; then
  echo "FAIL: Atrium did not appear within ${LAUNCH_TIMEOUT_SECS}s of launch" >&2
  exit 1
fi

echo "tracking pid ${target_pid}; sleeping ${WAIT_SECS}s"
sleep "${WAIT_SECS}"

if ! kill -0 "${target_pid}" 2>/dev/null; then
  echo "FAIL: pid ${target_pid} is gone after ${WAIT_SECS}s (keep-alive regression)" >&2
  exit 1
fi

echo "PASS: pid ${target_pid} survived ${WAIT_SECS}s unattended"
