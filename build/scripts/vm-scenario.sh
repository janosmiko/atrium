#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# build/scripts/vm-scenario.sh - declarative, asserting VM scenario runner.
#
# Turns the ad-hoc VM probing (manual cgevent/rapidcap/axresize driving,
# hand-written logs, no asserts) into a repeatable regression harness.
#
# A scenario (build/scenarios/<name>.json) declares: a spaces fixture, the
# startup active-space Atrium launches on (the presenter regression is
# startup-space-dependent), an optional themes fixture, a sequence of INPUT
# ACTIONS (settle / capture / key / key_hold_taps / resize / drag / move /
# key_during_capture / crash_renderer / relaunch / collapse_sidebar /
# eval_until),
# ASSERTIONS (ax_window_count / theme_changed / pixels_changed / region_stable /
# resize_applied / quit / file_contains / recency_walk_distinct), and an
# optional second list, TEARDOWN_ASSERTIONS, evaluated after the app has
# stopped.
#
# Run loop per scenario:
#   1. restore the probe VM to its CLEAN snapshot          (deterministic start)
#   2. deploy the current build (Atrium.app + dylib)       (./build/scripts/vm.sh deploy)
#   3. stage + compile probe tools inside the VM, host-build pixcmp
#   4. stage the fixture (patched so activeSpaceID = startup_space) + themes
#   5. launch Atrium with ATRIUM_UI_DYLIB + ATRIUM_SPACES_FILE + ATRIUM_THEMES_FILE
#   6. run the actions in order
#   7. evaluate the assertions, recording PASS/FAIL + evidence
#   8. if the scenario has teardown_assertions: stop the app, re-pull the run
#      log, evaluate them - all BEFORE the tally
#   9. ALWAYS tear down (quit the app, pull run log + artifacts)
#
# TEARDOWN-TIME LINES need step 8. Everything the app writes on its
# way out - a final flush, a last save, the sidebar's last push as the strip
# empties - lands after the pull in step 7, so nothing in `assertions` can see
# it, and a `wait_vlog` action in step 6 times out waiting for a line that does
# not exist yet. Raising that timeout does nothing, which is what shows the
# problem is ordering rather than patience. Put such a gate in
# `teardown_assertions` instead: same assertion types, same meanings, read
# against the log as it stands once the app is gone.
#
# ASSERTION ORDER MATTERS when `quit` is involved. The run log is
# pulled ONCE before assertions are evaluated, and assert_quit re-pulls it after
# the quit. So a vlog/vlog_absent assertion listed BEFORE `quit` sees only
# pre-quit output, and one listed AFTER it also sees whatever Cmd+Q produced.
# Getting this backwards is silent: a `vlog` gate can never match, and a
# `vlog_absent` gate passes trivially - measured on quit-with-tab, whose
# "the palette did not route the quit" gate went green while the line it looks
# for could not have been in the log either way. `teardown_assertions` is the
# way to say "after the quit" out loud instead of encoding it in list order.
#
# A QUIT IS GRADED ON HOW IT ENDED, not on the process being gone.
# Both stopping paths - the `quit` assertion and the shutdown that precedes a
# relaunch or a teardown assertion - go through lib/quit_outcome.py, which reads
# the harness's pgrep bookkeeping AND the app's own log. A process that only
# ended because the harness sent SIGTERM FAILS and the evidence names the
# fallback; so does one that vanished on time but exited on a signal rather than
# through the app's quit path. Before this, shutdown_app's fallback was a log
# line and nothing else, so a surface swallowing Cmd+Q could not fail anything -
# settings-confirm-quit-toggle scored 12 passed, 0 failed on 2026-08-10 while
# its own log said the chord had been ignored for the full fifteen seconds.
#
# A PERSISTENCE QUESTION needs both pieces, and neither alone. Every
# other assertion here is satisfied by a REPAINT: a surface that draws the new
# value and writes nothing passes the window count, the pixel compare and the
# log gate alike. `file_contains` reads the staged file back out of the VM, and
# `relaunch` stops the app and starts it again against those same files without
# restaging - so "it was written" and "it was read back" are two separate
# measurements rather than one hopeful one. Note the ORDER: a write that only
# happens on the way out is not on disk while the app still runs, so the read
# belongs after the relaunch (or in teardown_assertions), not before it.
#
# A scenario should also GATE ITS OWN SETUP. If a setup step can silently fail
# to take - an Escape that races the thing it dismisses, a click that lands on
# an overlay, a chord swallowed by a surface that happens to have focus - assert
# that it took, or the real assertion measures something other than what its
# name claims. empty-window-quit.json spent two task closes asserting "Cmd+Q
# quits" while every green run went through the command palette's route and
# never touched the browser window it was cited for.
#
# Exit code is non-zero if any assertion FAILed (CI-runnable in spirit).
# Artifacts land under build/vm-fixtures/results/<name>-<timestamp>/.
# NO SCENARIO MAY GATE ON A REAL PAGE LOAD. The probe VM is to have
# no outbound access, so a wait_vlog for a remote URL cannot be satisfied. Use
# the harness's own fixture server instead: set "serve_fixtures": true in the
# spec and point the navigation at http://127.0.0.1:8000/<file>.html, served
# from build/vm-fixtures.
#
# The reason is determinism, not policy for its own sake. When the VM did have
# access, the same suite was green or red by the hour depending on whether a
# remote host answered, and both readings looked like statements about Atrium -
# nine scenarios failed that way and the failures were filed as feature bugs.
# A page this repo serves is a fixed string this repo owns.
#
# A scenario about EXTERNAL-link handling needs a host Atrium CLASSIFIES as
# external, which is a different requirement from a remote one. Solve that in
# the classifier's input, not by reaching the internet.
#
#
# Usage:
#   build/scripts/vm-scenario.sh <scenario-name>     run one scenario
#   build/scripts/vm-scenario.sh all                 run every build/scenarios/*.json
#   build/scripts/vm-scenario.sh all <name> <name>   run just those, in ONE process -
#                                            the same path `all` takes, which a
#                                            per-scenario loop does not
#   build/scripts/vm-scenario.sh --list              list available scenarios
#   build/scripts/vm-scenario.sh --validate <name>   parse+validate a spec only (no VM)
#
# Env knobs:
#   SKIP_RESTORE=1   skip the clean-snapshot restore (faster iterative dev)
#   SKIP_DEPLOY=1    skip make vm-deploy (reuse the already-deployed build)
#   ATRIUM_EXTRA_FLAGS="--flag ..."  extra Chromium flags, appended after the
#                    scenario's own optional `chromium_flags` string (so an
#                    ad-hoc run can override a repeated one - last wins).
#                    Useful for an ad-hoc flag a scenario's own spec does
#                    not carry.
#   VM_LOCK_WAIT_SECS=1800  how long a runner waits for another one's VM
#                    lock (see vm-lock.sh) before giving up
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
# shellcheck disable=SC1091
source "${REPO}/build/scripts/env.sh"

# --- VM/SSH config (mirrors build/scripts/vm.sh so probe ssh matches lifecycle ssh) ---
export TART_HOME="${TART_HOME:-/Volumes/AtriumBuild/tart}"
VM="${VM_NAME:-atrium-probe}"
SSH_USER="admin"
SSH_KEY="${TART_HOME}/${VM}-key"
SSH_OPTS=(-i "${SSH_KEY}" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=5)
REMOTE_DIR="/Users/${SSH_USER}/atrium-probe"
REMOTE_TOOLS="${REMOTE_DIR}/tools"
REMOTE_SCEN="${REMOTE_DIR}/scenario"
# Atrium's Chromium user-data-dir inside the VM (mirrors launch_atrium's
# --user-data-dir). A scenario's optional `seed_prefs` fixture is written into
# each declared profile's Preferences here BEFORE launch, so a clean snapshot
# can start with e.g. a user-chosen default search engine (no first-run picker).
REMOTE_UDD="/Users/${SSH_USER}/Library/Application Support/Atrium/Chromium"
# The dylib is no longer built or deployed, so this path does
# not exist and CopySidebarModelJSON comes back empty. Kept as an override so a
# run can still be pointed at a pre-cut copy - out/atrium/atrium-pre-cut holds
# the last one - when the question is whether the cut changed a behaviour.
DYLIB_REMOTE="${ATRIUM_DYLIB_REMOTE:-${REMOTE_DIR}/libAtriumUI.dylib}"

SCEN_DIR="${REPO}/build/scenarios"
LIB_DIR="${SCEN_DIR}/lib"
TOOLS_SRC="${REPO}/build/vm-tools"
RESULTS_ROOT="${REPO}/build/vm-fixtures/results"
PARSE="${LIB_DIR}/parse.py"
PICK="${LIB_DIR}/pick_renderer.py"
FILE_VALUE="${LIB_DIR}/file_value.py"
QUIT_OUTCOME="${LIB_DIR}/quit_outcome.py"
MOVED_OUTCOME="${LIB_DIR}/moved_outcome.py"
MID_FRAME="${LIB_DIR}/mid_frame.py"

VM_IP=""            # resolved once per scenario after boot
RESULTS_DIR=""      # per-scenario artifact dir
RUN_LOG_LOCAL=""    # pulled Atrium run log (VLOG source for asserts)
HOST_PIXCMP=""      # host-compiled pixel-compare binary
APP_PROCESS="Atrium"
PASS_COUNT=0
FAIL_COUNT=0
# Harness faults - a capture that returned nothing, and anything else that
# stops the run measuring what it claims to. Counted apart from FAIL_COUNT so
# the summary can say 'the harness broke' rather than blaming an assertion.
HARNESS_ERRORS=0
# Has the app already been stopped? assert_quit sets it when the process is
# confirmed gone, so shutdown_app does not Cmd+Q an empty desktop.
APP_QUIT=0
declare -A CAPTURE_LAST=()   # capture id -> host path of its last (most-settled) frame
declare -A EVAL_RESULT=()    # eval id -> the value that surface returned
CDP_PORT="${ATRIUM_CDP_PORT:-9334}"
# host paths of the run-log segments a `relaunch` closed, oldest
# first, plus which launch is running. Empty for every scenario that never
# relaunches - which is what keeps pull_run_log's output byte-identical for
# them.
declare -a RUN_LOG_SEGMENTS=()
LAUNCH_INDEX=1
# The line pull_run_log writes between two segments. It is the only handle a
# scenario has on "after the relaunch", since the app's own lines look the same
# in both launches - see the `relaunch` action for the whole argument. Kept in
# sync with HARNESS_LINE_PREFIX in lib/parse.py.
RELAUNCH_MARKER="atrium harness: launch"

log() { printf '==> %s\n' "$*" >&2; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

# --- remote helpers ---------------------------------------------------------
vm_ssh() {
  # shellcheck disable=SC2029  # intentional client-side expansion of args
  # `</dev/null`: the action/assert loops read records from a process-
  # substitution pipe on stdin; without this, ssh would drain that pipe and
  # silently truncate the loop after the first ssh-using action.
  ssh "${SSH_OPTS[@]}" "${SSH_USER}@${VM_IP}" "$@" </dev/null
}
vm_scp_to()   { scp "${SSH_OPTS[@]}" -q "$1" "${SSH_USER}@${VM_IP}:$2"; }
vm_scp_from() { scp "${SSH_OPTS[@]}" -qr "${SSH_USER}@${VM_IP}:$1" "$2"; }

# vm_q/vm_num live in lib/quoting.sh, shared with keycombo.sh
# rather than duplicated.
# shellcheck source=build/scenarios/lib/quoting.sh
source "${LIB_DIR}/quoting.sh"
# shellcheck source=build/scripts/vm-lock.sh
source "${REPO}/build/scripts/vm-lock.sh"

# record_result <PASS|FAIL|INFO> <name> <evidence...>
record_result() {
  local status="$1" name="$2"
  shift 2
  local evidence="$*"
  case "${status}" in
    PASS) PASS_COUNT=$(( PASS_COUNT + 1 )) ;;
    FAIL) FAIL_COUNT=$(( FAIL_COUNT + 1 )) ;;
    # a probe tool that did not answer. The run is not green - the
    # summary already fails on a harness error - but the count is NOT added to
    # FAIL_COUNT, because nothing about the app was measured. A tool failure
    # reported as a product failure is how a whole run of false findings
    # starts.
    HARNESS) HARNESS_ERRORS=$(( HARNESS_ERRORS + 1 )) ;;
  esac
  printf '[%s] %s :: %s\n' "${status}" "${name}" "${evidence}" | tee -a "${RESULTS_DIR}/results.log" >&2
}

# shellcheck source=build/scenarios/lib/asserts.sh
source "${LIB_DIR}/asserts.sh"
# shellcheck source=build/scenarios/lib/keycombo.sh
source "${LIB_DIR}/keycombo.sh"

# --- field extractor for tab-encoded action/assert records ------------------
# field <record> <key> -> value (empty if absent)
field() {
  local record="$1" key="$2" tok
  local -a toks
  IFS=$'\t' read -ra toks <<<"${record}"
  for tok in "${toks[@]:1}"; do
    if [ "${tok%%=*}" = "${key}" ]; then printf '%s' "${tok#*=}"; return 0; fi
  done
  return 0
}

# sleep_ms <ms> - fractional sleep for settle actions.
sleep_ms() { awk "BEGIN{ printf \"%.3f\", $1/1000 }" | xargs sleep; }

# scaled_wait <ms> - every wait multiplied, for a build that is simply slower.
#
# a sanitizer build runs 2-3x slower, so a settle that was generous
# stops being one and a run fails on timing rather than on behaviour. This scales
# the WAITS and touches no assertion and no bar - the difference matters, because
# moving a bar to accommodate a slow build is how a guard stops guarding.
#
#   ATRIUM_SETTLE_SCALE=3 ./build/scripts/vm-scenario.sh <name>
scaled_wait() { echo $(( $1 * ${ATRIUM_SETTLE_SCALE:-1} )); }

# --- VM bring-up ------------------------------------------------------------
# macOS's own screen-recording TCC reminder ("<X> Is Accessing
# Your Screen") pops into the top-right corner mid-run because the harness's
# own screencapture rides over this SSH session, and region_stable measures
# a region wide enough to catch it (narrowing that region was rejected).
# `defaults write com.apple.notificationcenterui doNotDisturb` - the
# pre-Monterey way to mute banners - is a no-op on this macOS version
# (verified by observation: a test notification still banners after setting
# it and restarting the agent). What DOES work: booting the notification
# banner agent out of launchd entirely. Confirmed by observation on macOS
# 26.5 - the pre-existing "Atrium Notifications" banners and a fired test
# notification both stop appearing immediately after this, and the agent
# does not respawn on its own (a `bootstrap` of the same plist brings it
# back if ever needed). Best-effort: a VM that can't reach this agent still
# runs the scenario, just with the original noise-tolerance risk.
suppress_notification_banners() {
  log "suppressing macOS notification banners for this run (launchctl bootout com.apple.notificationcenterui.agent)"
  if ! vm_ssh "pgrep -q -f NotificationCenter.app/Contents/MacOS/NotificationCenter" >/dev/null 2>&1; then
    log "  notification banner agent already booted out - banners already suppressed"
    return 0
  fi
  if vm_ssh "launchctl bootout gui/\$(id -u)/com.apple.notificationcenterui.agent" >/dev/null 2>&1; then
    log "  notification banner agent booted out - banners suppressed for this run"
  else
    log "  WARNING: could not boot out the notification banner agent - banners may still appear (region_stable noise risk unmitigated)"
  fi
}

bring_up_vm() {
  # The clean-snapshot restore would wipe the build
  # SKIP_DEPLOY exists to reuse, so SKIP_DEPLOY implies SKIP_RESTORE.
  if [ "${SKIP_DEPLOY:-0}" = "1" ] && [ "${SKIP_RESTORE:-0}" != "1" ]; then
    log "SKIP_DEPLOY=1 implies SKIP_RESTORE=1 - the clean-snapshot restore would wipe the already-deployed build this flag exists to reuse"
    SKIP_RESTORE=1
  fi
  if [ "${SKIP_RESTORE:-0}" != "1" ]; then
    log "restoring ${VM} to clean snapshot"
    if ! "${REPO}/build/scripts/vm.sh" restore; then
      die "restore failed - create a clean snapshot first: ./build/scripts/vm.sh snapshot"
    fi
  else
    log "SKIP_RESTORE=1 - reusing current VM state"
  fi
  "${REPO}/build/scripts/vm.sh" start
  VM_IP="$("${REPO}/build/scripts/vm.sh" ip)"
  [ -n "${VM_IP}" ] || die "could not resolve VM IP"
  log "VM IP ${VM_IP}"
  suppress_notification_banners
  if [ "${SKIP_DEPLOY:-0}" != "1" ]; then
    log "deploying current build into VM"
    # Explicit check, not ambient `set -e`: when `all` calls this via
    # `run_scenario "${s}" || rc=1`, bash suspends errexit for the WHOLE call
    # tree under that `||` - a plain nonzero exit here would
    # otherwise be swallowed and the scenario would carry on staging fixtures
    # and launching against a build that never landed.
    if ! "${REPO}/build/scripts/vm.sh" deploy; then
      log "  deploy FAILED - not proceeding with this scenario"
      return 1
    fi
  else
    log "SKIP_DEPLOY=1 - reusing already-deployed build"
  fi
}

