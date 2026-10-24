#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# build/scripts/vm-lock.sh - per-VM_NAME lock for vm-scenario.sh.
#
# A directory under ${VM_LOCK_ROOT:-$TMPDIR/atrium-vm-locks}/<vm>.lock, keyed
# on the resolved VM name. A second runner WAITS (VM_LOCK_WAIT_SECS, default
# 1800) rather than failing outright, for a queued CI/loop caller. A held
# lock counts as live only when its pid is running and its recorded start
# time still matches - a killed run's pid, recycled by an unrelated process,
# reads as stale. Any vm_lock_acquire caller counts as a holder, not only
# vm-scenario.sh.
#
# Contract - the sourcing script must define before sourcing:
#   log <msg>   die <msg> (used only past the wait bound)

vm_lock_root() {
  printf '%s' "${VM_LOCK_ROOT:-${TMPDIR:-/tmp}/atrium-vm-locks}"
}

# Checked against this ONE pid via `ps -p`, never a table-wide `pgrep -f` -
# that would match the waiting shell's own command line too.
vm_lock_holder_alive() {
  local pid="$1" recorded="$2"
  kill -0 "${pid}" 2>/dev/null || return 1
  [ -n "${recorded}" ] || return 1
  local current
  current="$(ps -o lstart= -p "${pid}" 2>/dev/null || true)"
  [ "${current}" = "${recorded}" ]
}

# Pair with a caller-side `trap 'vm_lock_release "<vm>"' EXIT` so the lock is
# freed on every exit path, including an internal `die`.
vm_lock_acquire() {
  local vm="$1"
  local dir; dir="$(vm_lock_root)/${vm}.lock"
  local pid_file="${dir}/pid"
  local start_file="${dir}/start"
  local wait_bound="${VM_LOCK_WAIT_SECS:-1800}"
  mkdir -p "$(vm_lock_root)"

  local waited=0 announced=0 holder holder_start
  while true; do
    if mkdir "${dir}" 2>/dev/null; then
      # $BASHPID, not $$: $$ keeps naming the top-level script even from
      # inside the caller's subshell wrapper, so a kill aimed at just that
      # subshell worker would leave a lock recording a pid that is still
      # alive, and the lock would never look stale.
      printf '%s\n' "${BASHPID}" >"${pid_file}"
      # The pid's own start time - survives an exec() by the same process,
      # unlike a script-name check, so it works for any vm_lock_acquire
      # caller.
      ps -o lstart= -p "${BASHPID}" >"${start_file}" 2>/dev/null || true
      VM_LOCK_HELD_DIR="${dir}"
      log "holding VM lock for ${vm} (pid ${BASHPID}, ${dir})"
      return 0
    fi

    holder="$(cat "${pid_file}" 2>/dev/null || true)"
    if [ -z "${holder}" ]; then
      # Another acquirer is mid-mkdir, between creating the directory and
      # writing its pid - not stale, just not finished claiming it yet.
      sleep 1
      continue
    fi
    holder_start="$(cat "${start_file}" 2>/dev/null || true)"

    if vm_lock_holder_alive "${holder}" "${holder_start}"; then
      if [ "${announced}" -eq 0 ]; then
        log "waiting for the ${vm} VM lock - held by pid ${holder} (another VM run in progress), will wait up to ${wait_bound}s"
        announced=1
      fi
      if [ "${waited}" -ge "${wait_bound}" ]; then
        die "gave up waiting for the ${vm} VM lock after ${wait_bound}s - pid ${holder} still holds it (ps -p ${holder} to inspect, or rm -rf ${dir} if it is gone)"
      fi
      sleep 5
      waited=$(( waited + 5 ))
      continue
    fi

    log "reclaiming stale ${vm} VM lock at ${dir} - pid ${holder} is not a live holder of it"
    rm -rf "${dir}"
  done
}

# vm_lock_release <vm> - no-op unless this process holds the lock it names,
# so a caller can call it unconditionally on every exit path.
vm_lock_release() {
  local vm="$1"
  local dir; dir="$(vm_lock_root)/${vm}.lock"
  [ "${VM_LOCK_HELD_DIR:-}" = "${dir}" ] || return 0
  rm -rf "${dir}"
  VM_LOCK_HELD_DIR=""
}
