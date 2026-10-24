#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# build/scripts/vm.sh - Tart VM lifecycle for GUI-probe testing.
#
# Why: agent-driven probes (CGEvent clicks, AppleScript, fullscreen windows,
# screencapture) steal focus from the host desktop. This runs them inside a
# headless macOS VM instead. Builds still happen on the host ($SRC); only
# run+probe moves to the VM. See docs/BUILD.md "VM probe environment".
#
# Assumptions:
#   - tart is installed: brew install cirruslabs/cli/tart
#   - TART_HOME defaults to ~/.tart, which is tart's own default, so the bare
#     `tart` CLI and this script see the same store. It pointed at
#     /Volumes/AtriumBuild/tart while the boot disk ran near-full, and every
#     call then needed an override because that volume is not always mounted.
#   - Base image: ghcr.io/cirruslabs/macos-tahoe-base (matches host major,
#     macOS 26 Tahoe) - ships an "admin" user with SSH enabled
#   - A dedicated ed25519 keypair is generated on first use and authorized
#     inside the VM via `tart exec` (Tart Guest Agent - no password needed),
#     so later ssh/rsync calls are passwordless
#   - VM name: atrium-probe. `snapshot` clones it to $CLEAN_VM;
#     `restore` recreates atrium-probe from that clean snapshot. The clean
#     image must carry build/vm-tools' sources under tools/src plus their
#     compiled, TCC-exercised binaries, or every run recompiles them and
#     loses the screen-recording grant - see BUILD.md
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
# shellcheck disable=SC1091
source "${REPO}/build/scripts/env.sh"

# vm-lock.sh's contract, and it has to come BEFORE the source: unshimmed, `log`
# resolves to macOS log(1), which exits non-zero on the message and takes the
# run down with it.
log() { echo "==> $*"; }
die() { echo "$*" >&2; exit 1; }
# shellcheck source=build/scripts/vm-lock.sh
source "${REPO}/build/scripts/vm-lock.sh"

export TART_HOME="${TART_HOME:-$HOME/.tart}"
VM="${VM_NAME:-atrium-probe}"
# Base image `restore` clones from. -clean-v4 carries the probe tools already
# compiled and TCC-granted under tools/, so vm-scenario.sh's reuse path fires
# instead of recompiling ungranted binaries whose first ScreenCaptureKit call
# silently yields no frames. Override to fall back to an older
# image; -clean-v3 and -clean are both kept on disk.
#
# v4 is v3 with the Swift dylib removed. v3 carried a Jul 7 stub at
# /Users/admin/atrium-probe/libAtriumUI.dylib, and rsync --delete does not
# prune loose files at the destination root, so every restore brought it back.
# It answered a whole night of scenarios for a build that no longer had Swift
# in it. Nothing can load it now, but an image that keeps a build nobody
# deploys will mislead somebody again.
CLEAN_VM="${CLEAN_VM:-${VM}-clean-v4}"
BASE_IMAGE="ghcr.io/cirruslabs/macos-tahoe-base:latest"
SSH_USER="admin"
SSH_KEY="${TART_HOME}/atrium-probe-key"
SSH_OPTS=(-i "${SSH_KEY}" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=5)
REMOTE_DIR="/Users/${SSH_USER}/atrium-probe"
FIXTURES_DIR="${REPO}/build/vm-fixtures"

usage() {
  cat <<EOF
Usage: $(basename "$0") <command>

  start        Boot the VM headless (tart run --no-graphics), wait for SSH
  stop         Stop the VM
  ip           Print the VM's IP address
  deploy       rsync Atrium.app + loose dylibs + probe fixtures into the VM
  shell        SSH into the VM
  run-atrium   Launch the deployed Atrium app inside the VM
  run-gtest    Run a gtest binary (e.g. browser_tests) in the VM, live-checkout mounted
  run-gtest-selftest  Prove run-gtest's verdict can fail (no VM needed)
  snapshot     Stop + clone ${VM} -> ${CLEAN_VM}
  restore      Delete ${VM} and re-clone it from ${CLEAN_VM}
EOF
}