# Compile probe tools inside the VM (cgevent/axresize/rapidcap need the VM's
# AX/ScreenCaptureKit/TCC relationship - a host-signed binary won't hold the
# grants). pixcmp is pure image math, so it is host-compiled once.
stage_tools() {
  log "staging + compiling probe tools in VM"
  vm_ssh "mkdir -p '${REMOTE_TOOLS}/src'"
  # Staged as a FILE, not an inline `sh -c "..."`: vm_ssh hardcodes
  # `</dev/null` (see its doc), so a heredoc into `cat >` never arrives, and the
  # inline form needs three levels of nested quoting through ssh - the level
  # that got eaten was the one writing the completion marker, so the compile
  # silently succeeded while the poll waited forever for a marker that could
  # never appear.
  local builder="${RESULTS_DIR}/build-one.sh"
  cat > "${builder}" <<'BUILDONE'
#!/bin/sh
# $1 = tools dir, $2 = tool name. Writes <tool>.done only on success, so a
# half-written binary is never mistaken for a finished one.
set -e
tools="$1"; t="$2"
rm -f "${tools}/${t}.done"
swiftc -O "${tools}/src/${t}.swift" -o "${tools}/${t}.tmp"
mv "${tools}/${t}.tmp" "${tools}/${t}"
printf ok > "${tools}/${t}.done"
BUILDONE
  vm_scp_to "${builder}" "${REMOTE_TOOLS}/build-one.sh"
  vm_ssh "chmod +x '${REMOTE_TOOLS}/build-one.sh'"

  local t sum
  for t in cgevent axresize rapidcap; do
    sum="$(shasum -a 256 "${TOOLS_SRC}/${t}.swift" | cut -d' ' -f1)"
    # REUSE the binary already in the VM when its staged source is byte-identical
    # to the host's. This is not just a speed win - it is required for capture to
    # work on the first frame of a run. A freshly compiled binary is a new
    # binary as far as TCC is concerned, and its FIRST ScreenCaptureKit call
    # silently yields no frames in a headless VM (no one is there to answer the
    # permission prompt); later calls succeed. Recompiling every run therefore
    # burned the run's first `capture` action - observed as "no frames pulled"
    # for every capture in a scenario whose tools had just been rebuilt, while
    # the very same binary captured fine moments later. The clean snapshot
    # carries both the granted binaries AND their sources, so the comparison
    # below is what lets a restored VM skip the rebuild entirely.
    #
    # Compared BEFORE the scp, since staging the source would overwrite the
    # very thing being compared. ATRIUM_VM_FORCE_TOOLS=1 rebuilds regardless.
    if [ -z "${ATRIUM_VM_FORCE_TOOLS:-}" ] && vm_ssh "test -x '${REMOTE_TOOLS}/${t}' && \
      test \"\$(shasum -a 256 '${REMOTE_TOOLS}/src/${t}.swift' 2>/dev/null | cut -d' ' -f1)\" = '${sum}'" 2>/dev/null; then
      log "  ${t}: reusing VM-compiled binary (source unchanged; keeps its TCC grants)"
      continue
    fi
    vm_scp_to "${TOOLS_SRC}/${t}.swift" "${REMOTE_TOOLS}/src/${t}.swift"
    # DETACHED compile driven by an uploaded script, then poll for its
    # completion marker - not a plain blocking `vm_ssh swiftc`. swiftc inside a
    # 4-CPU/8GB VM is heavy enough to stall the VM's network for tens of
    # seconds, which dropped the ssh channel mid-compile and failed the whole
    # scenario ("No route to host" on axresize) even though the compile itself
    # went on to finish. nohup means the compile outlives the channel; the
    # marker (written only after a successful move into place) means a
    # half-written binary is never mistaken for a good one.
    #
    # The build step is a FILE rather than an inline `sh -c "..."`: the inline
    # form needs three levels of nested quoting through ssh, and the level that
    # got eaten was the one writing the marker - so the compile silently
    # succeeded while the poll waited forever for a marker that could never
    # appear.
    vm_ssh "rm -f '${REMOTE_TOOLS}/${t}.done'; \
      nohup '${REMOTE_TOOLS}/build-one.sh' '${REMOTE_TOOLS}' '${t}' >/dev/null 2>&1 &" >/dev/null 2>&1 || true
    # 300s: a cold clean-snapshot boot leaves the VM at load ~50 for the first
    # minute, and swiftc under that takes minutes for what costs 0.2s idle.
    if retry 300 3 _tool_built "${t}"; then
      log "  ${t}: compiled in VM"
    else
      die "in-VM compile of ${t}.swift failed or timed out"
    fi
  done
  HOST_PIXCMP="${RESULTS_DIR}/pixcmp"
  swiftc -O "${TOOLS_SRC}/pixcmp.swift" -o "${HOST_PIXCMP}" || die "host compile of pixcmp.swift failed"
}

# the "X Is Accessing Your Screen" banner that fails region_stable
# is NOT routed through com.apple.notificationcenterui.agent (suppress_
# notification_banners bootout doesn't touch it, confirmed by a still-failing
# follow-up run). Live `log stream` on the VM during a rapidcap burst shows
# the real source: replayd's -[SCAlertStatsManager requireAlertWithCanonical
# BundleID:auditToken:], evaluated on EVERY SCStream start
# (rapidcap starts a fresh stream per capture action) - a TCC/ScreenCaptureKit
# reminder, a different subsystem entirely. It was observed firing on a run
# hours into the day but not on one earlier the same day, consistent with a
# cooldown since the client's last alert. This throwaway capture, run once
# right after the probe tools are staged - tens of seconds and several
# scenario steps before any scenario's own measured capture - exists to
# absorb that cooldown early, so a real alert (if one fires) appears and
# auto-dismisses well outside the measured window instead of racing a
# region_stable capture.
warm_up_screen_capture_alert() {
  log "warming up ScreenCaptureKit alert state (throwaway capture, absorbs any first-stream banner early)"
  vm_ssh "'${REMOTE_TOOLS}/rapidcap' /tmp/atrium-warmup-capture 200 5 >/dev/null 2>&1" >/dev/null 2>&1 || true
}

# --- the system window-picker consent alert ---------------------------------
# macOS 26 re-asks for screen recording over an existing grant, naming the SSH
# session rapidcap rides over: "com.apple.sshd-session is requesting to bypass
# the system private window picker". It is system-modal - macOS swallows every
# synthetic click and scroll while it is up - and the snapshot cannot
# pre-answer it: not the kTCCServiceScreenCapture row (auth_value=2, untouched
# by the alert), and answering one wrote no file a restore carries.

# Measured off the alert at 1024x768. Its panel is translucent but its app icon
# is not, so the icon mean holds whatever sits behind the alert.
PICKER_ALLOW_X=512          # centre of the "Allow" button
PICKER_ALLOW_Y=418
PICKER_ICON_X=412           # the alert's screen-recording app icon
PICKER_ICON_Y=152
PICKER_ICON_W=42
PICKER_ICON_H=42
PICKER_ICON_MEAN="153.9,176.9,228.3"
# Euclidean RGB distance. The two backgrounds measured behind the alert sit 85
# and 205 away, so 20 separates them with room to spare.
PICKER_ICON_TOL=20
PICKER_LAST_MEAN=""

# picker_consent_up <tag> - 0 when the consent alert is on screen right now.
# Captures its own frame, because the alert can appear between two scenario
# steps and no earlier frame proves anything about now.
picker_consent_up() {
  local tag="$1"
  local vmdir="/tmp/atrium-picker-${tag}"
  local host="${RESULTS_DIR}/picker-${tag}"
  PICKER_LAST_MEAN=""
  rm -rf "${host}"
  vm_ssh "rm -rf '${vmdir}'; '${REMOTE_TOOLS}/rapidcap' '${vmdir}' 200 5 >/dev/null 2>&1" >/dev/null 2>&1 || true
  vm_scp_from "${vmdir}" "${host}" >/dev/null 2>&1 || true
  local frame
  frame="$(find "${host}" -name 'frame-*.png' 2>/dev/null | sort | tail -1)"
  if [ -z "${frame}" ]; then
    log "  window-picker consent check (${tag}): no frame pulled - cannot tell, treating as clear"
    return 1
  fi
  local stats
  if ! stats="$("${HOST_PIXCMP}" stats "${frame}" \
    "${PICKER_ICON_X}" "${PICKER_ICON_Y}" "${PICKER_ICON_W}" "${PICKER_ICON_H}" 2>/dev/null)"; then
    log "harness: window-picker consent check could not read the frame (pixcmp failed)"
    return 2
  fi
  PICKER_LAST_MEAN="${stats#*mean=}"
  PICKER_LAST_MEAN="${PICKER_LAST_MEAN%% *}"
  awk -v got="${PICKER_LAST_MEAN}" -v want="${PICKER_ICON_MEAN}" -v tol="${PICKER_ICON_TOL}" 'BEGIN {
    split(got, g, ","); split(want, w, ",");
    d = sqrt((g[1]-w[1])^2 + (g[2]-w[2])^2 + (g[3]-w[3])^2);
    exit (d <= tol) ? 0 : 1
  }'
}

# Look for the consent alert after the warm-up capture triggered it, and click
# Allow. Three looks rather than one: the alert is drawn a beat after the
# stream starts, and each look costs one 200 ms capture.
dismiss_window_picker_consent() {
  local try
  for try in 1 2 3; do
    if picker_consent_up "warm${try}"; then
      log "harness: dismissed the window-picker consent (alert icon at ${PICKER_LAST_MEAN}, clicking Allow at ${PICKER_ALLOW_X},${PICKER_ALLOW_Y})"
      vm_ssh "'${REMOTE_TOOLS}/cgevent' click ${PICKER_ALLOW_X} ${PICKER_ALLOW_Y}" >/dev/null 2>&1 || true
      sleep 1
      if picker_consent_up "warm${try}-after"; then
        die "the window-picker consent is still up after clicking Allow at ${PICKER_ALLOW_X},${PICKER_ALLOW_Y} - it swallows every click and scroll, so this run would measure nothing"
      fi
      log "  consent gone (icon region now ${PICKER_LAST_MEAN})"
      return 0
    fi
    # `&&` alone would leave the loop body's last command failing on try 3,
    # which under `set -e` exits the whole run.
    if [ "${try}" -lt 3 ]; then sleep 2; fi
  done
  log "no window-picker consent on screen after the warm-up capture (icon region ${PICKER_LAST_MEAN:-unread})"
}

# The alert swallows input silently, so a run that meets one measures nothing
# and reports it as product failure. Checked once more here, immediately before
# the first action, so that outcome is named instead of inferred.
require_no_window_picker_consent() {
  # A bare call would trip `set -e` on rc 1 (clear, the common case) before
  # this function ever reads the code - `||` keeps the non-zero exit inside
  # a conditional context so errexit lets it through.
  local rc=0
  picker_consent_up "pre-action" || rc=$?
  if [ "${rc}" -eq 0 ]; then
    record_result HARNESS "window-picker consent" \
      "the system window-picker consent alert is on screen at the first action (icon region ${PICKER_LAST_MEAN}) - it swallows every click and scroll; the pre-warm dismissal did not hold"
    die "window-picker consent alert is up at the first action - refusing to measure a run whose input is swallowed"
  elif [ "${rc}" -eq 2 ]; then
    record_result HARNESS "window-picker consent" \
      "the window-picker consent check could not read the frame (pixcmp failed) - refusing to assume the alert is clear"
    die "window-picker consent check could not read the frame - refusing to measure a run that may have its input swallowed"
  fi
}

# Patch the spaces fixture so activeSpaceID = startup_space, then stage it plus
# the optional themes fixture into the VM.
stage_fixtures() {
  local fixture="$1" themes="$2" startup_space="$3" routing="$4" seed_prefs="$5"
  local hotkeys="$6" settings="$7" spaces_absent="$8" import_arc_dir="$9"
  local import_zen_dir="${10}" import_chrome_dir="${11}" import_firefox_dir="${12}"
  local src="${REPO}/build/vm-fixtures/${fixture}"
  [ -f "${src}" ] || src="${SCEN_DIR}/fixtures/${fixture}"
  [ -f "${src}" ] || die "spaces fixture not found: ${fixture}"
  local patched="${RESULTS_DIR}/spaces.json"
  python3 - "${src}" "${patched}" "${startup_space}" <<'PY'
import json, sys
src, dst, startup = sys.argv[1], sys.argv[2], sys.argv[3]
doc = json.load(open(src, encoding="utf-8"))
if startup:
    doc.setdefault("appState", {})["activeSpaceID"] = startup
json.dump(doc, open(dst, "w", encoding="utf-8"))
PY
  vm_ssh "mkdir -p '${REMOTE_SCEN}'"
  vm_scp_to "${patched}" "${REMOTE_SCEN}/spaces.json"
  REMOTE_SPACES="${REMOTE_SCEN}/spaces.json"
  REMOTE_THEMES=""
  if [ -n "${themes}" ]; then
    local tsrc="${REPO}/build/vm-fixtures/${themes}"
    [ -f "${tsrc}" ] || tsrc="${SCEN_DIR}/fixtures/${themes}"
    [ -f "${tsrc}" ] || die "themes fixture not found: ${themes}"
    cp "${tsrc}" "${RESULTS_DIR}/themes.json"
    vm_scp_to "${tsrc}" "${REMOTE_SCEN}/themes.json"
    REMOTE_THEMES="${REMOTE_SCEN}/themes.json"
  fi
  # an optional hotkeys.json, staged the same way as themes/routing
  # and handed to the app via ATRIUM_HOTKEYS_FILE. Needed to exercise a GATED
  # chord (one macOS uses for text editing) without shipping a default binding
  # on one - see atrium_bridge.h's EDIT FOCUS doc block on why gating is a
  # property of the chord, so a rebind is enough to test it.
  REMOTE_HOTKEYS=""
  if [ -n "${hotkeys}" ]; then
    local hsrc="${REPO}/build/vm-fixtures/${hotkeys}"
    [ -f "${hsrc}" ] || hsrc="${SCEN_DIR}/fixtures/${hotkeys}"
    [ -f "${hsrc}" ] || die "hotkeys fixture not found: ${hotkeys}"
    cp "${hsrc}" "${RESULTS_DIR}/hotkeys.json"
    vm_scp_to "${hsrc}" "${REMOTE_SCEN}/hotkeys.json"
    REMOTE_HOTKEYS="${REMOTE_SCEN}/hotkeys.json"
  fi
  # An optional settings.json, staged like hotkeys and handed over via
  # ATRIUM_SETTINGS_FILE. Seeding the file beats driving Settings > Appearance
  # with synthetic input: SwiftUI control commits are
  # unreliable here, so a scenario that toggled a setting would be testing the
  # input path rather than the behaviour it means to assert.
  REMOTE_SETTINGS=""
  if [ -n "${settings}" ]; then
    local ssrc="${REPO}/build/vm-fixtures/${settings}"
    [ -f "${ssrc}" ] || ssrc="${SCEN_DIR}/fixtures/${settings}"
    [ -f "${ssrc}" ] || die "settings fixture not found: ${settings}"
    cp "${ssrc}" "${RESULTS_DIR}/settings.json"
    vm_scp_to "${ssrc}" "${REMOTE_SCEN}/settings.json"
    REMOTE_SETTINGS="${REMOTE_SCEN}/settings.json"
  fi
  REMOTE_ROUTING=""
  if [ -n "${routing}" ]; then
    local rsrc="${REPO}/build/vm-fixtures/${routing}"
    [ -f "${rsrc}" ] || rsrc="${SCEN_DIR}/fixtures/${routing}"
    [ -f "${rsrc}" ] || die "routing fixture not found: ${routing}"
    cp "${rsrc}" "${RESULTS_DIR}/routing.json"
    vm_scp_to "${rsrc}" "${REMOTE_SCEN}/routing.json"
    REMOTE_ROUTING="${REMOTE_SCEN}/routing.json"
  fi
  # Optional: seed each declared profile's Chromium Preferences BEFORE launch
  # (e.g. a user-chosen default search engine, so the first-run picker never
  # gates typed navigation on a clean snapshot). Profiles come from the
  # fixture's own profileIDs, so the seed matches whatever spaces it declares.
  if [ -n "${seed_prefs}" ]; then
    local psrc="${REPO}/build/vm-fixtures/${seed_prefs}"
    [ -f "${psrc}" ] || psrc="${SCEN_DIR}/fixtures/${seed_prefs}"
    [ -f "${psrc}" ] || die "seed_prefs fixture not found: ${seed_prefs}"
    cp "${psrc}" "${RESULTS_DIR}/seed-prefs.json"
    local profiles
    profiles="$(python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print("\n".join(sorted({s.get("profileID","Default") for s in d.get("spaces",[])})))' "${RESULTS_DIR}/spaces.json")"
    while IFS= read -r profile; do
      [ -n "${profile}" ] || continue
      log "seeding Preferences into profile '${profile}'"
      vm_ssh "mkdir -p '${REMOTE_UDD}/${profile}'"
      vm_scp_to "${psrc}" "${REMOTE_UDD}/${profile}/Preferences"
    done <<< "${profiles}"
  fi
  if [ "${spaces_absent}" = "True" ]; then
    # A fresh profile has no spaces.json. The folder exists, so the first save
    # after an import can create the file.
    vm_ssh "rm -rf '${REMOTE_SCEN}/fresh' && mkdir -p '${REMOTE_SCEN}/fresh'"
    REMOTE_SPACES="${REMOTE_SCEN}/fresh/spaces.json"
  fi
  REMOTE_IMPORT_ARC=""
  if [ -n "${import_arc_dir}" ]; then
    local asrc="${REPO}/${import_arc_dir}"
    [ -f "${asrc}/StorableSidebar.json" ] || die "arc import fixture not found: ${import_arc_dir}"
    vm_ssh "rm -rf '${REMOTE_SCEN}/arc'"
    # -r: Arc keeps Local State one folder down, under "User Data".
    scp "${SSH_OPTS[@]}" -qr "${asrc}" "${SSH_USER}@${VM_IP}:${REMOTE_SCEN}/arc"
    REMOTE_IMPORT_ARC="${REMOTE_SCEN}/arc"
  fi
  REMOTE_IMPORT_ZEN=""
  if [ -n "${import_zen_dir}" ]; then
    local zsrc="${REPO}/${import_zen_dir}"
    [ -f "${zsrc}/profiles.ini" ] || die "zen import fixture not found: ${import_zen_dir}"
    vm_ssh "rm -rf '${REMOTE_SCEN}/zen'"
    scp "${SSH_OPTS[@]}" -qr "${zsrc}" "${SSH_USER}@${VM_IP}:${REMOTE_SCEN}/zen"
    REMOTE_IMPORT_ZEN="${REMOTE_SCEN}/zen"
  fi
  REMOTE_IMPORT_CHROME=""
  if [ -n "${import_chrome_dir}" ]; then
    local csrc="${REPO}/${import_chrome_dir}"
    [ -f "${csrc}/Local State" ] || die "chrome import fixture not found: ${import_chrome_dir}"
    # History visits are days before now, so each History file is packed
    # here. A committed one would age out of the importer's 90-day window.
    local cstage twin
    cstage="$(mktemp -d)"
    cp -R "${csrc}/." "${cstage}/"
    for twin in "${cstage}"/*/history.json; do
      [ -f "${twin}" ] || continue
      python3 "${REPO}/build/scripts/chromium-history-pack.py" "${twin}" "$(dirname "${twin}")/History" \
        || { rm -rf "${cstage}"; die "could not pack ${twin}"; }
    done
    vm_ssh "rm -rf '${REMOTE_SCEN}/chrome'"
    # -r: each profile is a folder next to Local State.
    scp "${SSH_OPTS[@]}" -qr "${cstage}" "${SSH_USER}@${VM_IP}:${REMOTE_SCEN}/chrome"
    rm -rf "${cstage}"
    REMOTE_IMPORT_CHROME="${REMOTE_SCEN}/chrome"
  fi
  REMOTE_IMPORT_FIREFOX=""
  if [ -n "${import_firefox_dir}" ]; then
    local ffsrc="${REPO}/${import_firefox_dir}"
    [ -f "${ffsrc}/profiles.ini" ] || die "firefox import fixture not found: ${import_firefox_dir}"
    # Visit times are days before now, so each places.sqlite with a
    # visits.json twin is repacked here. A committed one would age out of the
    # importer's 90-day window.
    local ffstage twin
    ffstage="$(mktemp -d)"
    cp -R "${ffsrc}/." "${ffstage}/"
    for twin in "${ffstage}"/Profiles/*/visits.json; do
      [ -f "${twin}" ] || continue
      python3 "${REPO}/build/scripts/places-pack.py" \
        "$(dirname "${twin}")/bookmarks.json" "$(dirname "${twin}")/places.sqlite" "${twin}" \
        || { rm -rf "${ffstage}"; die "could not pack ${twin}"; }
    done
    vm_ssh "rm -rf '${REMOTE_SCEN}/firefox'"
    # -r: each profile is a folder under Profiles.
    scp "${SSH_OPTS[@]}" -qr "${ffstage}" "${SSH_USER}@${VM_IP}:${REMOTE_SCEN}/firefox"
    rm -rf "${ffstage}"
    REMOTE_IMPORT_FIREFOX="${REMOTE_SCEN}/firefox"
  fi
}

