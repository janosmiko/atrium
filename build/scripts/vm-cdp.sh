#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Forwards the probe VM's DevTools port to this machine, exactly once.
#
#   ./build/scripts/vm-cdp.sh          # forward 9334 (the VM default)
#   ./build/scripts/vm-cdp.sh stop     # tear it down
#
# Hand-rolled `ssh -L` lines cost three separate confusions in one day: a
# tunnel left open on 9333 made a LOCAL Atrium fail to bind its own devtools
# server so every reading described the VM instead, and a stale tunnel mapping
# 9334 -> 9333 answered nothing at all while still holding the port. Both
# looked like product bugs. This kills whatever holds the port first, then
# forwards, and fails loudly rather than silently listening on a dead route.
set -euo pipefail

REPO_BUILD="$(cd "$(dirname "$0")" && pwd)"
PORT="${ATRIUM_CDP_PORT:-9334}"
TART_HOME="${TART_HOME:-$HOME/.tart}"
SSH_KEY="${TART_HOME}/atrium-probe-key"
SSH_USER="admin"

kill_holders() {
  # By port, not by command pattern: the pattern is exactly what went wrong -
  # a tunnel with a mismatched mapping does not match the string you expect.
  local pids
  pids="$(lsof -nP -iTCP:"${PORT}" -sTCP:LISTEN -t 2>/dev/null || true)"
  if [ -n "${pids}" ]; then
    echo "vm-cdp: freeing port ${PORT} (pids: ${pids//$'\n'/ })"
    # shellcheck disable=SC2086
    kill ${pids} 2>/dev/null || true
    sleep 1
  fi
}

if [ "${1:-}" = "stop" ]; then
  kill_holders
  echo "vm-cdp: port ${PORT} free"
  exit 0
fi

ip="$("${REPO_BUILD}/vm.sh" ip)"
kill_holders

# stdout and stderr go to a file, not inherited: a backgrounded child holding
# the caller's stdout keeps the calling shell waiting even after this script
# exits, which reads as the script hanging.
ssh -i "${SSH_KEY}" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
  -o LogLevel=ERROR -o ExitOnForwardFailure=yes \
  -N -L "127.0.0.1:${PORT}:127.0.0.1:${PORT}" "${SSH_USER}@${ip}" \
  >/tmp/atrium-vm-cdp.log 2>&1 &
tunnel=$!
disown "${tunnel}" 2>/dev/null || true

# Prove the route before claiming success. ExitOnForwardFailure catches a busy
# local port; only a real request catches a tunnel pointed somewhere useless.
for _ in $(seq 1 10); do
  sleep 1
  if curl -s --max-time 3 "http://127.0.0.1:${PORT}/json/version" >/dev/null 2>&1; then
    echo "vm-cdp: ${ip}:${PORT} -> 127.0.0.1:${PORT} (pid ${tunnel})"
    exit 0
  fi
done

kill "${tunnel}" 2>/dev/null || true
echo "vm-cdp: no DevTools answered on ${PORT}. Is Atrium running in the VM with" >&2
echo "        --remote-debugging-port=${PORT}?" >&2
exit 1