vm_exists() { tart list --quiet 2>/dev/null | grep -qx "$1"; }

# tart list shows one row per VM regardless of live process count, so check
# the OS instead of vm_running. Token match, not substring: "atrium-probe" is
# a substring of the CLEAN_VM runner's argv ("atrium-probe-clean-v4").
tart_run_pids() {
  local name="$1" pid line tok candidates
  local -a tokens
  candidates="$(pgrep -f "tart run" 2>/dev/null || true)"
  [ -n "$candidates" ] || return 0
  while IFS= read -r pid; do
    [ -n "$pid" ] || continue
    line="$(ps -o args= -p "$pid" 2>/dev/null)" || continue
    read -ra tokens <<<"$line"
    for tok in "${tokens[@]}"; do
      if [ "$tok" = "$name" ]; then
        printf '%s\n' "$pid"
        break
      fi
    done
  done <<<"$candidates"
}

vm_running() {
  # The name as an ARGV entry, read via sys.argv, not spliced into the program
  # text. A name carrying a quote used to end the python string early.
  tart list --format json 2>/dev/null \
    | python3 -c 'import json,sys
d = json.load(sys.stdin)
want = sys.argv[1]
sys.exit(0 if any(v["Name"] == want and v["State"] == "running" for v in d) else 1)' "$1"
}

ensure_created() {
  if ! vm_exists "$VM"; then
    if vm_exists "$CLEAN_VM"; then
      echo "==> cloning ${CLEAN_VM} -> ${VM}"
      tart clone "$CLEAN_VM" "$VM"
    else
      echo "==> pulling base image (one-time, ~27GB compressed)"
      tart pull "$BASE_IMAGE"
      echo "==> cloning base image -> ${VM}"
      tart clone "$BASE_IMAGE" "$VM"
      tart set "$VM" --cpu 4 --memory 8192
    fi
  fi
}

ensure_ssh_key() {
  if [ ! -f "${SSH_KEY}" ]; then
    echo "==> generating dedicated SSH keypair for the probe VM"
    mkdir -p "$(dirname "${SSH_KEY}")"
    ssh-keygen -t ed25519 -N "" -f "${SSH_KEY}" -C "atrium-probe-vm" >/dev/null
  fi
}

push_ssh_key() {
  # Tart Guest Agent lets us exec without a password; use it once per boot to
  # authorize our key so later ssh/rsync calls skip the admin:admin password.
  local pubkey tries=36
  pubkey="$(cat "${SSH_KEY}.pub")"
  # Guest agent comes up well after DHCP; retry until it answers.
  until tart exec "$VM" true 2>/dev/null; do
    tries=$((tries - 1))
    if [ "$tries" -le 0 ]; then
      echo "error: Tart Guest Agent did not come up in ${VM}" >&2
      exit 1
    fi
    sleep 5
  done
  tart exec "$VM" mkdir -p "/Users/${SSH_USER}/.ssh"
  tart exec "$VM" /bin/sh -c "grep -qxF '${pubkey}' /Users/${SSH_USER}/.ssh/authorized_keys 2>/dev/null || echo '${pubkey}' >> /Users/${SSH_USER}/.ssh/authorized_keys"
  tart exec "$VM" chmod 700 "/Users/${SSH_USER}/.ssh"
  tart exec "$VM" chmod 600 "/Users/${SSH_USER}/.ssh/authorized_keys"
}

wait_for_ssh() {
  local ip="$1" tries=30
  until ssh "${SSH_OPTS[@]}" "${SSH_USER}@${ip}" true 2>/dev/null; do
    tries=$((tries - 1))
    if [ "$tries" -le 0 ]; then
      echo "error: SSH did not come up on ${ip}" >&2
      exit 1
    fi
    sleep 2
  done
}

cmd_ip() {
  tart ip "$VM" --wait 30
}