# stage_extension <repo-relative dir>: copies an unpacked extension into the VM
# and loads it at launch. A manifest "key" fixes its id, so a seed_prefs file
# can pin it.
stage_extension() {
  local dir="$1"
  [ -n "${dir}" ] || return 0
  [ -f "${REPO}/${dir}/manifest.json" ] || die "extension fixture not found: ${dir}"
  vm_ssh "rm -rf '${REMOTE_SCEN}/ext'"
  scp "${SSH_OPTS[@]}" -qr "${REPO}/${dir}" "${SSH_USER}@${VM_IP}:${REMOTE_SCEN}/ext"
  CHROMIUM_FLAGS="${CHROMIUM_FLAGS:-} --load-extension=${REMOTE_SCEN}/ext"
  log "loading extension ${dir}"
}

# apply_full_keyboard_access <"True"|""> - toggles System Settings > Keyboard
# > "Use keyboard navigation to move focus between controls" BEFORE Atrium
# launches (the settings tab row's focus ring is only
# reachable via Tab, not just arrow keys, when this is on - AppKit does not
# make non-text/list controls Tab-stops otherwise). Set pre-launch rather
# than live: AppKit apps read this at process start, and there is no
# reliable way to force a live distributed-notification re-read from a
# scripted probe. Left at the VM's default (off) when the scenario does not
# ask for it - never reset explicitly, since `bring_up_vm` already restores
# the clean snapshot (default off) unless SKIP_RESTORE=1.
apply_full_keyboard_access() {
  local requested="$1"
  [ "${requested}" = "True" ] || return 0
  log "enabling Full Keyboard Access (AppleKeyboardUIMode=2) before launch"
  vm_ssh "defaults write -g AppleKeyboardUIMode -int 2" >/dev/null 2>&1 || true
}

# Pre-launch, as with Full Keyboard Access above: AppKit reads the style at
# process start. Through System Events, NOT `defaults write` - that sets the key
# and a launched app still reads Light, even after a cfprefsd flush, because the
# appearance is a session property loginwindow owns rather than a preference the
# process reads. osascript asks loginwindow, which broadcasts it.
apply_system_appearance() {
  local requested="$1"
  [ -n "${requested}" ] || return 0
  local want
  if [ "${requested}" = "dark" ] || [ "${requested}" = "Dark" ]; then
    want=true
  elif [ "${requested}" = "light" ] || [ "${requested}" = "Light" ]; then
    want=false
  else
    die "system_appearance must be 'dark' or 'light', got '${requested}'"
  fi
  log "setting ${requested} appearance before launch"
  vm_ssh "osascript -e 'tell application \"System Events\" to tell appearance preferences to set dark mode to ${want}'" >/dev/null 2>&1 || true
  local readback
  readback="$(vm_ssh "defaults read -g AppleInterfaceStyle 2>/dev/null || echo absent-so-light" 2>/dev/null || true)"
  log "appearance now reads: ${readback}"
  # The readback only says the key moved. A scenario that depends on the
  # appearance still has to assert it from inside the app, since only the app
  # can say what NSApp.effectiveAppearance resolved to.
  if { [ "${want}" = "true" ] && [ "${readback}" != "Dark" ]; } ||
     { [ "${want}" = "false" ] && [ "${readback}" = "Dark" ]; }; then
    die "the appearance did not change: asked for ${requested}, key reads '${readback}'"
  fi
}

launch_atrium() {
  # --disable-component-update / --disable-background-networking are load-bearing,
  # not hygiene. Every run restores a clean snapshot with a fresh profile, so the
  # component updater re-fetches its whole catalogue each time. When it decides to
  # update everything it spawns ~36 utility processes, and on the VM's 4 cores that
  # starves the default-QoS GCD timer driving SidebarView.settleSlide - the gesture
  # commit then lands ~500s late instead of ~7s.
  # a scenario that reads a surface needs the debugging port open and
  # forwarded. Added only when the spec actually uses `eval`, so every other
  # run launches exactly as it did before - a port nobody opens cannot change
  # what a scenario measures.
  if [ "${SPEC_USES_EVAL:-0}" = "1" ]; then
    CHROMIUM_FLAGS="${CHROMIUM_FLAGS:-} --remote-debugging-port=${CDP_PORT}"
    log "eval in use: opening the debugging port on ${CDP_PORT}"
  fi
  log "launching Atrium (startup fixture staged)"
  local env_themes=""
  [ -n "${REMOTE_THEMES}" ] && env_themes="ATRIUM_THEMES_FILE='${REMOTE_THEMES}'"
  local env_routing=""
  [ -n "${REMOTE_ROUTING:-}" ] && env_routing="ATRIUM_ROUTING_FILE='${REMOTE_ROUTING}'"
  local env_hotkeys=""
  [ -n "${REMOTE_HOTKEYS:-}" ] && env_hotkeys="ATRIUM_HOTKEYS_FILE='${REMOTE_HOTKEYS}'"
  local env_settings=""
  [ -n "${REMOTE_SETTINGS:-}" ] && env_settings="ATRIUM_SETTINGS_FILE='${REMOTE_SETTINGS}'"
  local env_import=""
  [ -n "${REMOTE_IMPORT_ARC:-}" ] && env_import="ATRIUM_IMPORT_ARC_DIR='${REMOTE_IMPORT_ARC}'"
  [ -n "${REMOTE_IMPORT_ZEN:-}" ] && env_import="${env_import} ATRIUM_IMPORT_ZEN_DIR='${REMOTE_IMPORT_ZEN}'"
  [ -n "${REMOTE_IMPORT_CHROME:-}" ] && env_import="${env_import} ATRIUM_IMPORT_CHROME_DIR='${REMOTE_IMPORT_CHROME}'"
  [ -n "${REMOTE_IMPORT_FIREFOX:-}" ] && env_import="${env_import} ATRIUM_IMPORT_FIREFOX_DIR='${REMOTE_IMPORT_FIREFOX}'"
  # anything extra the launch needs in its environment, verbatim.
  # A malloc checker or a sanitizer runtime is set per RUN, not per scenario, so
  # it does not belong in the spec:
  #   ATRIUM_SCENARIO_ENV="MallocScribble=1 MallocErrorAbort=1" ./vm-scenario.sh <name>
  local env_extra="${ATRIUM_SCENARIO_ENV:-}"
  # shellcheck disable=SC2029  # remote-var expansion is intentional
  vm_ssh "nohup env ${env_extra} ATRIUM_UI_DYLIB='${DYLIB_REMOTE}' \
      ATRIUM_SPACES_FILE='${REMOTE_SPACES}' ${env_themes} ${env_routing} ${env_hotkeys} ${env_settings} ${env_import} \
      '${REMOTE_DIR}/Atrium.app/Contents/MacOS/Atrium' \
      --user-data-dir='${REMOTE_UDD}' \
      --no-first-run --no-default-browser-check \
      --disable-component-update --disable-background-networking \
      --enable-logging=stderr --v=1 ${CHROMIUM_FLAGS:-} \
      >/tmp/atrium-run.log 2>&1 & sleep 2; pgrep -x Atrium >/dev/null || echo LAUNCH_FAILED"
}

# pull_run_log - assemble the log every assertion reads.
#
# With no relaunch this is the one scp it always was. After a relaunch it is
# the concatenation of the closed segments and the live log, in launch order,
# with a marker line between them.
#
# Concatenated rather than replaced because launch_atrium's `>/tmp/atrium-run.log`
# TRUNCATES: a second launch would otherwise delete the evidence for everything
# the scenario did before it, and a persistence scenario is precisely one whose
# interesting half happened in the earlier launch. Assertions therefore see the
# whole run, and `vlog_after` against the marker is how a scenario says "this
# line came from the relaunched app" - which matters because the two launches
# write the SAME lines, so presence alone cannot tell them apart.
#
# `wait_vlog` and `vlog_cite` are deliberately NOT part of this: both grep the
# live log inside the VM, so after a relaunch they see the current launch only.
# That is the right meaning for a gate - waiting for a line the previous launch
# already wrote would return instantly and gate nothing.
pull_run_log() {
  RUN_LOG_LOCAL="${RESULTS_DIR}/atrium-run.log"
  if [ "${#RUN_LOG_SEGMENTS[@]}" -eq 0 ]; then
    vm_scp_from "/tmp/atrium-run.log" "${RUN_LOG_LOCAL}" 2>/dev/null || true
    return 0
  fi
  local live="${RESULTS_DIR}/atrium-run-live.log" seg n=1
  : >"${live}"
  vm_scp_from "/tmp/atrium-run.log" "${live}" 2>/dev/null || true
  : >"${RUN_LOG_LOCAL}"
  for seg in "${RUN_LOG_SEGMENTS[@]}"; do
    cat "${seg}" >>"${RUN_LOG_LOCAL}"
    n=$(( n + 1 ))
    printf '%s %d begins here\n' "${RELAUNCH_MARKER}" "${n}" >>"${RUN_LOG_LOCAL}"
  done
  cat "${live}" >>"${RUN_LOG_LOCAL}"
}

# start_vm_sampler / pull_vm_sampler - opt-in system sampling inside the VM,
# enabled with ATRIUM_VM_SAMPLE=1. Off by default: it costs a background
# process in a 4-core VM, which is the very resource under investigation.
#
# Exists because a scenario's artifacts hold only atrium-run.log - the app's
# own output. When a gesture commit lands ~300s late while the app's run loop
# is demonstrably healthy, that log cannot say what the VM was doing instead
#. Samples load, the stuck-process count and disk throughput - the
# quantities hand sampling could NOT settle, since the stalling and the clean
# runs both showed heavy I/O.
start_vm_sampler() {
  [ "${ATRIUM_VM_SAMPLE:-0}" = "1" ] || return 0
  log "ATRIUM_VM_SAMPLE=1 - sampling VM system state every 2s"
  # Captures RAW top/iostat lines with a timestamp rather than parsing in the
  # VM. A first version extracted named fields with awk and mislabelled two of
  # them (total processes read as stuck, iostat columns transposed) - wrong
  # labels on measurements are worse than no measurements, so parsing happens
  # on the host where the output can be inspected.
  vm_ssh "nohup sh -c 'while :; do
      echo \"--- \$(date +%H:%M:%S)\";
      top -l 1 -n 0 | sed -n \"1,8p\";
      iostat -d disk0 | tail -2;
      sleep 2;
    done' >/tmp/vm-sample.log 2>&1 < /dev/null &" >/dev/null 2>&1 || true
}

pull_vm_sampler() {
  [ "${ATRIUM_VM_SAMPLE:-0}" = "1" ] || return 0
  vm_ssh "pkill -f 'while :; do' >/dev/null 2>&1 || true" >/dev/null 2>&1 || true
  vm_scp_from "/tmp/vm-sample.log" "${RESULTS_DIR}/vm-sample.log" 2>/dev/null || true
  [ -s "${RESULTS_DIR}/vm-sample.log" ] && \
    log "  vm sampler: $(grep -c '^---' "${RESULTS_DIR}/vm-sample.log") samples -> vm-sample.log"
}

# report_capture_dropout <id> <vmdir> - explain a capture that pulled no frames.
# Both the rapidcap call and the scp swallow their errors, so a dropout used to
# leave nothing behind and there was no way to tell a failed capture from a
# failed transfer. Pull rapidcap's own log and say what the VM side
# actually had.
report_capture_dropout() {
  local id="$1" vmdir="$2"
  local remote_count vmdir_q
  vmdir_q="$(vm_q "${vmdir}")"
  remote_count="$(vm_ssh "ls -1 ${vmdir_q}/frame-*.png 2>/dev/null | wc -l | tr -d ' '" 2>/dev/null || echo "?")"
  vm_scp_from "/tmp/rapidcap-${id}.log" "${RESULTS_DIR}/rapidcap-${id}.log" 2>/dev/null || true
  log "  WARNING: no frames pulled for capture '${id}' - VM held ${remote_count} frame(s) in ${vmdir}"
  if [ -s "${RESULTS_DIR}/rapidcap-${id}.log" ]; then
    log "  rapidcap said: $(tail -3 "${RESULTS_DIR}/rapidcap-${id}.log" | tr '\n' ' ')"
  fi
  # Its exit status and stderr, kept beside the frames in the VM. This is the
  # part that was missing: without them a dropout could only be described, not
  # explained.
  local rc err
  rc="$(vm_ssh "cat ${vmdir_q}/rapidcap.rc 2>/dev/null" 2>/dev/null | tr -d '\r')"
  err="$(vm_ssh "tail -5 ${vmdir_q}/rapidcap.err 2>/dev/null" 2>/dev/null | tr '\n' ' ')"
  log "  rapidcap exit=${rc:-unknown} stderr=${err:-<empty>}"
  if [ -z "${rc}" ]; then
    log "  no exit status either - rapidcap did not run, or the ssh call carrying it did not"
  fi
  # A capture that returned nothing is a HARNESS failure, not a product one.
  # Left as a warning it becomes whatever assertion wanted the frame - which is
  # how a run with a working Settings pane reported a theme that never changed.
  HARNESS_ERRORS=$((HARNESS_ERRORS + 1))
}

# serve the deployed HTML fixtures over http inside the VM.
#
# Needed because a peek only opens for an http/https navigation - ShouldAutoPeek
# returns false on any other scheme - while every fixture here is loaded as
# file://, so no file-served page can trigger one. build/vm-fixtures/peek-test
# .html has always pointed at http://127.0.0.1:8000/, which is what past
# hand-driven peek probing used; this makes that available to a scenario.
#
# Opt-in per scenario, because it costs a process in a 4-core VM and only peek
# work needs it.
start_fixture_server() {
  [ "${SERVE_FIXTURES}" = "True" ] || return 0
  log "serving fixtures over http://127.0.0.1:8000 inside the VM"
  vm_ssh "cd '${REMOTE_DIR}/vm-fixtures' && nohup python3 -m http.server 8000 \
      >/tmp/fixture-server.log 2>&1 & sleep 1" >/dev/null 2>&1 || true
  # Confirm rather than assume: a dead server here makes every later assertion
  # fail against a blank page, which reads as the feature being broken.
  if vm_ssh "curl -fsS -o /dev/null http://127.0.0.1:8000/" >/dev/null 2>&1; then
    log "  fixture server up"
  else
    log "  fixture server did NOT come up - pages served over http will not load"
  fi
}

stop_fixture_server() {
  [ "${SERVE_FIXTURES}" = "True" ] || return 0
  vm_ssh "pkill -f 'http.server 8000'" >/dev/null 2>&1 || true
}

