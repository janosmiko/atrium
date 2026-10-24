#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# How long does the deployed Atrium stay up in the VM with nobody touching it?
#
# One number, in seconds, from a launch and a poll. No scenario, no
# fixture, no assertions - which is the point: when the question is "is the app
# even alive", every one of those is a way for the answer to arrive as
# somebody else's failed assertion.
#
# Written after a UI-thread crash killed the app three seconds after launch and
# spent an hour looking like a broken scenario harness. This said "GONE after
# ~3s" in four seconds, and "STILL ALIVE after 148s" once it was reverted.
#
#   make vm-lifetime              watch for up to 120s
#   WATCH_SECONDS=300 make vm-lifetime
#
# Needs a running VM with a deployed build (make vm-start && make vm-deploy).
set -uo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
# shellcheck disable=SC1091
source "${REPO}/build/scripts/env.sh"
export TART_HOME="${TART_HOME:-/Volumes/AtriumBuild/tart}"

VM="${VM_NAME:-atrium-probe}"
WATCH="${WATCH_SECONDS:-120}"
APP="Atrium"

ip="$("${REPO}/build/scripts/vm.sh" ip 2>/dev/null | tail -1)"
[ -n "${ip}" ] || { echo "error: no VM ip - is it running? (make vm-start)" >&2; exit 1; }

ssh_opts=(-i "${TART_HOME}/${VM}-key" -o StrictHostKeyChecking=no
          -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=5)
# shellcheck disable=SC2029  # intentional client-side expansion
vm_ssh() { ssh "${ssh_opts[@]}" "admin@${ip}" "$@" </dev/null; }

vm_ssh "pkill -x ${APP} 2>/dev/null; true" >/dev/null 2>&1
"${REPO}/build/scripts/vm.sh" run-atrium >/dev/null 2>&1 &

start=$SECONDS
alive=0
while [ $(( SECONDS - start )) -lt "${WATCH}" ]; do
  if vm_ssh "pgrep -x ${APP} >/dev/null" 2>/dev/null; then
    alive=$(( SECONDS - start ))
  elif [ "${alive}" -gt 0 ]; then
    # Seen alive, now gone. That is the answer.
    echo "GONE after ~${alive}s alive (noticed at $(( SECONDS - start ))s)"
    echo "run log: vm_ssh 'tail -40 /tmp/atrium-run.log' - a FATAL line names the cause"
    exit 1
  fi
  sleep 1
done

if [ "${alive}" -eq 0 ]; then
  echo "NEVER STARTED within ${WATCH}s - check the deploy" >&2
  exit 1
fi
echo "STILL ALIVE after $(( SECONDS - start ))s"