refuse_if_running() {
  local pids pid started
  pids="$(tart_run_pids "$VM")"
  [ -n "$pids" ] || return 0
  echo "error: ${VM} already has a tart run process alive:" >&2
  while IFS= read -r pid; do
    [ -n "$pid" ] || continue
    read -r started < <(ps -o lstart= -p "$pid" 2>/dev/null) || started="unknown"
    echo "  pid ${pid}, started ${started}" >&2
  done <<<"${pids}"
  echo "  run: $(basename "$0") stop" >&2
  exit 1
}

cmd_start() {
  refuse_if_running
  ensure_created
  ensure_ssh_key
  if vm_running "$VM"; then
    echo "==> ${VM} already running"
  else
    echo "==> starting ${VM} (headless)"
    # --no-audio: the guest sound device opens the host microphone, which
    # pulls Bluetooth headsets into call mode. Every tart run here passes it.
    nohup tart run --no-graphics --no-audio "$VM" >/tmp/atrium-probe-vm.log 2>&1 &
    disown
  fi
  local ip
  ip="$(tart ip "$VM" --wait 60)"
  echo "==> VM IP: ${ip}"
  push_ssh_key
  wait_for_ssh "$ip"
  echo "==> SSH ready: ssh -i ${SSH_KEY} ${SSH_USER}@${ip}"
}

cmd_stop() {
  local pids pid count=0
  pids="$(tart_run_pids "$VM")"
  # tart's own registry can already disagree with the process table (the
  # bug this guards against), so a failed tart stop must not block the
  # pgrep-driven kill below.
  tart stop "$VM" 2>/dev/null || true
  if [ -n "$pids" ]; then
    while IFS= read -r pid; do
      [ -n "$pid" ] || continue
      kill "$pid" 2>/dev/null || true
      count=$((count + 1))
    done <<<"${pids}"
  fi
  echo "==> stopped ${count} tart run process(es) for ${VM}"
}

cmd_deploy() {
  local ip out
  ip="$(cmd_ip)"
  # a sanitizer build lives in its own output directory so the
  # ordinary one survives the hours it takes to produce. Set ATRIUM_OUT_DIR to
  # deploy that instead - a path relative to SRC, e.g. out/asan.
  out="${SRC}/${ATRIUM_OUT_DIR:-out/atrium}"
  if [ ! -d "${out}/Atrium.app" ]; then
    echo "error: ${out}/Atrium.app not found; build it first (make build)" >&2
    exit 1
  fi
  mkdir -p "${FIXTURES_DIR}"
  echo "==> deploying to ${SSH_USER}@${ip}:${REMOTE_DIR}"
  # shellcheck disable=SC2029  # REMOTE_DIR is a local var; intentional client-side expansion
  ssh "${SSH_OPTS[@]}" "${SSH_USER}@${ip}" "mkdir -p '${REMOTE_DIR}'"
  # Component build: the framework resolves 525 loose lib*.dylib next to the
  # .app via rpath, so they must be deployed alongside it. Single rsync so
  # --delete never prunes a sibling copied by another invocation.
  # Probe tools (cgevent/rapidcap/axresize binaries, built from
  # build/vm-tools/*.swift) get staged into the VM out-of-band during probe
  # rounds - stage them under REMOTE_DIR/tools/ so this --delete doesn't
  # prune them.
  # results/ is PULLED artifacts, not input: 1519 past run directories, 15G, and
  # this rsync was pushing every one of them into a 46G VM on each scenario
  # until it ran out of disk mid-deploy and reported the scenario unmeasured.
  # Plain --exclude, no --delete-excluded: pairing the two with a '***' pattern
  # made rsync 3.4.4 abort with "security violation: zero-length pathname".
  # The stale VM-side copy is pruned by the rm below instead.
  rsync -az --delete --exclude='results' \
    --filter='protect tools/***' -e "ssh ${SSH_OPTS[*]}" \
    "${out}/Atrium.app" \
    "${out}/"*.dylib \
    "${FIXTURES_DIR}" \
    "${SSH_USER}@${ip}:${REMOTE_DIR}/"
  # A byte copy of the host's own pulled artifacts, excluded above but left
  # behind by earlier deploys - 15G of it, on a 46G disk.
  #
  # `sh -s --` with the script on STDIN, because `ssh host cmd a b` does NOT
  # bind a and b as positional parameters - it JOINS them into one remote
  # command line. The previous form here read
  #   ssh host 'rm -rf "$1"/vm-fixtures/results' _ "${REMOTE_DIR}"
  # which the remote shell ran with $1 empty and the path as a SECOND rm
  # target, so every deploy copied 129 MB of app plus 541 dylibs and then
  # deleted the entire directory. It still exited 0 and printed "deploy
  # complete", and each scenario died afterwards on a missing binary reported
  # as a harness error rather than a failure.
  #
  # Feeding the script through stdin is what makes the path data rather than
  # code, which was the intent of the broken form. The path itself is built by
  # this script from a literal and SSH_USER, so it carries no metacharacters
  # for the join to mangle.
  printf '%s\n' 'rm -rf "$1"/vm-fixtures/results' \
    | ssh "${SSH_OPTS[@]}" "${SSH_USER}@${ip}" "sh -s -- ${REMOTE_DIR}"
  # The deploy has to be checked, not announced. Everything above can exit 0
  # having moved nothing, and the harness turns a missing binary into an
  # unmeasured run rather than a red one, so a silent deploy costs a whole
  # scenario round before anyone looks here.
  if ! ssh "${SSH_OPTS[@]}" "${SSH_USER}@${ip}" \
      "test -x ${REMOTE_DIR}/Atrium.app/Contents/MacOS/Atrium"; then
    echo "error: deploy moved nothing - ${REMOTE_DIR}/Atrium.app is not in the VM" >&2
    exit 1
  fi
  echo "==> deploy complete"
}