# shutdown_app - stop the app the way a user would, and wait for it to be gone.
#
# Cmd+Q first, because the app's own quit path is what emits the shutdown
# lines a teardown assertion is about to read; SIGTERM only as the fallback, so
# a wedged app cannot stall the run.
#
# THE FALLBACK IS NOW A RECORDED FAILURE, not a log line. This used to
# say which route it took and stop there, so the app died either way and every
# assertion downstream was graded as if the chord had worked. Measured
# 2026-08-10: settings-confirm-quit-toggle scored 12 passed, 0 failed while its
# own log carried "Cmd+Q did not stop Atrium within 15s - sending SIGTERM" - the
# chord had been swallowed by the settings window and nothing in the
# tally said so. Same grader as the `quit` assertion, so the two cannot drift.
shutdown_app() {
  if [ "${APP_QUIT}" = "1" ]; then
    log "shutdown: ${APP_PROCESS} already stopped by a 'quit' assertion"
    return 0
  fi
  log "shutdown: Cmd+Q, then waiting for ${APP_PROCESS} to exit"
  local before still="" after_fallback="" fallback=none
  before="$(vm_ssh pgrep -x "${APP_PROCESS}" 2>/dev/null || true)"
  vm_ssh "${REMOTE_TOOLS}/cgevent" keypress 12 cmd >/dev/null 2>&1 || true
  local deadline=$(( SECONDS + 15 ))
  while [ "${SECONDS}" -le "${deadline}" ]; do
    still="$(vm_ssh pgrep -x "${APP_PROCESS}" 2>/dev/null || true)"
    [ -z "${still}" ] && break
    sleep 1
  done
  if [ -n "${still}" ]; then
    log "shutdown: Cmd+Q did not stop ${APP_PROCESS} within 15s - sending SIGTERM"
    fallback=SIGTERM
    vm_ssh "pkill -x '${APP_PROCESS}'" >/dev/null 2>&1 || true
    # Poll rather than sleep once: the verdict below turns on whether the
    # fallback worked, and a flat two seconds is not long enough to say it did
    # not. An app tearing down a dozen renderers takes longer than that, and
    # calling it wedged would be the same kind of unearned claim this task is
    # about - in the other direction.
    local kill_deadline=$(( SECONDS + 5 ))
    while :; do
      after_fallback="$(vm_ssh pgrep -x "${APP_PROCESS}" 2>/dev/null || true)"
      [ -z "${after_fallback}" ] && break
      [ "${SECONDS}" -ge "${kill_deadline}" ] && break
      sleep 1
    done
  else
    log "shutdown: ${APP_PROCESS} exited on Cmd+Q"
  fi
  # `|| true`: the FAIL is already counted, and a red verdict must not abort a
  # relaunch mid-scenario - the actions after it still have to run and be graded.
  grade_quit "quit:shutdown" 15 "${fallback}" "${before}" "${still}" \
    "${after_fallback}" || true
  APP_QUIT=1
}

teardown() {
  log "teardown: quitting app + pulling artifacts"
  # Kill BEFORE pulling. The other order was this function's own version of the
  # bug above: it saved a log that stopped short of the quit, so the artifact
  # left behind for a human to read was missing exactly the part a quit-time
  # question is about.
  #
  # Safe here ONLY because this runs inside the disposable atrium-probe VM
  # (single-tenant, snapshot-reset between runs). Do NOT copy this idiom to a
  # host-side script - `pkill -x Atrium` cannot tell instances apart and has
  # already killed an unrelated dev's browser on a shared host. Kill by PID
  # there instead (see docs/BUILD.md).
  vm_ssh "pkill -x Atrium" >/dev/null 2>&1 || true
  local deadline=$(( SECONDS + 5 ))
  while [ "${SECONDS}" -le "${deadline}" ]; do
    [ -z "$(vm_ssh pgrep -x Atrium 2>/dev/null || true)" ] && break
    sleep 1
  done
  pull_run_log
  pull_vm_sampler
  stop_fixture_server
}

# _renderer_pids - the live renderer child pids, space separated.
#
# crash_renderer takes this before and after its signal, so the run can say
# EXACTLY which processes went away rather than that the intended one did. The
# blanket match is safe for the same reason teardown's `pkill -x Atrium` is:
# atrium-probe is single-tenant and snapshot-reset between runs.
_renderer_pids() {
  vm_ssh "pgrep -f -- '--type=renderer' || true" 2>/dev/null | tr -d '\r' | tr '\n' ' '
}

# forwards the VM's debugging port so cdp-eval.mjs can reach it from
# here. vm-cdp.sh owns the how - it kills whatever holds the port first, which
# is the failure that cost three separate confusions when it was hand-rolled.
ensure_cdp_tunnel() {
  [ "${CDP_TUNNEL_WANTED:-0}" = "1" ] || return 0
  [ "${CDP_TUNNEL_UP:-0}" = "1" ] && return 0
  local rc=0
  ATRIUM_CDP_PORT="${CDP_PORT}" TART_HOME="${TART_HOME:-$HOME/.tart}" \
    "${REPO}/build/scripts/vm-cdp.sh" >/dev/null 2>&1 || rc=$?
  if [ "${rc}" != "0" ]; then
    log "  cdp tunnel did not come up on ${CDP_PORT} (rc=${rc})"
    return 1
  fi
  CDP_TUNNEL_UP=1
  log "  cdp tunnel up on ${CDP_PORT}"
}

# eval_point <eval-id> <action>: sets EVAL_X and EVAL_Y from an earlier eval's
# "x,y" answer, a point only the running app knows. Records a FAIL otherwise.
# Not called in $(...): the FAIL count would stay in the subshell.
eval_point() {
  local point="${EVAL_RESULT[$1]:-}"
  point="${point#\"}"; point="${point%\"}"
  if [[ ! "${point}" =~ ^-?[0-9]+,-?[0-9]+$ ]]; then
    record_result FAIL "$2:$1" "eval $1 gave no x,y point to $2: '${point}'"
    return 1
  fi
  EVAL_X="${point%,*}"; EVAL_Y="${point#*,}"
}