cmd_shell() {
  local ip
  ip="$(cmd_ip)"
  exec ssh "${SSH_OPTS[@]}" "${SSH_USER}@${ip}"
}

# Extra Chromium flags are passed through, so a surface can be exercised in the
# VM instead of on the host desktop:
#   ./build/scripts/vm.sh run-atrium --force-dark-mode
#
# ATRIUM_STARTUP_WINDOW=1 drops --no-startup-window, so the launch opens a real
# browser window.
#
# That matters for more than looks. Without a window there is no Browser, so
# AtriumBrowserWindow's browser_ is null and EVERY sidebar message that needs it
# returns silently - sidebarTabClicked and sidebarNewTab both do nothing, while
# anything routed through the Swift bridge (pin, unpin, paging, favourites)
# works normally. A windowless VM therefore cannot tell a broken click path from
# a working one, which is exactly the gap that hid a broken cold-row click.
cmd_run_atrium() {
  local ip EXTRA_ARGS WINDOW_FLAG
  EXTRA_ARGS="$*"
  WINDOW_FLAG="--no-startup-window"
  if [ "${ATRIUM_STARTUP_WINDOW:-0}" = "1" ]; then
    WINDOW_FLAG=""
  fi
  ip="$(cmd_ip)"
  echo "==> launching Atrium inside ${VM}${WINDOW_FLAG:+ (windowless)}"
  # shellcheck disable=SC2029  # REMOTE_DIR/SSH_USER are local vars; intentional client-side expansion
  ssh "${SSH_OPTS[@]}" "${SSH_USER}@${ip}" \
    "nohup env \
      '${REMOTE_DIR}/Atrium.app/Contents/MacOS/Atrium' \
      --user-data-dir='/Users/${SSH_USER}/Library/Application Support/Atrium/Chromium' \
      ${WINDOW_FLAG} --no-first-run --no-default-browser-check ${EXTRA_ARGS} \
      >/tmp/atrium-run.log 2>&1 & sleep 2; pgrep -fl Atrium || true"
}