# --- action dispatch --------------------------------------------------------
do_action() {
  local record="$1"
  local type="${record%%$'\t'*}"
  case "${type}" in
    settle)
      local ms; ms="$(field "${record}" ms)"; [ -n "${ms}" ] || ms=1000
      ms=$(scaled_wait "${ms}")
      log "action settle ${ms}ms"; sleep_ms "${ms}"
      ;;
    cdp_wheel)
      # a wheel over CDP, straight into the page's widget host.
      local wtarget wx wy wdy wsteps wout
      wtarget="$(field "${record}" target)"
      wx="$(field "${record}" x)"; wy="$(field "${record}" y)"
      wdy="$(field "${record}" dy)"; wsteps="$(field "${record}" steps)"
      [ -n "${wsteps}" ] || wsteps=6
      log "action cdp_wheel ${wtarget} at ${wx},${wy} dy=${wdy} steps=${wsteps}"
      ensure_cdp_tunnel || log "  (tunnel unavailable)"
      wout="$(node "${REPO}/build/scripts/cdp-wheel.mjs" "${CDP_PORT}" "${wtarget}" "${wx}" "${wy}" "${wdy}" "${wsteps}" 2>&1 | tail -1 || true)"
      log "  cdp_wheel -> ${wout}"
      ;;
    cdp_move)
      # mouseMoved over CDP, past AppKit, into the page's widget host.
      local mtarget cmx cmy mout
      mtarget="$(field "${record}" target)"
      cmx="$(field "${record}" x)"; cmy="$(field "${record}" y)"
      log "action cdp_move ${mtarget} at ${cmx},${cmy}"
      ensure_cdp_tunnel || log "  (tunnel unavailable)"
      mout="$(node "${REPO}/build/scripts/cdp-wheel.mjs" "${CDP_PORT}" "${mtarget}" "${cmx}" "${cmy}" 0 4 move 2>&1 | tail -1 || true)"
      log "  cdp_move -> ${mout}"
      ;;
    eval)
      # read a value out of a surface. Nothing else here can tell a
      # gesture that never ARRIVED from one that arrived and did nothing, and
      # that difference has cost two sessions of guessing.
      local eid target expr out
      eid="$(field "${record}" id)"
      target="$(field "${record}" target)"; [ -n "${target}" ] || target="/sidebar"
      expr="$(field "${record}" expression)"
      log "action eval id=${eid} target=${target}"
      ensure_cdp_tunnel || log "  (tunnel unavailable)"
      out="$(node "${REPO}/build/scripts/cdp-eval.mjs" "${CDP_PORT}" "${target}" "${expr}" 2>&1 | tail -1 || true)"
      [ -n "${out}" ] || out="BLIND cdp-eval produced no output"
      case "${out}" in
        OK\ *)
          EVAL_RESULT["${eid}"]="${out#OK }"
          log "  eval ${eid} -> ${EVAL_RESULT[${eid}]}"
          ;;
        *)
          # Deliberately NOT stored. A probe that could not look must not leave
          # a value behind for an assertion to compare against - that is how a
          # blind read turns into a confident wrong answer.
          log "  eval ${eid} BLIND: ${out}"
          ;;
      esac
      ;;
    capture)
      local id dur fps vmdir vmdir_q
      id="$(field "${record}" id)"
      dur="$(field "${record}" duration_ms)"; [ -n "${dur}" ] || dur=400
      fps="$(field "${record}" fps)"; [ -n "${fps}" ] || fps=20
      vm_num "${dur}" duration_ms
      vm_num "${fps}" fps
      vmdir="/tmp/scen-cap-${id}"
      vmdir_q="$(vm_q "${vmdir}")"
      log "action capture id=${id} ${dur}ms @${fps}fps"
      # rapidcap's own stderr and exit status are KEPT, in the VM, next to the
      # frames. Both used to be discarded by this line, so a capture that
      # produced nothing left nothing to explain it and the run reported only
      # "rapidcap left no log" - which is not a cause, it is the absence of one
      #. Suppressing errors on the step that produces the
      # measurement is how a failure becomes a data point.
      vm_ssh "rm -rf ${vmdir_q}; mkdir -p ${vmdir_q}; \
          '${REMOTE_TOOLS}/rapidcap' ${vmdir_q} ${dur} ${fps} \
              >${vmdir_q}/rapidcap.out 2>${vmdir_q}/rapidcap.err; \
          echo \$? > ${vmdir_q}/rapidcap.rc" >/dev/null 2>&1 || true
      local localdir="${RESULTS_DIR}/frames/${id}"
      mkdir -p "${localdir}"
      vm_scp_from "${vmdir}/" "${RESULTS_DIR}/frames/" 2>/dev/null || true
      # rapidcap writes into <vmdir>; scp of the dir lands it as frames/<basename>.
      [ -d "${RESULTS_DIR}/frames/scen-cap-${id}" ] && localdir="${RESULTS_DIR}/frames/scen-cap-${id}"
      local last
      last="$(find "${localdir}" -name 'frame-*.png' 2>/dev/null | sort | tail -1)"
      if [ -n "${last}" ]; then
        CAPTURE_LAST["${id}"]="${last}"
        log "  captured last frame: $(basename "${last}")"
      else
        report_capture_dropout "${id}" "${vmdir}"
      fi
      ;;
    drag_during_capture)
      # rapidcap backgrounded and the drag fired from ONE ssh session, same
      # shape as key_during_capture below, so the burst holds the whole drag.
      local x1 y1 x2 y2 steps id dur fps delay delay_s vmdir vmdir_q id_q
      x1="$(field "${record}" x1)"; y1="$(field "${record}" y1)"
      x2="$(field "${record}" x2)"; y2="$(field "${record}" y2)"
      steps="$(field "${record}" steps)"; [ -n "${steps}" ] || steps=8
      id="$(field "${record}" capture_id)"
      dur="$(field "${record}" duration_ms)"; [ -n "${dur}" ] || dur=2000
      fps="$(field "${record}" fps)"; [ -n "${fps}" ] || fps=30
      delay="$(field "${record}" drag_delay_ms)"; [ -n "${delay}" ] || delay=300
      vm_num "${x1}" x1; vm_num "${y1}" y1; vm_num "${x2}" x2; vm_num "${y2}" y2
      vm_num "${steps}" steps; vm_num "${dur}" duration_ms; vm_num "${fps}" fps
      vm_num "${delay}" drag_delay_ms
      vmdir="/tmp/scen-cap-${id}"
      vmdir_q="$(vm_q "${vmdir}")"
      id_q="$(vm_q "${id}")"
      delay_s="$(awk "BEGIN{printf \"%.3f\", ${delay}/1000}")"
      log "action drag_during_capture ${x1},${y1} -> ${x2},${y2} steps=${steps} capture_id=${id} ${dur}ms @${fps}fps drag_delay=${delay}ms"
      vm_ssh "rm -rf ${vmdir_q}; '${REMOTE_TOOLS}/rapidcap' ${vmdir_q} ${dur} ${fps} >/tmp/rapidcap-${id_q}.log 2>&1 & RAPID_PID=\$!; sleep ${delay_s}; '${REMOTE_TOOLS}/cgevent' drag ${x1} ${y1} ${x2} ${y2} ${steps}; wait \${RAPID_PID}" >/dev/null 2>&1 || true
      local localdir="${RESULTS_DIR}/frames/${id}"
      mkdir -p "${localdir}"
      vm_scp_from "${vmdir}/" "${RESULTS_DIR}/frames/" 2>/dev/null || true
      [ -d "${RESULTS_DIR}/frames/scen-cap-${id}" ] && localdir="${RESULTS_DIR}/frames/scen-cap-${id}"
      local last
      last="$(find "${localdir}" -name 'frame-*.png' 2>/dev/null | sort | tail -1)"
      if [ -n "${last}" ]; then
        CAPTURE_LAST["${id}"]="${last}"
        log "  captured last frame: $(basename "${last}")"
      else
        report_capture_dropout "${id}" "${vmdir}"
      fi
      ;;
    move_during_capture)
      # The hover half of key_during_capture. The floating panel's reveal arms
      # on a mouse-entered and nothing else, and its 240ms slide is over before
      # a plain `move` then `capture` pair finishes its second round trip.
      local mx my id dur fps delay after_move mid_at vmdir vmdir_q id_q
      mx="$(field "${record}" x)"; my="$(field "${record}" y)"
      id="$(field "${record}" capture_id)"
      dur="$(field "${record}" duration_ms)"; [ -n "${dur}" ] || dur=1200
      fps="$(field "${record}" fps)"; [ -n "${fps}" ] || fps=25
      delay="$(field "${record}" move_delay_ms)"; [ -n "${delay}" ] || delay=300
      after_move="$(field "${record}" mid_after_move_ms)"
      mid_at="$(field "${record}" mid_at_ms)"
      [ -n "${mid_at}" ] || [ -n "${after_move}" ] || mid_at="${delay}"
      vm_num "${mx}" x; vm_num "${my}" y
      vm_num "${dur}" duration_ms
      vm_num "${fps}" fps
      vm_num "${delay}" move_delay_ms
      vmdir="/tmp/scen-cap-${id}"
      vmdir_q="$(vm_q "${vmdir}")"
      id_q="$(vm_q "${id}")"
      local delay_s; delay_s="$(awk "BEGIN{printf \"%.3f\", ${delay}/1000}")"
      log "action move_during_capture ${mx},${my} capture_id=${id} ${dur}ms @${fps}fps move_delay=${delay}ms mid_at=${mid_at:--}ms mid_after_move=${after_move:--}ms"
      vm_ssh "rm -rf ${vmdir_q}; '${REMOTE_TOOLS}/rapidcap' ${vmdir_q} ${dur} ${fps} >/tmp/rapidcap-${id_q}.log 2>&1 & RAPID_PID=\$!; T0=\$(perl -MTime::HiRes -e 'print int(Time::HiRes::time()*1000)'); sleep ${delay_s}; '${REMOTE_TOOLS}/cgevent' move ${mx} ${my}; T1=\$(perl -MTime::HiRes -e 'print int(Time::HiRes::time()*1000)'); wait \${RAPID_PID}; mkdir -p ${vmdir_q}; [ -n \"\${T0}\" ] && [ -n \"\${T1}\" ] && echo \$((T1-T0)) >${vmdir_q}/chord-return.ms" >/dev/null 2>&1 || true
      local localdir="${RESULTS_DIR}/frames/${id}"
      mkdir -p "${localdir}"
      vm_scp_from "${vmdir}/" "${RESULTS_DIR}/frames/" 2>/dev/null || true
      [ -d "${RESULTS_DIR}/frames/scen-cap-${id}" ] && localdir="${RESULTS_DIR}/frames/scen-cap-${id}"
      local last mid mid_why
      last="$(find "${localdir}" -name 'frame-*.png' 2>/dev/null | sort | tail -1)"
      set +e
      mid="$(python3 "${MID_FRAME}" "${localdir}" "${delay}" "${mid_at}" "${after_move}" \
        "${localdir}/chord-return.ms" 2>"${localdir}/mid-frame.txt")"
      set -e
      mid_why="$(tr '\n' ' ' <"${localdir}/mid-frame.txt" 2>/dev/null || true)"
      if [ -n "${last}" ]; then
        CAPTURE_LAST["${id}"]="${last}"
        log "  captured settled frame: $(basename "${last}")"
      else
        report_capture_dropout "${id}" "${vmdir}"
      fi
      if [ -n "${mid}" ]; then
        CAPTURE_LAST["${id}_mid"]="${mid}"
        log "  captured mid frame: $(basename "${mid}") - ${mid_why}"
      else
        log "  WARNING: no mid frame for capture '${id}' - ${mid_why}"
      fi
      ;;
    key_during_capture)
      # catches a transition too short for the sequential
      # key-then-capture pair above to reliably land inside - each of those
      # is its own ssh round trip, and a ~150-300ms slide animation can
      # start AND finish in that gap (observed: the plain `capture` action's
      # first frame already showed the settled state). This backgrounds
      # rapidcap and fires the keypress from ONE ssh session, so the delay
      # between them is an in-VM `sleep`, not two host<->VM round trips.
      # One burst, two capture ids: "<id>" settled, "<id>_mid" mid-transition.
      # lib/mid_frame.py chooses the mid one and holds the argument for why a
      # `mid_at_ms` probe lands in front of the transition.
      local combo id dur fps delay mid_at after_chord after_return vmdir vmdir_q id_q
      combo="$(field "${record}" combo)"
      id="$(field "${record}" capture_id)"
      dur="$(field "${record}" duration_ms)"; [ -n "${dur}" ] || dur=1200
      fps="$(field "${record}" fps)"; [ -n "${fps}" ] || fps=25
      delay="$(field "${record}" key_delay_ms)"; [ -n "${delay}" ] || delay=300
      after_chord="$(field "${record}" mid_after_chord_ms)"
      after_return="$(field "${record}" mid_after_chord_return_ms)"
      mid_at="$(field "${record}" mid_at_ms)"
      [ -n "${mid_at}" ] || [ -n "${after_chord}" ] || [ -n "${after_return}" ] || mid_at="${delay}"
      vm_num "${dur}" duration_ms
      vm_num "${fps}" fps
      vm_num "${delay}" key_delay_ms
      vmdir="/tmp/scen-cap-${id}"
      vmdir_q="$(vm_q "${vmdir}")"
      id_q="$(vm_q "${id}")"
      local -a parts
      IFS='+' read -ra parts <<<"${combo}"
      local key="${parts[-1]}"
      local mods=("${parts[@]:0:${#parts[@]}-1}")
      local keycode; keycode="$(keycode_for "${key}")" || die "unknown key in combo '${combo}'"
      local mod_str=""
      [ "${#mods[@]}" -gt 0 ] && mod_str="$(IFS='+'; echo "${mods[*]}")"
      local mod_arg=""
      [ -n "${mod_str}" ] && mod_arg=" $(vm_q "${mod_str}")"
      local delay_s; delay_s="$(awk "BEGIN{printf \"%.3f\", ${delay}/1000}")"
      log "action key_during_capture combo=${combo} capture_id=${id} ${dur}ms @${fps}fps key_delay=${delay}ms mid_at=${mid_at:--}ms mid_after_chord=${after_chord:--}ms mid_after_chord_return=${after_return:--}ms"
      # chord-return.ms: ms from backgrounding rapidcap to `cgevent` returning,
      # which tells mid_frame.py which stall in the burst the chord sits in.
      # No perl means no file, and the picker falls back (see its docstring).
      vm_ssh "rm -rf ${vmdir_q}; '${REMOTE_TOOLS}/rapidcap' ${vmdir_q} ${dur} ${fps} >/tmp/rapidcap-${id_q}.log 2>&1 & RAPID_PID=\$!; T0=\$(perl -MTime::HiRes -e 'print int(Time::HiRes::time()*1000)'); sleep ${delay_s}; '${REMOTE_TOOLS}/cgevent' keypress ${keycode}${mod_arg}; T1=\$(perl -MTime::HiRes -e 'print int(Time::HiRes::time()*1000)'); wait \${RAPID_PID}; mkdir -p ${vmdir_q}; [ -n \"\${T0}\" ] && [ -n \"\${T1}\" ] && echo \$((T1-T0)) >${vmdir_q}/chord-return.ms" >/dev/null 2>&1 || true
      local localdir="${RESULTS_DIR}/frames/${id}"
      mkdir -p "${localdir}"
      vm_scp_from "${vmdir}/" "${RESULTS_DIR}/frames/" 2>/dev/null || true
      [ -d "${RESULTS_DIR}/frames/scen-cap-${id}" ] && localdir="${RESULTS_DIR}/frames/scen-cap-${id}"
      local last mid mid_why
      last="$(find "${localdir}" -name 'frame-*.png' 2>/dev/null | sort | tail -1)"
      set +e
      mid="$(python3 "${MID_FRAME}" "${localdir}" "${delay}" "${mid_at}" "${after_chord}" \
        "${localdir}/chord-return.ms" "${after_return}" 2>"${localdir}/mid-frame.txt")"
      set -e
      mid_why="$(tr '\n' ' ' <"${localdir}/mid-frame.txt" 2>/dev/null || true)"
      if [ -n "${last}" ]; then
        CAPTURE_LAST["${id}"]="${last}"
        log "  captured settled frame: $(basename "${last}")"
      else
        report_capture_dropout "${id}" "${vmdir}"
      fi
      if [ -n "${mid}" ]; then
        CAPTURE_LAST["${id}_mid"]="${mid}"
        log "  captured mid frame: $(basename "${mid}") - ${mid_why}"
      else
        log "  WARNING: no mid frame for capture '${id}' - ${mid_why}"
      fi
      ;;
    activate)
      # Make the app frontmost before driving synthetic input. launch_atrium
      # starts Atrium from ssh (not `open`), which does NOT guarantee it wins
      # focus: on a clean snapshot Finder and any pending notification alert
      # sit in front, and every cgevent key/click then lands in the WRONG app
      # (observed: cmd+t opened a Finder tab and the typed URL went nowhere).
      # System Events also dismisses stray notification banners by activating
      # over them. Idempotent - safe to repeat before each input burst.
      local aapp
      aapp="$(field "${record}" app)"; [ -n "${aapp}" ] || aapp="${APP_PROCESS}"
      log "action activate ${aapp}"
      vm_ssh "osascript -e 'tell application \"System Events\" to set frontmost of first process whose name is \"${aapp}\" to true'" >/dev/null 2>&1 || true
      ;;
    frontmost)
      # "<app>,<focused window subrole>", stored raw for eval_equals. A read
      # that fails stores nothing, as eval does.
      local fid fout
      fid="$(field "${record}" id)"
      fout="$(vm_ssh "osascript -e 'tell application \"System Events\" to tell (first process whose frontmost is true)' \
        -e 'try' -e 'set r to value of attribute \"AXSubrole\" of (value of attribute \"AXFocusedWindow\")' \
        -e 'on error' -e 'set r to \"none\"' -e 'end try' -e 'return name & \",\" & r' -e 'end tell'" 2>/dev/null || true)"
      log "action frontmost id=${fid} -> ${fout:-<no answer>}"
      [ -z "${fout}" ] || EVAL_RESULT["${fid}"]="${fout}"
      ;;
    reopen)
      # what clicking the Dock icon of an ALREADY-RUNNING app does.
      # `open -a` on a running app sends the reopen Apple Event, which is the
      # same kAEReopenApplication that reaches
      # applicationShouldHandleReopen:hasVisibleWindows: - the entry point
      # that crashed. `activate` above cannot stand in for it: System Events
      # only raises the app and never sends that event, so a scenario built
      # on activate would report nothing about this path.
      #
      # Deliberately NOT `|| true`-swallowed the way activate is: activate is
      # best-effort focus hygiene, while a reopen that fails to dispatch means
      # the scenario measured nothing and should say so.
      log "action reopen ${APP_PROCESS} (Dock-click Apple Event)"
      vm_ssh "open -a '${REMOTE_DIR}/Atrium.app'" 2>&1 | sed 's/^/      /' || \
        log "  reopen dispatch FAILED - the assertions after this measured nothing"
      ;;
    open_url)
      # a link handed over by another macOS app. `open -a` on a
      # RUNNING app sends the openURLs Apple Event, which is the entry point
      # -[AppController application:openURLs:] serves; typing a URL into the
      # address bar is a different path and would say nothing about it.
      #
      # Not `|| true`-swallowed, for `reopen`'s reason: a handover that failed
      # to dispatch means the assertions after it measured nothing.
      local ourl ourl_q
      ourl="$(field "${record}" url)"
      ourl_q="$(vm_q "${ourl}")"
      log "action open_url ${ourl}"
      vm_ssh "open -a '${REMOTE_DIR}/Atrium.app' ${ourl_q}" 2>&1 | sed 's/^/      /' || \
        log "  open_url dispatch FAILED - the assertions after this measured nothing"
      ;;
    key)
      local combo vlog
      combo="$(field "${record}" combo)"
      log "action key ${combo}"
      send_combo "${combo}"
      vlog="$(field "${record}" vlog)"
      # Guarded so an absent 'vlog' field (the compound then returns non-zero)
      # cannot trip `set -e` and abort the scenario mid-run.
      if [ -n "${vlog}" ]; then vlog_cite "${vlog}" "key:${combo}" || true; fi
      ;;
    key_hold_taps)
      # what `key` cannot express - one held modifier across
      # several taps. parse.py's validator already refused a missing
      # modifier/key or a taps < 2, so this trusts the record.
      local hmod hkey htaps hdelay hvlog
      hmod="$(field "${record}" modifier)"
      hkey="$(field "${record}" key)"
      htaps="$(field "${record}" taps)"
      hdelay="$(field "${record}" tap_delay_ms)"; [ -n "${hdelay}" ] || hdelay=400
      log "action key_hold_taps modifier=${hmod} key=${hkey} taps=${htaps} tap_delay_ms=${hdelay}"
      send_held_taps "${hmod}" "${hkey}" "${htaps}" "${hdelay}"
      hvlog="$(field "${record}" vlog)"
      if [ -n "${hvlog}" ]; then vlog_cite "${hvlog}" "key_hold_taps:${hmod}+${hkey}x${htaps}" || true; fi
      ;;
    type)
      # Type a literal string via cgevent's unicode string events (address
      # pill / command bar entry). Routing runs found typed-URL
      # navigation to be the RELIABLE way to drive a route in the VM, unlike
      # sidebar/tab-row clicks.
      local text text_q
      text="$(field "${record}" text)"
      text_q="$(vm_q "${text}")"
      log "action type '${text}'"
      vm_ssh "${REMOTE_TOOLS}/cgevent" typetext "${text_q}" >/dev/null 2>&1 || true
      ;;
    click)
      local cx cy mods
      cx="$(field "${record}" x)"; cy="$(field "${record}" y)"
      mods="$(field "${record}" mods)"
      local from_eval
      from_eval="$(field "${record}" from_eval)"
      if [ -n "${from_eval}" ]; then
        eval_point "${from_eval}" click || return 0
        cx="${EVAL_X}"; cy="${EVAL_Y}"
      fi
      vm_num "${cx}" x; vm_num "${cy}" y
      log "action click ${cx},${cy}${mods:+ mods=${mods}}"
      if [ -n "${mods}" ]; then
        vm_ssh "${REMOTE_TOOLS}/cgevent" modclick "${cx}" "${cy}" "$(vm_q "${mods}")" >/dev/null 2>&1 || true
      else
        vm_ssh "${REMOTE_TOOLS}/cgevent" click "${cx}" "${cy}" >/dev/null 2>&1 || true
      fi
      ;;
    system_appearance)
      # A live switch with the app running, unlike the pre-launch meta field.
      apply_system_appearance "$(field "${record}" value)"
      ;;
    system_accent)
      # The macOS accent, as System Settings' own picker writes it: -1 for
      # Graphite through 6 for Pink, and DELETING the key means Multicolor.
      # `defaults` only, because nothing in the VM can drive that pane.
      local accent
      accent="$(field "${record}" value)"
      log "action system_accent ${accent}"
      if [ "${accent}" = "multicolor" ]; then
        vm_ssh "defaults delete -g AppleAccentColor" >/dev/null 2>&1 || true
      else
        vm_num "${accent}" value
        vm_ssh "defaults write -g AppleAccentColor -int ${accent}" \
          >/dev/null 2>&1 || true
      fi
      # `defaults` writes the store and posts nothing. System Settings posts
      # this, and AppKit invalidates its cached system colours off it. Never
      # kill cfprefsd first: that drops the invalidation it owes each running
      # process, and AppKit then re-reads its own stale copy.
      vm_ssh "osascript -l JavaScript -e 'ObjC.import(\"Foundation\"); \$.NSDistributedNotificationCenter.defaultCenter.postNotificationNameObjectUserInfoDeliverImmediately(\"AppleColorPreferencesChangedNotification\", \$(), \$(), true)'" >/dev/null 2>&1 || true
      ;;
    rightclick)
      # A context menu opens on the secondary button only, and the sidebar's
      # is the one surface a left click can never reach.
      local rx ry rfrom
      rx="$(field "${record}" x)"; ry="$(field "${record}" y)"
      rfrom="$(field "${record}" from_eval)"
      if [ -n "${rfrom}" ]; then
        eval_point "${rfrom}" rightclick || return 0
        rx="${EVAL_X}"; ry="${EVAL_Y}"
      fi
      vm_num "${rx}" x; vm_num "${ry}" y
      log "action rightclick ${rx},${ry}"
      vm_ssh "${REMOTE_TOOLS}/cgevent" rightclick "${rx}" "${ry}" >/dev/null 2>&1 || true
      ;;
    move)
      # a plain hover (no click/drag), needed to arm the sidebar
      # overlay's proximity-reveal zone (SidebarOverlay.revealZoneWidth) -
      # cgevent's `move` subcommand already existed for this (mouseMoved,
      # no button), just not wired into the runner before now.
      local mx my
      mx="$(field "${record}" x)"; my="$(field "${record}" y)"
      vm_num "${mx}" x; vm_num "${my}" y
      log "action move ${mx},${my}"
      vm_ssh "${REMOTE_TOOLS}/cgevent" move "${mx}" "${my}" >/dev/null 2>&1 || true
      ;;
    resize)
      local w h
      w="$(field "${record}" w)"; h="$(field "${record}" h)"
      vm_num "${w}" w; vm_num "${h}" h
      log "action resize ${w}x${h}"
      vm_ssh "${REMOTE_TOOLS}/axresize" "${APP_PROCESS}" size "${w}" "${h}" >/dev/null 2>&1 || true
      ;;
    move_window)
      # axresize's OTHER subcommand. Every Atrium window opens at
      # pos=0,30, so two windows overlap exactly and a click at a fixed
      # coordinate lands on whichever is frontmost - which no scenario asserts.
      # Resizing does not separate them: it moves the content card and leaves
      # both sidebars in the same band. Moving one does.
      local wx wy
      wx="$(field "${record}" x)"; wy="$(field "${record}" y)"
      vm_num "${wx}" x; vm_num "${wy}" y
      log "action move_window ${wx},${wy}"
      vm_ssh "${REMOTE_TOOLS}/axresize" "${APP_PROCESS}" pos "${wx}" "${wy}" >/dev/null 2>&1 || true
      ;;
    drag)
      local x1 y1 x2 y2 steps expect tol delivered tries
      x1="$(field "${record}" x1)"; y1="$(field "${record}" y1)"
      x2="$(field "${record}" x2)"; y2="$(field "${record}" y2)"
      # 20ms per step. A sidebar row drag needs the release to come after the
      # native drag session has opened, or that session never ends.
      steps="$(field "${record}" steps)"; [ -n "${steps}" ] || steps=8
      # Same "x,y" eval answers click's from_eval takes, one per end.
      local end_key end_eval end_point
      for end_key in from_eval to_eval; do
        end_eval="$(field "${record}" "${end_key}")"
        [ -n "${end_eval}" ] || continue
        end_point="${EVAL_RESULT[${end_eval}]:-}"
        end_point="${end_point#\"}"; end_point="${end_point%\"}"
        if [[ ! "${end_point}" =~ ^-?[0-9]+,-?[0-9]+$ ]]; then
          record_result FAIL "drag:${end_eval}" "eval ${end_eval} gave no x,y point to drag: '${end_point}'"
          return 0
        fi
        if [ "${end_key}" = from_eval ]; then
          x1="${end_point%,*}"; y1="${end_point#*,}"
        else
          x2="${end_point%,*}"; y2="${end_point#*,}"
        fi
      done
      vm_num "${x1}" x1; vm_num "${y1}" y1; vm_num "${x2}" x2; vm_num "${y2}" y2
      vm_num "${steps}" steps
      log "action drag ${x1},${y1} -> ${x2},${y2} steps=${steps}"
      vm_ssh "${REMOTE_TOOLS}/cgevent" drag "${x1}" "${y1}" "${x2}" "${y2}" "${steps}" >/dev/null 2>&1 || true
      # a synthetic drag lands short by a different amount each run.
      # With expect_width the drag is graded on the width the window printed
      # and corrected up to three times, and the delivered width is logged
      # either way - a short drag that reads like a finding is the cost this
      # removes.
      expect="$(field "${record}" expect_width)"
      if [ -n "${expect}" ]; then
        tol="$(field "${record}" tolerance)"; [ -n "${tol}" ] || tol=12
        tries=0
        while :; do
          sleep 0.6
          delivered="$(_vlog_last 'Atrium: sidebar grip band' | sed -n 's/.*grip band \([0-9]*\),.*/\1/p')"
          [ -n "${delivered}" ] || delivered=0
          log "  drag delivered width ${delivered} (asked ${expect}, try $((tries + 1)))"
          if [ $(( delivered > expect ? delivered - expect : expect - delivered )) -le "${tol}" ]; then
            record_result PASS "drag:width" "delivered ${delivered} for ${expect} within ${tol}"
            break
          fi
          tries=$(( tries + 1 ))
          if [ "${tries}" -ge 3 ]; then
            record_result FAIL "drag:width" "delivered ${delivered} for ${expect} after ${tries} tries"
            break
          fi
          vm_ssh "${REMOTE_TOOLS}/cgevent" drag "${delivered}" "${y2}" "${expect}" "${y2}" >/dev/null 2>&1 || true
        done
      fi
      ;;
    collapse_sidebar)
      # the keyboard chord is the one gesture the harness drops after
      # another action. The chrome row's own toggle is a click on the page, and
      # the window logs both the request and the grip band that moves for it.
      local before after
      before="$(_vlog_count 'Atrium: sidebar toggle requested from the chrome row')"
      log "action collapse_sidebar"
      ensure_cdp_tunnel || log "  (tunnel unavailable)"
      node "${REPO}/build/scripts/cdp-eval.mjs" "${CDP_PORT}" "/sidebar" \
        "(() => { const b = document.querySelector('.sbtoggle'); if (!b) return 'no-button'; b.click(); return 'clicked'; })()" >/dev/null 2>&1 || true
      _toggle_landed() { [ "$(_vlog_count 'Atrium: sidebar toggle requested from the chrome row')" -gt "${before}" ]; }
      if retry 8 0.25 _toggle_landed; then
        after="$(_vlog_last 'Atrium: sidebar grip band' | sed -n 's/.*grip band \([0-9]*\),.*/\1/p')"
        log "  collapse landed, grip band now ${after:-unknown}"
      else
        record_result FAIL "collapse_sidebar" "the chrome row's toggle never logged a request within 8s"
      fi
      ;;
    eval_until)
      # an eval that samples once reads whatever was mid-motion. This
      # one polls until the expression answers `expected` or the timeout runs
      # out, and stores the last answer either way so the failure names it.
      local eid target expr want timeout_ms timeout_s out
      eid="$(field "${record}" id)"
      target="$(field "${record}" target)"; [ -n "${target}" ] || target="/sidebar"
      expr="$(field "${record}" expression)"
      want="$(field "${record}" expected)"
      timeout_ms="$(field "${record}" timeout_ms)"; [ -n "${timeout_ms}" ] || timeout_ms=8000
      timeout_ms=$(scaled_wait "${timeout_ms}")
      timeout_s=$(( (timeout_ms + 999) / 1000 ))
      log "action eval_until id=${eid} target=${target} expected=${want} (timeout ${timeout_ms}ms)"
      ensure_cdp_tunnel || log "  (tunnel unavailable)"
      # cdp-eval prints a string answer JSON-quoted, so a scenario can write
      # expected: "cleared" and match "cleared" as well as \"cleared\".
      _eval_settled() {
        out="$(node "${REPO}/build/scripts/cdp-eval.mjs" "${CDP_PORT}" "${target}" "${expr}" 2>&1 | tail -1 || true)"
        local got="${out#OK }"
        got="${got#\"}"; got="${got%\"}"
        [ "${out}" != "${out#OK }" ] && [ "${got}" = "${want}" ]
      }
      if retry "${timeout_s}" 0.25 _eval_settled; then
        EVAL_RESULT["${eid}"]="${want}"
        log "  eval ${eid} settled -> ${want}"
      else
        local last="${out#OK }"; last="${last#\"}"; last="${last%\"}"
        EVAL_RESULT["${eid}"]="${last}"
        record_result FAIL "eval_until:${eid}" "never settled on ${want} within ${timeout_ms}ms, last answer: ${out}"
      fi
      ;;
    scroll)
      # phase-based trackpad scroll (began/changed/ended, plus a
      # momentum tail when momentum_ms > 0 - a released-finger flick) - see
      # cgevent.swift's own `scroll` doc for why a plain scroll event cannot
      # drive SidebarSwipeCatcher at all.
      local sx sy dx phase_ms momentum_ms step_ms
      sx="$(field "${record}" x)"; sy="$(field "${record}" y)"
      dx="$(field "${record}" dx)"
      phase_ms="$(field "${record}" phase_ms)"; [ -n "${phase_ms}" ] || phase_ms=300
      momentum_ms="$(field "${record}" momentum_ms)"; [ -n "${momentum_ms}" ] || momentum_ms=0
      step_ms="$(field "${record}" step_ms)"; [ -n "${step_ms}" ] || step_ms=20
      vm_num "${sx}" x; vm_num "${sy}" y; vm_num "${dx}" dx
      vm_num "${phase_ms}" phase_ms; vm_num "${momentum_ms}" momentum_ms; vm_num "${step_ms}" step_ms
      log "action scroll ${sx},${sy} dx=${dx} phase_ms=${phase_ms} momentum_ms=${momentum_ms} step_ms=${step_ms}"
      vm_ssh "${REMOTE_TOOLS}/cgevent" scroll "${sx}" "${sy}" "${dx}" "${phase_ms}" "${momentum_ms}" "${step_ms}" >/dev/null 2>&1 || true
      ;;
    vscroll)
      # the same phase-based trackpad gesture on the VERTICAL axis.
      # cgevent hard-coded wheel1 to 0 until now, so nothing vertical could be
      # driven from a scenario at all - the tab list's rubber band was the
      # first thing to want it.
      #
      # A separate cgevent subcommand rather than a fourth positional on
      # `scroll`: every swipe scenario passes `scroll x y dx ...`, and a new
      # positional in the middle would silently reinterpret their arguments.
      local vx vy vdy vphase_ms vmomentum_ms vstep_ms
      vx="$(field "${record}" x)"; vy="$(field "${record}" y)"
      vdy="$(field "${record}" dy)"
      vphase_ms="$(field "${record}" phase_ms)"; [ -n "${vphase_ms}" ] || vphase_ms=300
      vmomentum_ms="$(field "${record}" momentum_ms)"; [ -n "${vmomentum_ms}" ] || vmomentum_ms=0
      vstep_ms="$(field "${record}" step_ms)"; [ -n "${vstep_ms}" ] || vstep_ms=20
      vm_num "${vx}" x; vm_num "${vy}" y; vm_num "${vdy}" dy
      vm_num "${vphase_ms}" phase_ms; vm_num "${vmomentum_ms}" momentum_ms; vm_num "${vstep_ms}" step_ms
      log "action vscroll ${vx},${vy} dy=${vdy} phase_ms=${vphase_ms} momentum_ms=${vmomentum_ms} step_ms=${vstep_ms}"
      vm_ssh "${REMOTE_TOOLS}/cgevent" vscroll "${vx}" "${vy}" "${vdy}" "${vphase_ms}" "${vmomentum_ms}" "${vstep_ms}" >/dev/null 2>&1 || true
      ;;
    vscroll_during_capture)
      # The band springs back in about 0.3s, so a sequential gesture-then-
      # capture pair photographs the list already at rest. Same background-
      # rapidcap technique as scroll_during_capture, vertical axis.
      local wx wy wdy wphase_ms wmomentum_ms wstep_ms wid wdur wfps wvmdir wvmdir_q
      wx="$(field "${record}" x)"; wy="$(field "${record}" y)"
      wdy="$(field "${record}" dy)"
      wphase_ms="$(field "${record}" phase_ms)"; [ -n "${wphase_ms}" ] || wphase_ms=300
      wmomentum_ms="$(field "${record}" momentum_ms)"; [ -n "${wmomentum_ms}" ] || wmomentum_ms=0
      wstep_ms="$(field "${record}" step_ms)"; [ -n "${wstep_ms}" ] || wstep_ms=20
      vm_num "${wx}" x; vm_num "${wy}" y; vm_num "${wdy}" dy
      vm_num "${wphase_ms}" phase_ms; vm_num "${wmomentum_ms}" momentum_ms; vm_num "${wstep_ms}" step_ms
      wid="$(field "${record}" capture_id)"
      wdur="$(field "${record}" duration_ms)"; [ -n "${wdur}" ] || wdur=$(( wphase_ms + wmomentum_ms + 600 ))
      wfps="$(field "${record}" fps)"; [ -n "${wfps}" ] || wfps=25
      vm_num "${wdur}" duration_ms
      vm_num "${wfps}" fps
      wvmdir="/tmp/scen-cap-${wid}"
      wvmdir_q="$(vm_q "${wvmdir}")"
      log "action vscroll_during_capture ${wx},${wy} dy=${wdy} phase_ms=${wphase_ms} capture_id=${wid} ${wdur}ms @${wfps}fps"
      vm_ssh "rm -rf ${wvmdir_q}; mkdir -p ${wvmdir_q}; '${REMOTE_TOOLS}/rapidcap' ${wvmdir_q} ${wdur} ${wfps} >${wvmdir_q}/rapidcap.out 2>${wvmdir_q}/rapidcap.err & RAPID_PID=\$!; '${REMOTE_TOOLS}/cgevent' vscroll ${wx} ${wy} ${wdy} ${wphase_ms} ${wmomentum_ms} ${wstep_ms}; wait \${RAPID_PID}" >/dev/null 2>&1 || true
      local wlocaldir="${RESULTS_DIR}/frames/${wid}"
      mkdir -p "${wlocaldir}"
      vm_scp_from "${wvmdir}/" "${RESULTS_DIR}/frames/" 2>/dev/null || true
      [ -d "${RESULTS_DIR}/frames/scen-cap-${wid}" ] && wlocaldir="${RESULTS_DIR}/frames/scen-cap-${wid}"
      local wlast
      wlast="$(find "${wlocaldir}" -name 'frame-*.png' 2>/dev/null | sort | tail -1)"
      if [ -n "${wlast}" ]; then
        CAPTURE_LAST["${wid}"]="${wlast}"
        log "  captured last frame: $(basename "${wlast}") ($(find "${wlocaldir}" -name 'frame-*.png' 2>/dev/null | wc -l | tr -d ' ') frames total)"
      else
        report_capture_dropout "${wid}" "${wvmdir}"
      fi
      ;;
    scroll_during_capture)
      # `scroll`'s same background-rapidcap technique as
      # key_during_capture above (one ssh round trip, in-VM timing) - a plain
      # sequential scroll-then-capture pair would miss the transition the
      # same way a sequential key-then-capture pair does. Registers ONE
      # capture id (the whole burst, not a settled/mid pair - the swipe
      # tasks' own AC needs a frame SEQUENCE across the transition, not a
      # single before/after pixel compare).
      local sx sy dx phase_ms momentum_ms step_ms id dur fps vmdir vmdir_q id_q
      sx="$(field "${record}" x)"; sy="$(field "${record}" y)"
      dx="$(field "${record}" dx)"
      phase_ms="$(field "${record}" phase_ms)"; [ -n "${phase_ms}" ] || phase_ms=300
      momentum_ms="$(field "${record}" momentum_ms)"; [ -n "${momentum_ms}" ] || momentum_ms=0
      step_ms="$(field "${record}" step_ms)"; [ -n "${step_ms}" ] || step_ms=20
      vm_num "${sx}" x; vm_num "${sy}" y; vm_num "${dx}" dx
      vm_num "${phase_ms}" phase_ms; vm_num "${momentum_ms}" momentum_ms; vm_num "${step_ms}" step_ms
      id="$(field "${record}" capture_id)"
      dur="$(field "${record}" duration_ms)"; [ -n "${dur}" ] || dur=$(( phase_ms + momentum_ms + 600 ))
      fps="$(field "${record}" fps)"; [ -n "${fps}" ] || fps=25
      vm_num "${dur}" duration_ms
      vm_num "${fps}" fps
      vmdir="/tmp/scen-cap-${id}"
      vmdir_q="$(vm_q "${vmdir}")"
      id_q="$(vm_q "${id}")"
      log "action scroll_during_capture ${sx},${sy} dx=${dx} phase_ms=${phase_ms} momentum_ms=${momentum_ms} step_ms=${step_ms} capture_id=${id} ${dur}ms @${fps}fps"
      vm_ssh "rm -rf ${vmdir_q}; '${REMOTE_TOOLS}/rapidcap' ${vmdir_q} ${dur} ${fps} >/tmp/rapidcap-${id_q}.log 2>&1 & RAPID_PID=\$!; '${REMOTE_TOOLS}/cgevent' scroll ${sx} ${sy} ${dx} ${phase_ms} ${momentum_ms} ${step_ms}; wait \${RAPID_PID}" >/dev/null 2>&1 || true
      local localdir="${RESULTS_DIR}/frames/${id}"
      mkdir -p "${localdir}"
      vm_scp_from "${vmdir}/" "${RESULTS_DIR}/frames/" 2>/dev/null || true
      [ -d "${RESULTS_DIR}/frames/scen-cap-${id}" ] && localdir="${RESULTS_DIR}/frames/scen-cap-${id}"
      local last
      last="$(find "${localdir}" -name 'frame-*.png' 2>/dev/null | sort | tail -1)"
      if [ -n "${last}" ]; then
        CAPTURE_LAST["${id}"]="${last}"
        log "  captured last frame: $(basename "${last}") ($(find "${localdir}" -name 'frame-*.png' 2>/dev/null | wc -l | tr -d ' ') frames total)"
      else
        report_capture_dropout "${id}" "${vmdir}"
      fi
      ;;
    clicks)
      # Paced from INSIDE the VM: chained `click` actions each cost an ssh hop,
      # which is longer than the gap being asked for.
      local cxs cy interval
      cxs="$(field "${record}" x)"
      cy="$(field "${record}" y)"
      interval="$(field "${record}" interval_ms)"; [ -n "${interval}" ] || interval=100
      vm_num "${cy}" y
      vm_num "${interval}" interval_ms
      local -a click_xs
      IFS=',' read -ra click_xs <<<"${cxs}"
      local cx
      for cx in "${click_xs[@]}"; do
        vm_num "${cx}" x
      done
      local interval_s; interval_s="$(awk "BEGIN{printf \"%.3f\", ${interval}/1000}")"
      # One remote command string, so the VM side parses nothing of its own.
      # Each press is a `move` and then a `click` - the pair that is needed to
      # reach a surface that has had no pointer over it.
      local burst=""
      for cx in "${click_xs[@]}"; do
        burst="${burst} '${REMOTE_TOOLS}/cgevent' move ${cx} ${cy}; '${REMOTE_TOOLS}/cgevent' click ${cx} ${cy}; sleep ${interval_s};"
      done
      log "action clicks xs=${cxs} y=${cy} every ${interval}ms"
      # No rapidcap around this, unlike key_during_capture. Measured 2026-08-30:
      # a press fired while a capture stream is live moves the pointer and lands
      # its hover, and produces no DOM click at all. Keys are unaffected.
      vm_ssh "${burst} true" >/dev/null 2>&1 || true
      ;;
    drag_path)
      # The button state lives in the OS, so raw events from separate cgevent
      # runs read as one gesture. `steps` is per segment, 20ms each.
      local pts steps
      pts="$(field "${record}" path)"
      steps="$(field "${record}" steps)"; [ -n "${steps}" ] || steps=20
      vm_num "${steps}" steps
      local -a path_xy
      IFS=',' read -ra path_xy <<<"${pts}"
      local v
      for v in "${path_xy[@]}"; do
        vm_num "${v}" path
      done
      local cg="'${REMOTE_TOOLS}/cgevent'"
      local drag="${cg} rawdown ${path_xy[0]} ${path_xy[1]}; sleep 0.02;"
      local i s px py qx qy
      for (( i = 2; i < ${#path_xy[@]}; i += 2 )); do
        px=${path_xy[i-2]}; py=${path_xy[i-1]}; qx=${path_xy[i]}; qy=${path_xy[i+1]}
        for (( s = 1; s <= steps; s++ )); do
          drag="${drag} ${cg} rawdragged $(( px + (qx - px) * s / steps )) $(( py + (qy - py) * s / steps )); sleep 0.02;"
        done
      done
      drag="${drag} ${cg} rawup ${qx} ${qy};"
      log "action drag_path ${pts} steps=${steps}"
      vm_ssh "${drag} true" >/dev/null 2>&1 || true
      ;;
    wait_vlog)
      # Poll the VM's live run log for a specific line instead of a fixed
      # sleep.
      #
      # A timeout is recorded as a FAILURE, not a warning. This used to
      # proceed anyway on the theory that a real problem would surface via
      # the assertions below - it does not. The later actions run against
      # state the gate was there to establish, so the run fails somewhere
      # unrelated and reads as a defect in whatever was being tested
      # (that is how this harness produced a wrong attribution).
      local pattern timeout_ms timeout_s
      pattern="$(field "${record}" pattern)"
      timeout_ms="$(field "${record}" timeout_ms)"; [ -n "${timeout_ms}" ] || timeout_ms=4000
      timeout_ms=$(scaled_wait "${timeout_ms}")
      timeout_s=$(( (timeout_ms + 999) / 1000 ))
      log "action wait_vlog /${pattern}/ (timeout ${timeout_ms}ms)"
      if retry "${timeout_s}" 0.25 _vlog_present "${pattern}"; then
        log "  matched"
      else
        record_result FAIL "wait_vlog" "no line matching /${pattern}/ within ${timeout_ms}ms - the gate never opened, so every action after this one ran against unestablished state"
      fi
      ;;
    relaunch)
      # stop the app and start it again against the SAME files.
      #
      # NOT RESTAGING IS THE WHOLE POINT. bring_up_vm restores the clean
      # snapshot and stage_fixtures re-copies every fixture out of the repo, so
      # chaining two scenarios could never ask whether a write survived - the
      # second one would read the repo's copy, and a build with no writer at all
      # would look identical to a build with one. This calls launch_atrium
      # alone: REMOTE_SPACES / REMOTE_SETTINGS / REMOTE_THEMES / REMOTE_HOTKEYS
      # / REMOTE_ROUTING still point at the files the first launch was handed,
      # whatever they now contain.
      #
      # Cmd+Q rather than a kill, via shutdown_app: a persistence path may only
      # flush on the way out (atrium_core_save() runs in PostMainMessageLoopRun),
      # and SIGKILL would skip exactly the write the scenario is about to look
      # for. shutdown_app already falls back to SIGTERM for a wedged app and
      # says which one it took.
      local segment
      log "action relaunch (launch ${LAUNCH_INDEX} -> $(( LAUNCH_INDEX + 1 )))"
      shutdown_app
      # Move the finished log aside BEFORE the new launch truncates it, and
      # pull it now rather than at teardown - the VM keeps no copy once the
      # scenario is over, and this is the only record of the earlier launch.
      segment="${RESULTS_DIR}/atrium-run-${LAUNCH_INDEX}.log"
      vm_ssh "mv /tmp/atrium-run.log '/tmp/atrium-run-${LAUNCH_INDEX}.log'" \
        >/dev/null 2>&1 || true
      vm_scp_from "/tmp/atrium-run-${LAUNCH_INDEX}.log" "${segment}" 2>/dev/null || true
      if [ -s "${segment}" ]; then
        RUN_LOG_SEGMENTS+=("${segment}")
      else
        # Losing the segment silently would make every earlier vlog assertion
        # fail for a reason no artifact explains.
        record_result FAIL "relaunch" \
          "could not keep launch ${LAUNCH_INDEX}'s run log, so nothing before this relaunch can be asserted on"
      fi
      LAUNCH_INDEX=$(( LAUNCH_INDEX + 1 ))
      local relaunched
      relaunched="$(launch_atrium)"
      if printf '%s' "${relaunched}" | grep -q LAUNCH_FAILED; then
        record_result FAIL "relaunch" \
          "${APP_PROCESS} did not start again as launch ${LAUNCH_INDEX} (see run log)"
        return 0
      fi
      # The app is up again, so a later `quit` assertion or the teardown phase
      # must send Cmd+Q rather than skip it. shutdown_app set this to 1.
      APP_QUIT=0
      record_result INFO "relaunch" \
        "${APP_PROCESS} stopped and started again as launch ${LAUNCH_INDEX}, against the files staged for launch 1 (spaces=${REMOTE_SPACES}${REMOTE_SETTINGS:+ settings=${REMOTE_SETTINGS}}) - nothing was restaged"
      ;;
    crash_renderer)
      # crash ONE named renderer process, and nothing else.
      #
      # Written for the case where the recovery for a dead chrome-layer
      # renderer cannot be proven without first killing that renderer. These
      # surfaces have no address bar, so chrome://crash is not reachable
      # through the UI, and `pkill -x Atrium` takes the whole app.
      #
      # WHICH process is the whole difficulty, and it is settled on the host by
      # lib/pick_renderer.py against a `ps` dump plus the run log's committed
      # routes - two independent signals, so a spare renderer carrying the
      # right switch but hosting nothing is never the one that dies. Read that
      # file for the target names and for why the browser process is not one.
      #
      # The browser is refused three times over: parse.py rejects the name, the
      # picker refuses any pid running the browser binary, and the guard below
      # re-reads the target's OWN command line from the live process before
      # signalling it. The first two grade our own reasoning; only the third
      # asks the machine, which is what the cost of being wrong here deserves.
      local target ps_file commit_file pick_err pid rc reason
      target="$(field "${record}" target)"
      log "action crash_renderer target=${target}"
      ps_file="${RESULTS_DIR}/crash-${target}-ps.txt"
      commit_file="${RESULTS_DIR}/crash-${target}-commits.txt"
      pick_err="${RESULTS_DIR}/crash-${target}-pick.txt"
      # Kept as artifacts: when a pick is later disputed, the inputs it was
      # made from are the only way to answer.
      vm_ssh "ps -axww -o pid=,command=" >"${ps_file}" 2>/dev/null || true
      vm_ssh "grep -a 'Committed provisional load:' /tmp/atrium-run.log || true" \
        >"${commit_file}" 2>/dev/null || true
      # set -e off around the pick: a refusal is a result to report, not a
      # reason to abort the run, and `rc=$?` after an `if` reads the IF's
      # status rather than the command's (see retry() in lib/asserts.sh).
      set +e
      pid="$(python3 "${PICK}" "${target}" "${ps_file}" "${commit_file}" "${APP_PROCESS}" 2>"${pick_err}")"
      rc=$?
      set -e
      reason="$(tr '\n' ' ' <"${pick_err}")"
      case "${rc}" in
        0) log "  picked pid ${pid}: ${reason}" ;;
        3) record_result FAIL "crash_renderer" "REFUSED - ${reason}"; return 0 ;;
        4) record_result FAIL "crash_renderer" "no '${target}' renderer to crash, so every action after this one ran against an app that never lost one - ${reason}"; return 0 ;;
        *) record_result FAIL "crash_renderer" "the picker itself failed (rc=${rc}) - ${reason}"; return 0 ;;
      esac

      local live
      live="$(vm_ssh "ps -p ${pid} -o command= || true" 2>/dev/null | tr -d '\r')"
      case "${live}" in
        *--type=renderer*) ;;
        *)
          record_result FAIL "crash_renderer" \
            "refusing to signal pid ${pid}: its live command line is not a renderer -> ${live:-<process gone>}"
          return 0 ;;
      esac

      local before_pids after_pids browser_after gone=0 deadline vanished
      before_pids="$(_renderer_pids)"
      # SIGSEGV rather than SIGKILL: a kill reaches the browser as
      # TERMINATION_STATUS_PROCESS_WAS_KILLED, and the thing under test is a
      # crash. It also makes the renderer print its own fatal-signal report,
      # which is what a scenario can then assert on.
      vm_ssh "kill -SEGV ${pid}" >/dev/null 2>&1 || true
      deadline=$(( SECONDS + 10 ))
      while [ "${SECONDS}" -le "${deadline}" ]; do
        if [ -z "$(vm_ssh "ps -p ${pid} -o pid= || true" 2>/dev/null | tr -d ' \r')" ]; then
          gone=1; break
        fi
        sleep 1
      done
      after_pids="$(_renderer_pids)"
      browser_after="$(vm_ssh pgrep -x "${APP_PROCESS}" 2>/dev/null || true)"
      # The browser's own notice of THIS pid dying. A scenario cannot assert it
      # - the pid is not knowable when the spec is written, and a pattern loose
      # enough to match any pid also matches the utility processes that come and
      # go during startup. So it is recorded here, where the pid is known.
      local child_notice
      child_notice="$(vm_ssh "grep -aE 'Task died, PID=${pid}[^0-9]' /tmp/atrium-run.log | tail -1 || true" 2>/dev/null | tr -d '\r')"
      vanished="$(python3 -c 'import sys; print(" ".join(sorted(set(sys.argv[1].split()) - set(sys.argv[2].split()))))' \
        "${before_pids}" "${after_pids}")"
      if [ "${gone}" != "1" ]; then
        record_result FAIL "crash_renderer" \
          "SIGSEGV to pid ${pid} (${target}) left it running after 10s - nothing crashed"
      elif [ -z "${browser_after}" ]; then
        record_result FAIL "crash_renderer" \
          "the ${APP_PROCESS} browser process is GONE after crashing pid ${pid} - the crash was not contained to the renderer"
      elif [ "${vanished}" != "${pid}" ]; then
        record_result FAIL "crash_renderer" \
          "meant to crash pid ${pid} (${target}) alone, but the renderers that went away were: ${vanished:-<none>} (before [${before_pids}] after [${after_pids}])"
      else
        record_result INFO "crash_renderer" \
          "SIGSEGV -> ${target} renderer pid ${pid} gone, and only that one (before [${before_pids}] after [${after_pids}]); ${APP_PROCESS} browser pid ${browser_after//$'\n'/,} still up; browser noticed: ${child_notice:-<no \"Task died, PID=${pid}\" line>}; pick: ${reason}"
      fi
      ;;
    *)
      die "unknown action type '${type}'"
      ;;
  esac
}