# a filter selecting nothing still prints "SUCCESS: all tests passed"
# and returns 0 - test_launcher.cc:2184 only warns. So the log, not the exit
# code, decides. Pure over a log file so --selftest can drive it.
judge_gtest_log() {
  local log="$1"
  local run_status="$2"

  if grep -q "No matching tests to run" "$log"; then
    echo "" >&2
    echo "run-gtest FAILED: the filter matched no tests, so the run proved nothing." >&2
    echo "  Almost always a stale binary or a renamed test: out/atrium holds" >&2
    echo "  whatever ninja last linked, and a test edited since then is not in it." >&2
    echo "  Confirm with" >&2
    echo "    strings out/atrium/<binary> | grep -c <TestName>" >&2
    echo "  and re-link before trusting the next run." >&2
    return 1
  fi

  local ran
  ran="$(grep -cE '^\[[0-9]+/[0-9]+\] ' "$log" || true)"

  if [ "$run_status" -ne 0 ]; then
    echo "" >&2
    echo "run-gtest FAILED: the test binary exited ${run_status} (${ran} test(s) ran)." >&2
    return 1
  fi

  if grep -qE '\[  FAILED  \]|[0-9]+ FAILED TEST|^[0-9]+ tests? failed:' "$log"; then
    echo "" >&2
    echo "run-gtest FAILED: the log names a failed test though the binary exited 0." >&2
    return 1
  fi

  if [ "$ran" -eq 0 ] || ! grep -q "SUCCESS: all tests passed" "$log"; then
    echo "" >&2
    echo "run-gtest FAILED: no completed test in the log, so nothing can pass." >&2
    echo "  A crash, a launcher timeout, or a dropped ssh session all land here." >&2
    return 1
  fi

  echo "==> run-gtest SUCCESS: ${ran} test(s) ran, all passed"
  return 0
}

# A gtest binary (e.g. browser_tests) can run inside the VM,
# but only single-window InProcessBrowserTest cases: multi-window teardown
# still hits FATAL Check failed at
# content/browser/renderer_host/browser_compositor_view_mac.mm:379, both on
# the host and in this VM's headless mode - see docs/BUILD.md. Do not treat a
# clean exit here as proof for a multi-window case.
#
# browser_tests hard-requires the real source tree at a fixed relative depth
# from its own directory (chrome::DIR_TEST_DATA, chrome/test/base/
# chrome_test_utils.cc) and resolves 'Atrium Framework.framework' relative to
# itself too (base/apple/bundle_locations.mm) - a copied deploy into
# REMOTE_DIR satisfies neither, and the 121GB checkout does not fit the VM's
# 50GB disk to copy. So this boots the VM with the live checkout virtiofs-
# mounted read-only and runs the binary IN PLACE from the mount: that
# directory is the exact out/atrium ninja built on the host, so the framework
# bundle is already sitting right next to the binary - no rsync needed.
#
#   ./build/scripts/vm.sh run-gtest browser_tests --gtest_filter=Foo.Bar
cmd_run_gtest() {
  local binary="${1:-}"
  if [ -z "$binary" ]; then
    echo "usage: $(basename "$0") run-gtest <binary> [gtest args...]" >&2
    exit 1
  fi
  shift
  local gtest_args=("$@")

  # Same lock vm-scenario.sh takes: run-gtest also does tart stop + its own
  # deploy, and used to race a concurrent scenario's rsync mid-copy.
  trap 'vm_lock_release "${VM}"' EXIT
  trap 'vm_lock_release "${VM}" || true; exit 130' INT
  trap 'vm_lock_release "${VM}" || true; exit 143' TERM
  vm_lock_acquire "${VM}"

  ensure_created
  ensure_ssh_key

  echo "==> stopping ${VM} to restart with the live checkout mounted"
  tart stop "$VM" 2>/dev/null || true
  echo "==> starting ${VM} (headless) with --dir=src:${SRC}:ro"
  nohup tart run --no-graphics --no-audio --dir="src:${SRC}:ro" "$VM" >/tmp/atrium-probe-vm.log 2>&1 &
  disown
  local ip
  ip="$(tart ip "$VM" --wait 60)"
  echo "==> VM IP: ${ip}"
  push_ssh_key
  wait_for_ssh "$ip"

  local mount_out="/Volumes/My Shared Files/src/out/atrium"
  local user_data_dir="/tmp/atrium-gtest-$$"
  local remote_cmd
  remote_cmd="cd $(printf '%q' "$mount_out") && ./$(printf '%q' "$binary")"
  local a
  for a in "${gtest_args[@]:-}"; do
    [ -n "$a" ] && remote_cmd="${remote_cmd} $(printf '%q' "$a")"
  done
  remote_cmd="${remote_cmd} --user-data-dir=$(printf '%q' "$user_data_dir")"

  echo "==> running: ${remote_cmd}"
  local log
  log="$(mktemp "${TMPDIR:-/tmp}/atrium-run-gtest.XXXXXX")"
  # PIPESTATUS, not $?: tee's own 0 is what used to be reported as the run's.
  # set +e: pipefail+errexit would abort on this line before run_status is
  # read, skipping the VM restore and the judge_gtest_log diagnostic below.
  set +e
  # shellcheck disable=SC2029  # remote_cmd is built and quoted client-side on purpose
  ssh "${SSH_OPTS[@]}" "${SSH_USER}@${ip}" "$remote_cmd" 2>&1 | tee "$log"
  local run_status="${PIPESTATUS[0]}"
  set -e

  echo "==> restoring ${VM} to its normal no-mount headless state"
  tart stop "$VM" 2>/dev/null || true
  nohup tart run --no-graphics --no-audio "$VM" >/tmp/atrium-probe-vm.log 2>&1 &
  disown
  ip="$(tart ip "$VM" --wait 60)"
  push_ssh_key
  wait_for_ssh "$ip"

  local verdict=0
  judge_gtest_log "$log" "$run_status" || verdict=$?
  rm -f "$log"
  exit "$verdict"
}