# --- assertion dispatch -----------------------------------------------------
do_assert() {
  local record="$1"
  local type="${record%%$'\t'*}"
  case "${type}" in
    ax_window_count)
      assert_ax_window_count "$(field "${record}" expected)" "$(field "${record}" min_area)" || true
      ;;
    ax_window_kind)
      assert_ax_window_kind "$(field "${record}" pattern)" "$(field "${record}" expected)" \
        "$(field "${record}" min_area)" || true
      ;;
    ax_window_subrole)
      assert_ax_window_subrole "$(field "${record}" title)" "$(field "${record}" expected)" || true
      ;;
    eval_equals)
      assert_eval_equals "$(field "${record}" id)" "$(field "${record}" expected)" || true
      ;;
    eval_changed)
      assert_eval_changed "$(field "${record}" before)" "$(field "${record}" after)" || true
      ;;
    theme_changed)
      local before after
      before="${CAPTURE_LAST[$(field "${record}" before)]:-}"
      after="${CAPTURE_LAST[$(field "${record}" after)]:-}"
      assert_theme_changed "${before}" "${after}" "$(field "${record}" region)" "$(field "${record}" min_delta)" || true
      ;;
    pixels_changed)
      local before after
      before="${CAPTURE_LAST[$(field "${record}" before)]:-}"
      after="${CAPTURE_LAST[$(field "${record}" after)]:-}"
      assert_pixels_changed "${before}" "${after}" "$(field "${record}" region)" \
        "$(field "${record}" min_changed_pct)" "$(field "${record}" tol)" || true
      ;;
    region_stable)
      local before after
      before="${CAPTURE_LAST[$(field "${record}" before)]:-}"
      after="${CAPTURE_LAST[$(field "${record}" after)]:-}"
      assert_region_stable "${before}" "${after}" "$(field "${record}" region)" "$(field "${record}" max_delta)" || true
      ;;
    resize_applied)
      {
        local w h tol got gw gh dw dh
        w="$(field "${record}" w)"; h="$(field "${record}" h)"
        tol="$(field "${record}" tolerance)"; tol="${tol:-40}"
        vm_ssh "${REMOTE_TOOLS}/axresize" "${APP_PROCESS}" size "${w}" "${h}" >/dev/null 2>&1 || true
        sleep 1  # let the AX frame change settle before reading it back
        got="$(_ax_run get "")"
        # axresize get prints: pos X Y size W H
        gw="$(printf '%s' "${got}" | sed -n 's/.*size \([0-9][0-9]*\) [0-9][0-9]*/\1/p' | tail -1)"
        gh="$(printf '%s' "${got}" | sed -n 's/.*size [0-9][0-9]* \([0-9][0-9]*\)/\1/p' | tail -1)"
        # "could not read the size back" is the harness failing to
        # measure, not a claim that the resize did not apply.
        if [ -z "${gw}" ] || [ -z "${gh}" ]; then
          record_result HARNESS "resize_applied" \
            "axresize did not answer (rc=${AX_TOOL_RC}) - nothing was measured about the window. got='${got}' stderr: ${AX_TOOL_ERR:-<empty>}"
        else
          dw=$(( gw - w )); dw=${dw#-}
          dh=$(( gh - h )); dh=${dh#-}
          if [ "${dw}" -le "${tol}" ] && [ "${dh}" -le "${tol}" ]; then
            record_result PASS "resize_applied" "target=${w}x${h} got=${gw}x${gh} tol=${tol} (dw=${dw} dh=${dh})"
          else
            record_result FAIL "resize_applied" "target=${w}x${h} got=${gw}x${gh} tol=${tol} (dw=${dw} dh=${dh}) - resize did NOT apply"
          fi
        fi
      } || true
      ;;
    window_moved)
      assert_window_moved "$(field "${record}" x)" "$(field "${record}" y)" \
        "$(field "${record}" tolerance)" || true
      ;;
    quit)
      assert_quit || true
      ;;
    vlog_absent)
      assert_vlog_absent "$(field "${record}" pattern)" "$(field "${record}" label)" || true
      ;;
    vlog)
      assert_vlog "$(field "${record}" pattern)" "$(field "${record}" label)" || true
      ;;
    region_mean)
      assert_region_mean "$(field "${record}" capture)" "$(field "${record}" x)" \
        "$(field "${record}" y)" "$(field "${record}" w)" "$(field "${record}" h)" \
        "$(field "${record}" max_mean)" "$(field "${record}" min_mean)" \
        "$(field "${record}" label)" "$(field "${record}" min_r)" \
        "$(field "${record}" max_g)" "$(field "${record}" max_b)" \
        "$(field "${record}" max_r)" || true
      ;;
    region_every_frame)
      assert_region_every_frame "$(field "${record}" capture)" "$(field "${record}" x)" \
        "$(field "${record}" y)" "$(field "${record}" w)" "$(field "${record}" h)" \
        "$(field "${record}" max_mean)" "$(field "${record}" min_mean)" \
        "$(field "${record}" label)" || true
      ;;
    band_step)
      assert_band_step "$(field "${record}" capture)" "$(field "${record}" x)" \
        "$(field "${record}" y)" "$(field "${record}" len)" \
        "$(field "${record}" thick)" "$(field "${record}" dir)" \
        "$(field "${record}" expected)" "$(field "${record}" tol)" \
        "$(field "${record}" label)" "$(field "${record}" colour_tol)" || true
      ;;
    variance_collapsed)
      assert_variance_collapsed "$(field "${record}" before)" "$(field "${record}" after)" \
        "$(field "${record}" x)" "$(field "${record}" y)" "$(field "${record}" w)" \
        "$(field "${record}" h)" "$(field "${record}" max_ratio)" \
        "$(field "${record}" label)" || true
      ;;
    regions_track)
      # Every frame, not the last: a region that lags a crossfade has caught
      # up by the settled frame. min_swing refuses a burst that missed the fade.
      {
        local id ax ay aw ah bx by bw bh max_delta min_swing label last
        id="$(field "${record}" capture)"
        ax="$(field "${record}" ax)"; ay="$(field "${record}" ay)"
        aw="$(field "${record}" aw)"; ah="$(field "${record}" ah)"
        bx="$(field "${record}" bx)"; by="$(field "${record}" by)"
        bw="$(field "${record}" bw)"; bh="$(field "${record}" bh)"
        max_delta="$(field "${record}" max_delta)"; min_swing="$(field "${record}" min_swing)"
        label="$(field "${record}" label)"; label="${label:-regions_track}"
        last="${CAPTURE_LAST[${id}]:-}"
        if [ -z "${last}" ] || [ ! -f "${last}" ]; then
          record_result FAIL "regions_track:${label}" "capture '${id}' has no frame to read"
        else
          local dir png out a b frames=0 worst=0 worst_frame="" a_lo=999 a_hi=-1 loop_failed=0
          dir="$(dirname "${last}")"
          while IFS= read -r png; do
            out="$("${HOST_PIXCMP}" stats "${png}" "${ax}" "${ay}" "${aw}" "${ah}" 2>/dev/null)" || {
              record_result FAIL "regions_track:${label}" "pixcmp stats failed on ${png}"
              loop_failed=1
              break
            }
            a="$(echo "${out}" | awk -F'[=,]' '/^mean=/{printf "%.1f", ($2+$3+$4)/3}')"
            out="$("${HOST_PIXCMP}" stats "${png}" "${bx}" "${by}" "${bw}" "${bh}" 2>/dev/null)" || {
              record_result FAIL "regions_track:${label}" "pixcmp stats failed on ${png}"
              loop_failed=1
              break
            }
            b="$(echo "${out}" | awk -F'[=,]' '/^mean=/{printf "%.1f", ($2+$3+$4)/3}')"
            frames=$((frames + 1))
            if awk "BEGIN{exit !(${a} < ${a_lo})}"; then a_lo="${a}"; fi
            if awk "BEGIN{exit !(${a} > ${a_hi})}"; then a_hi="${a}"; fi
            local delta
            delta="$(awk "BEGIN{d=${a}-${b}; if (d<0) d=-d; printf \"%.1f\", d}")"
            if awk "BEGIN{exit !(${delta} > ${worst})}"; then
              worst="${delta}"; worst_frame="$(basename "${png}") a=${a} b=${b}"
            fi
          done < <(find "${dir}" -name 'frame-*.png' 2>/dev/null | sort)
          if [ "${loop_failed}" -eq 0 ]; then
            if [ "${frames}" -eq 0 ]; then
              record_result FAIL "regions_track:${label}" "capture '${id}' has no frames under ${dir}"
            else
              local swing
              swing="$(awk "BEGIN{printf \"%.1f\", ${a_hi}-${a_lo}}")"
              if ! awk "BEGIN{exit !(${swing} >= ${min_swing})}"; then
                record_result FAIL "regions_track:${label}" \
                  "region A only moved ${swing} over ${frames} frames (min_swing ${min_swing}) - the burst missed the fade"
              elif ! awk "BEGIN{exit !(${worst} <= ${max_delta})}"; then
                record_result FAIL "regions_track:${label}" \
                  "regions drift apart by ${worst} at ${worst_frame} (max_delta ${max_delta}) over ${frames} frames, A swung ${swing}"
              else
                record_result PASS "regions_track:${label}" \
                  "regions stay within ${worst} of each other over ${frames} frames (max_delta ${max_delta}), A swung ${swing}"
              fi
            fi
          fi
        fi
      } || true
      ;;
    region_steady)
      # A flip between two pictures reads as one big jump between two frames,
      # while a slow drift moves the mean a little each frame.
      {
        local id rx ry rw rh max_step label last dir png out m prev="" frames=0 worst=0 worst_frame=""
        id="$(field "${record}" capture)"
        IFS=',' read -r rx ry rw rh <<<"$(field "${record}" region)"
        max_step="$(field "${record}" max_step)"
        label="$(field "${record}" label)"; label="${label:-region_steady}"
        last="${CAPTURE_LAST[${id}]:-}"
        if [ -z "${last}" ] || [ ! -f "${last}" ]; then
          record_result FAIL "region_steady:${label}" "capture '${id}' has no frame to read"
        else
          dir="$(dirname "${last}")"
          while IFS= read -r png; do
            out="$("${HOST_PIXCMP}" stats "${png}" "${rx}" "${ry}" "${rw}" "${rh}" 2>/dev/null)" || {
              frames=-1
              break
            }
            m="$(echo "${out}" | awk -F'[=,]' '/^mean=/{printf "%.1f", ($2+$3+$4)/3}')"
            frames=$((frames + 1))
            if [ -n "${prev}" ]; then
              local step
              step="$(awk "BEGIN{d=${m}-${prev}; if (d<0) d=-d; printf \"%.1f\", d}")"
              if awk "BEGIN{exit !(${step} > ${worst})}"; then
                worst="${step}"; worst_frame="$(basename "${png}") ${prev} -> ${m}"
              fi
            fi
            prev="${m}"
          done < <(find "${dir}" -name 'frame-*.png' 2>/dev/null | sort)
          if [ "${frames}" -lt 2 ]; then
            record_result FAIL "region_steady:${label}" "capture '${id}' gave ${frames} readable frames under ${dir}"
          elif awk "BEGIN{exit !(${worst} <= ${max_step})}"; then
            record_result PASS "region_steady:${label}" \
              "largest step ${worst} over ${frames} frames (max_step ${max_step})"
          else
            record_result FAIL "region_steady:${label}" \
              "step ${worst} at ${worst_frame} (max_step ${max_step}) over ${frames} frames"
          fi
        fi
      } || true
      ;;
    vlog_count)
      assert_vlog_count "$(field "${record}" pattern)" "$(field "${record}" max)" \
        "$(field "${record}" min)" "$(field "${record}" label)" || true
      ;;
    vlog_after)
      assert_vlog_after "$(field "${record}" pattern)" \
        "$(field "${record}" after_pattern)" "$(field "${record}" label)" || true
      ;;
    recency_walk_distinct)
      assert_recency_walk_distinct "$(field "${record}" count)" "$(field "${record}" pattern)" \
        "$(field "${record}" after_pattern)" "$(field "${record}" label)" || true
      ;;
    file_contains)
      # the only assertion that reads a file. Which file is a NAME,
      # resolved here to both the path inside the VM and the host's copy of what
      # was staged there - a raw remote path would have no baseline, and without
      # the baseline the assertion cannot tell a write from the fixture it
      # started as. parse.py already refused a name this scenario stages none of.
      local which remote staged
      which="$(field "${record}" file)"
      case "${which}" in
        spaces)   remote="${REMOTE_SPACES:-}";          staged="${RESULTS_DIR}/spaces.json" ;;
        settings) remote="${REMOTE_SETTINGS:-}";        staged="${RESULTS_DIR}/settings.json" ;;
        themes)   remote="${REMOTE_THEMES:-}";          staged="${RESULTS_DIR}/themes.json" ;;
        hotkeys)  remote="${REMOTE_HOTKEYS:-}";         staged="${RESULTS_DIR}/hotkeys.json" ;;
        routing)  remote="${REMOTE_ROUTING:-}";         staged="${RESULTS_DIR}/routing.json" ;;
        *)        remote=""; staged="" ;;
      esac
      assert_file_contains "${which}" "${remote}" "${staged}" \
        "$(field "${record}" path)" "$(field "${record}" value)" \
        "$(field "${record}" label)" || true
      ;;
    *)
      die "unknown assertion type '${type}'"
      ;;
  esac
}