cmd_snapshot() {
  tart stop "$VM" 2>/dev/null || true
  if vm_exists "$CLEAN_VM"; then
    echo "==> deleting old ${CLEAN_VM}"
    tart delete "$CLEAN_VM"
  fi
  echo "==> cloning ${VM} -> ${CLEAN_VM}"
  tart clone "$VM" "$CLEAN_VM"
}

cmd_restore() {
  tart stop "$VM" 2>/dev/null || true
  if vm_exists "$VM"; then
    tart delete "$VM"
  fi
  if ! vm_exists "$CLEAN_VM"; then
    echo "error: no ${CLEAN_VM} snapshot found; run 'snapshot' first" >&2
    exit 1
  fi
  echo "==> cloning ${CLEAN_VM} -> ${VM}"
  tart clone "$CLEAN_VM" "$VM"
}

# The three fixtures are what this fork's browser_tests really printed on
# 2026-08-21, cut to the lines judge_gtest_log reads.
cmd_run_gtest_selftest() {
  # Not local: the EXIT trap fires after this function's frame is gone.
  SELFTEST_DIR="$(mktemp -d "${TMPDIR:-/tmp}/atrium-run-gtest-selftest.XXXXXX")"
  trap 'rm -rf "${SELFTEST_DIR}"' EXIT
  local dir="${SELFTEST_DIR}"

  cat >"${dir}/pass.log" <<'EOF'
Note: Google Test filter = AtriumFoo.Bar
[ RUN      ] AtriumFoo.Bar
[       OK ] AtriumFoo.Bar (120 ms)
[  PASSED  ] 1 test.
[1/1] AtriumFoo.Bar (1200 ms)
SUCCESS: all tests passed.
Tests took 2 seconds.
EOF

  cat >"${dir}/fail.log" <<'EOF'
[ RUN      ] AtriumFoo.Bar
[  FAILED  ] AtriumFoo.Bar, where TypeParam =  and GetParam() =  (32641 ms)
[  PASSED  ] 0 tests.
[  FAILED  ] 1 test, listed below:

 1 FAILED TEST
[1/1] AtriumFoo.Bar (33919 ms)
1 test failed:
    AtriumFoo.Bar (../../chrome/browser/ui/atrium/atrium_browser_window_browsertest.cc:2831)
Tests took 34 seconds.
EOF

  cat >"${dir}/empty.log" <<'EOF'
Using sharding settings from environment. This is shard 0/1
Using 1 parallel jobs.
WARNING: No matching tests to run.
SUCCESS: all tests passed.
Tests took 0 seconds.
EOF

  echo "selftest 1/5: a run that passed a test passes"
  judge_gtest_log "${dir}/pass.log" 0 >/dev/null \
    || { echo "SELFTEST FAILED: a passing run did not pass" >&2; exit 1; }

  echo "selftest 2/5: a failed test exits non-zero"
  judge_gtest_log "${dir}/fail.log" 1 >/dev/null 2>&1 \
    && { echo "SELFTEST FAILED: a failed test exited 0" >&2; exit 1; }

  echo "selftest 3/5: a failed test the binary reported as 0 exits non-zero"
  judge_gtest_log "${dir}/fail.log" 0 >/dev/null 2>&1 \
    && { echo "SELFTEST FAILED: a failed test with a 0 exit code exited 0" >&2; exit 1; }

  echo "selftest 4/5: a filter matching nothing exits non-zero"
  judge_gtest_log "${dir}/empty.log" 0 >/dev/null 2>&1 \
    && { echo "SELFTEST FAILED: an empty filter exited 0" >&2; exit 1; }

  echo "selftest 5/5: a failing remote command still restores the VM and prints the judge diagnostic"
  selftest_run_gtest_survives_remote_failure "${dir}"

  echo "selftest OK: only a run with a passed test passes"
}