# --- one scenario -----------------------------------------------------------
run_scenario() {
  local name="$1"
  local spec="${SCEN_DIR}/${name}.json"
  [ -f "${spec}" ] || die "scenario not found: ${spec}"
  python3 "${PARSE}" validate "${spec}" >/dev/null || die "scenario ${name} failed validation"

  local ts; ts="$(date +%Y%m%d-%H%M%S)"
  RESULTS_DIR="${RESULTS_ROOT}/${name}-${ts}"
  mkdir -p "${RESULTS_DIR}/frames"
  # APP_QUIT resets with the counters: `all` runs every scenario in ONE
  # process, so a leftover 1 from the previous scenario's quit would tell this
  # one's shutdown step the app was already gone.
  PASS_COUNT=0; FAIL_COUNT=0; HARNESS_ERRORS=0; APP_QUIT=0; CAPTURE_LAST=()
  # Same reason as APP_QUIT: `all` runs every scenario in ONE process, so a
  # previous scenario's relaunch would otherwise leave this one assembling its
  # run log out of another scenario's segments.
  RUN_LOG_SEGMENTS=(); LAUNCH_INDEX=1
  log "=== scenario ${name} -> ${RESULTS_DIR} ==="

  local fixture themes routing seed_prefs startup_space hotkeys settings full_kb_access
  local system_appearance spaces_absent import_arc_dir import_zen_dir import_chrome_dir import_firefox_dir load_extension_dir
  fixture="$(python3 "${PARSE}" meta "${spec}" fixture)"
  themes="$(python3 "${PARSE}" meta "${spec}" themes)"
  routing="$(python3 "${PARSE}" meta "${spec}" routing)"
  seed_prefs="$(python3 "${PARSE}" meta "${spec}" seed_prefs)"
  hotkeys="$(python3 "${PARSE}" meta "${spec}" hotkeys)"
  settings="$(python3 "${PARSE}" meta "${spec}" settings)"
  spaces_absent="$(python3 "${PARSE}" meta "${spec}" spaces_absent)"
  import_arc_dir="$(python3 "${PARSE}" meta "${spec}" import_arc_dir)"
  import_zen_dir="$(python3 "${PARSE}" meta "${spec}" import_zen_dir)"
  import_chrome_dir="$(python3 "${PARSE}" meta "${spec}" import_chrome_dir)"
  import_firefox_dir="$(python3 "${PARSE}" meta "${spec}" import_firefox_dir)"
  load_extension_dir="$(python3 "${PARSE}" meta "${spec}" load_extension_dir)"
  startup_space="$(python3 "${PARSE}" meta "${spec}" startup_space)"
  APP_PROCESS="$(python3 "${PARSE}" meta "${spec}" app_process)"
  full_kb_access="$(python3 "${PARSE}" meta "${spec}" full_keyboard_access)"
  system_appearance="$(python3 "${PARSE}" meta "${spec}" system_appearance)"
  # extra Chromium flags. ATRIUM_EXTRA_FLAGS is APPENDED after the
  # scenario's own, so an ad-hoc run can override a repeated flag (last one
  # wins in Chromium) without editing the spec.
  SERVE_FIXTURES="$(python3 "${PARSE}" meta "${spec}" serve_fixtures)"
  # An optional reason this scenario cannot pass on the current build.
  CANNOT_PASS="$(python3 "${PARSE}" meta "${spec}" cannot_pass)"
  [ -n "${CANNOT_PASS}" ] && log "marked cannot_pass: ${CANNOT_PASS}"
  CHROMIUM_FLAGS="$(python3 "${PARSE}" meta "${spec}" chromium_flags)"
  # Read from the spec rather than guessed: only a scenario that evaluates
  # anything pays for the port and the tunnel.
  # eval_until and collapse_sidebar drive the page over CDP too.
  if grep -qE '"type"[[:space:]]*:[[:space:]]*"(eval|eval_until|collapse_sidebar|cdp_wheel|cdp_move)"' "${spec}" 2>/dev/null; then
    SPEC_USES_EVAL=1
    CDP_TUNNEL_WANTED=1
  else
    SPEC_USES_EVAL=0
    CDP_TUNNEL_WANTED=0
  fi
  [ -n "${ATRIUM_EXTRA_FLAGS:-}" ] && CHROMIUM_FLAGS="${CHROMIUM_FLAGS} ${ATRIUM_EXTRA_FLAGS}"
  [ -n "${CHROMIUM_FLAGS}" ] && log "extra Chromium flags: ${CHROMIUM_FLAGS}"

  # record that this scenario STARTED before doing anything that
  # can be killed out from under us - a process that dies to SIGKILL runs no
  # cleanup code, so this line is the only trace a suite-progress check can
  # find afterwards. Written to disk now, not batched for later.
  if [ -n "${SUITE_PROGRESS_FILE:-}" ]; then
    printf '%s\n' "${name}" >>"${SUITE_PROGRESS_FILE}"
  fi

  # `bring_up_vm`'s deploy step now returns 1 (rather than relying on
  # ambient `set -e`, which `run_scenario "${s}" || rc=1` in `all` disables for
  # this whole call tree) on a real deploy failure. Checked explicitly here so
  # that failure is RAISED - counted and reported - instead of silently
  # swallowed and run against a stale or missing build.
  if ! bring_up_vm; then
    record_result HARNESS "bring_up_vm" "VM bring-up/deploy failed for ${name} - see log above; scenario not measured"
    log "scenario ${name}: harness setup failed (bring_up_vm) - not measured"
    # No EXIT trap yet - `trap teardown EXIT` is armed only once bring_up_vm
    # succeeds, so pull whatever artifacts exist directly.
    teardown
    return 1
  fi
  # Refuse to start against a VM that already has an Atrium alive - the
  # rest of this function stages fixtures and launches for a run that
  # ProcessSingleton would hand off to that leftover process instead of
  # using.
  require_no_existing_instance || die "Atrium is already running in the VM"
  # Ensure the app is stopped and artifacts pulled no matter how we exit.
  # EXIT, not RETURN: RETURN only fires on a normal `return` from this
  # function, so `die` (which calls `exit`) and any ungoverned command
  # failure under `set -e` both skipped teardown, leaving a live Atrium in
  # the VM to poison every scenario `all` ran after this one.
  trap teardown EXIT
  stage_tools
  warm_up_screen_capture_alert
  dismiss_window_picker_consent
  stage_fixtures "${fixture}" "${themes}" "${startup_space}" "${routing}" "${seed_prefs}" "${hotkeys}" "${settings}" "${spaces_absent}" "${import_arc_dir}" "${import_zen_dir}" "${import_chrome_dir}" "${import_firefox_dir}"
  stage_extension "${load_extension_dir}"
  apply_full_keyboard_access "${full_kb_access}"
  apply_system_appearance "${system_appearance}"
  start_fixture_server
  start_vm_sampler
  local launch; launch="$(launch_atrium)"
  pull_run_log
  # a launch that never produced a running process (pgrep found
  # nothing a couple of seconds after the launch command returned - binary
  # missing, or a deploy that never landed one) fails as a HARNESS error
  # HERE, before any action or assertion runs against a VM with no Atrium in
  # it. Observed 2026-08-10: without this, the run carried on and reported
  # untouched fixture values as product failures - a scoreboard that looked
  # exactly like a run that measured a broken build.
  if printf '%s' "${launch}" | grep -q LAUNCH_FAILED; then
    grade_launch_dead "launch"
    log "scenario ${name}: harness error (dead launch) - not measured"
    trap - EXIT
    teardown
    return 1
  fi
  # A handoff through ProcessSingleton (the leftover process from an
  # earlier aborted run answering instead of a fresh Atrium starting) fails
  # the run now, before any action or assertion grades that old process
  #.
  grade_launch_singleton "launch:singleton" || die "launch handed off through ProcessSingleton - see the evidence above"

  require_no_window_picker_consent

  local rec
  while IFS= read -r rec; do
    [ -n "${rec}" ] && do_action "${rec}"
  done < <(python3 "${PARSE}" actions "${spec}")

  # Pull the run log now so VLOG-citing asserts see it.
  pull_run_log
  while IFS= read -r rec; do
    [ -n "${rec}" ] && do_assert "${rec}"
  done < <(python3 "${PARSE}" asserts "${spec}")

  # the teardown phase - the only place an assertion can see what the
  # app wrote on its way out. Collected into a variable rather than streamed,
  # because the app must not be stopped at all unless the scenario asked for
  # this, and that is a question about the whole list.
  local teardown_recs
  teardown_recs="$(python3 "${PARSE}" teardown_asserts "${spec}")"
  if [ -n "${teardown_recs}" ]; then
    log "--- teardown assertions: stopping the app, then re-reading the log ---"
    shutdown_app
    pull_run_log
    while IFS= read -r rec; do
      [ -n "${rec}" ] && do_assert "${rec}"
    done <<< "${teardown_recs}"
  fi

  local harness_note=""
  [ "${HARNESS_ERRORS}" -gt 0 ] && harness_note=", ${HARNESS_ERRORS} harness error(s)"
  log "scenario ${name}: ${PASS_COUNT} passed, ${FAIL_COUNT} failed${harness_note}  (artifacts: ${RESULTS_DIR})"
  # say out loud that a red line was expected, so a reader does not
  # have to open the file - or worse, read it as a regression. Printed after the
  # numbers rather than instead of them: the scenario still runs and still
  # counts, which is how anyone notices the day it starts passing again.
  if [ -n "${CANNOT_PASS}" ]; then
    if [ "${FAIL_COUNT}" -gt 0 ]; then
      log "  EXPECTED FAILURE - ${CANNOT_PASS}"
    else
      log "  NOTE: this scenario is marked cannot_pass but PASSED. The reason no longer holds - delete the field. Reason on file: ${CANNOT_PASS}"
    fi
  fi
  # Clear the EXIT trap before calling teardown directly on the normal path -
  # otherwise the function returning would fire it a second time.
  trap - EXIT
  teardown
  [ "${FAIL_COUNT}" -eq 0 ] && [ "${HARNESS_ERRORS}" -eq 0 ]
}