# Regression for pipefail+errexit aborting cmd_run_gtest's ssh pipeline before
# the restore step and judge_gtest_log ever run. Stubs tart and ssh so no real
# VM starts, and the remote command "fails" by exiting 1.
selftest_run_gtest_survives_remote_failure() {
  local dir="$1"
  local fakebin="${dir}/fakebin"
  mkdir -p "${fakebin}"

  cat >"${fakebin}/tart" <<'FAKETART'
#!/usr/bin/env bash
case "$1" in
  list) echo "${VM_NAME}" ;;
  ip) echo "127.0.0.1" ;;
  *) exit 0 ;;
esac
FAKETART
  chmod +x "${fakebin}/tart"

  cat >"${fakebin}/ssh" <<'FAKESSH'
#!/usr/bin/env bash
cmd="${*: -1}"
case "$cmd" in
  true) exit 0 ;;
  *fake-gtest-binary*) echo "connection reset by peer"; exit 1 ;;
  *) exit 0 ;;
esac
FAKESSH
  chmod +x "${fakebin}/ssh"

  local out status
  out="$(
    PATH="${fakebin}:${PATH}" \
      TART_HOME="${dir}/tart-home" \
      VM_NAME="atrium-probe-selftest" \
      VM_LOCK_ROOT="${dir}/vm-locks" \
      bash "${REPO}/build/scripts/vm.sh" run-gtest fake-gtest-binary 2>&1
  )" && status=0 || status=$?

  if [ "$status" -eq 0 ]; then
    echo "SELFTEST FAILED: a failing remote command exited 0" >&2
    echo "$out" >&2
    exit 1
  fi
  if ! grep -q "restoring atrium-probe-selftest to its normal no-mount headless state" <<<"$out"; then
    echo "SELFTEST FAILED: the VM was never restored after the remote command failed" >&2
    echo "$out" >&2
    exit 1
  fi
  if ! grep -q "run-gtest FAILED: the test binary exited" <<<"$out"; then
    echo "SELFTEST FAILED: judge_gtest_log's diagnostic never printed" >&2
    echo "$out" >&2
    exit 1
  fi
}

case "${1:-}" in
  start) cmd_start ;;
  stop) cmd_stop ;;
  ip) cmd_ip ;;
  deploy) cmd_deploy ;;
  shell) cmd_shell ;;
  run-atrium) shift; cmd_run_atrium "$@" ;;
  run-gtest) shift; cmd_run_gtest "$@" ;;
  run-gtest-selftest) cmd_run_gtest_selftest ;;
  snapshot) cmd_snapshot ;;
  restore) cmd_restore ;;
  *) usage; exit 1 ;;
esac