# Lists runnable scenarios for `--list` and `all`. Scenarios with `"wip": true`
# are skipped here (kept out of the default suite) but stay runnable by an
# explicit name - e.g. one blocked on a known VM-input limitation.
list_scenarios() {
  local f name
  for f in "${SCEN_DIR}"/*.json; do
    [ -f "${f}" ] || continue
    [ "$(python3 "${PARSE}" meta "${f}" wip)" = "True" ] && continue
    basename "${f}" .json
  done | sort
}

main() {
  case "${1:-}" in
    ""|-h|--help)
      grep -E '^#( |$)' "$0" | sed -E 's/^# ?//'; exit 0 ;;
    --list) list_scenarios; exit 0 ;;
    --validate)
      [ -n "${2:-}" ] || die "--validate needs a scenario name"
      python3 "${PARSE}" validate "${SCEN_DIR}/${2}.json"; exit $? ;;
    all)
      local rc=0 s
      shift
      # Named scenarios run through the SAME in-process loop as the full
      # suite. Running them from a shell loop instead starts a fresh process
      # each time, which is a different thing - a
      # scenario once failed twice as often under `all` as it did on its own.
      # NOT `IFS=' ' read -ra all_names <<<"$(list_scenarios)"`: `read`
      # (unlike `mapfile`) stops at the FIRST newline regardless of IFS, and
      # list_scenarios emits one name PER LINE - so that form silently
      # collapsed the whole suite to its first entry (the incident this
      # comment documents: a 57-scenario run reported "named 1, ran 1"). The
      # two branches below agree with each producer's own format by
      # construction: `$@` is already space-split by the shell, and
      # `list_scenarios`'s one-per-line output is read with `mapfile`, which
      # reads every line, not just the first.
      local -a all_names=()
      if [ "$#" -gt 0 ]; then
        all_names=("$@")
      else
        mapfile -t all_names < <(list_scenarios)
        # Belt-and-suspenders for the truncation this whole block's comment
        # is about: count the eligible scenario files on disk INDEPENDENTLY
        # of list_scenarios (a plain glob, no `read`/IFS involved) and refuse
        # to proceed if what got parsed is fewer. This is not redundant with
        # the mapfile fix above - it is the check that would have failed
        # loudly even if the truncation had crept in somewhere else entirely.
        local disk_count=0 f
        for f in "${SCEN_DIR}"/*.json; do
          [ -f "${f}" ] || continue
          [ "$(python3 "${PARSE}" meta "${f}" wip)" = "True" ] && continue
          disk_count=$(( disk_count + 1 ))
        done
        if ! python3 "${LIB_DIR}/scenario_names.py" check "${#all_names[@]}" "${disk_count}"; then
          die "scenario list looks truncated - see message above"
        fi
      fi
      local named_count="${#all_names[@]}"

      # validate every named scenario BEFORE running any of
      # them, and report every invalid one TOGETHER - a batch used to stop
      # dead at the first invalid file and leave the scenarios behind it
      # unreported.
      local batch_dir; batch_dir="${RESULTS_ROOT}/.batch-$$"
      mkdir -p "${batch_dir}"
      local names_file="${batch_dir}/names" errors_file="${batch_dir}/errors"
      printf '%s\n' "${all_names[@]}" >"${names_file}"
      : >"${errors_file}"
      local verr
      for s in "${all_names[@]}"; do
        if ! verr="$(python3 "${PARSE}" validate "${SCEN_DIR}/${s}.json" 2>&1 >/dev/null)"; then
          printf '%s\t%s\n' "${s}" "${verr//$'\n'/ ; }" >>"${errors_file}"
        fi
      done
      if [ -s "${errors_file}" ]; then
        log "invalid scenario(s) - skipped, not counted as run:"
        while IFS= read -r line; do log "  ${line}"; done < <(
          python3 "${LIB_DIR}/batch_validation.py" invalid-report "${names_file}" "${errors_file}")
      fi
      local -a valid_names=()
      while IFS= read -r s; do
        [ -n "${s}" ] && valid_names+=("${s}")
      done < <(python3 "${LIB_DIR}/batch_validation.py" valid "${names_file}" "${errors_file}")
      local skipped_count=$(( named_count - ${#valid_names[@]} ))

      # a progress file, appended to the instant each scenario
      # STARTS, so a run that gets SIGKILL'd mid-scenario still leaves
      # evidence of how far it got - a killed process runs no cleanup code
      # and cannot print an end-of-loop summary itself.
      SUITE_PROGRESS_FILE="${batch_dir}/started"
      : >"${SUITE_PROGRESS_FILE}"
      local eligible_file="${batch_dir}/eligible"
      printf '%s\n' "${valid_names[@]}" >"${eligible_file}"

      local ran_count=0
      for s in "${valid_names[@]}"; do
        # Locked and released per scenario, not once for the whole batch - a
        # multi-hour `all` would otherwise starve any other lane wanting this
        # VM for scenarios it has no reason to wait on.
        ( trap 'vm_lock_release "${VM}"' EXIT
          vm_lock_acquire "${VM}"
          run_scenario "${s}" ) || rc=1
        ran_count=$(( ran_count + 1 ))
      done

      if [ "${#valid_names[@]}" -gt 0 ]; then
        python3 "${LIB_DIR}/suite_progress.py" verdict "${eligible_file}" "${SUITE_PROGRESS_FILE}" || rc=1
      elif [ "${named_count}" -gt 0 ]; then
        # Every named scenario was invalid - nothing was measured at all.
        rc=1
      fi

      log "batch summary: $(python3 "${LIB_DIR}/batch_validation.py" summary \
        "${named_count}" "${ran_count}" "${skipped_count}")"
      # Cleaned up only on a clean run. On failure - including a kill this
      # process never got to report - the eligible/started files stay on
      # disk as evidence: a leftover
      # build/vm-fixtures/results/.batch-<pid>/ directory found later is
      # itself proof a batch did not finish.
      if [ "${rc}" -eq 0 ]; then
        rm -rf "${batch_dir}"
      else
        log "batch did not complete cleanly - progress evidence kept at ${batch_dir}"
      fi
      exit "${rc}" ;;
    *)
      ( trap 'vm_lock_release "${VM}"' EXIT
        vm_lock_acquire "${VM}"
        run_scenario "$1" )
      exit $? ;;
  esac
}

# Sourced (not executed) by vm-scenario-quoting-test.sh to reuse the real
# do_action/vm_q/vm_num without running a scenario.
if [ "${BASH_SOURCE[0]}" = "${0}" ]; then
  main "$@"
fi
