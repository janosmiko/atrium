#!/usr/bin/env bash
# build/scenarios/lib/asserts.sh - assertion helpers for the VM scenario
# runner. Sourced by build/scripts/vm-scenario.sh; not executable alone.
#
# This is the piece the ad-hoc probing never had: repeatable asserts with
# evidence. Each assert_* returns 0 (PASS) / 1 (FAIL) and appends an evidence
# line via record_result. The runner owns PASS/FAIL tallies and teardown.
#
# Contract - the runner must define before sourcing:
#   vm_ssh <cmd...>        run a command inside the probe VM, stdout returned
#   APP_PROCESS            app process name (e.g. Atrium)
#   REMOTE_TOOLS           dir inside the VM holding compiled probe binaries
#   RESULTS_DIR            host dir for artifacts (frames, AX dumps, logs)
#   RUN_LOG_LOCAL          host path of the pulled Atrium run log (VLOG source)
#   HOST_PIXCMP            host path of the compiled pixcmp binary
#   FILE_VALUE             host path of lib/file_value.py (grades file_contains)
#   MOVED_OUTCOME          host path of lib/moved_outcome.py (grades pixels_changed)
#   QUIT_OUTCOME           host path of lib/quit_outcome.py (grades a shutdown)
#   LAUNCH_INDEX           which launch is running (names the shutdown artifact)
#   APP_QUIT               0/1 - has the app already been stopped
# and the counters PASS_COUNT / FAIL_COUNT plus record_result().
#
# LAUNCH_OUTCOME (lib/launch_outcome.py, grades a launch's ProcessSingleton
# handoff) is NOT part of the above contract: this file resolves
# its own path from BASH_SOURCE, since the guard it backs must work whether or
# not the caller has been updated to call it.
#
# Fidelity rules baked in (docs/BUILD.md "VM probe fidelity rules"):
#   - never outcome-only: every assert records the raw tool output as evidence
#   - pixel claims use raw decoded pixels (pixcmp), never PNG file hashes
#   - a byte-identical before/after frame is treated as a FAKE, not a pass
#   - GUI timing is absorbed with settle/retry, not a fixed sleep-and-hope

# retry <timeout_s> <interval_s> <cmd...> : run cmd until it exits 0 or the
# timeout elapses. Returns cmd's last exit code. Absorbs GUI settle jitter.
retry() {
  local timeout_s="$1" interval_s="$2"
  shift 2
  local deadline=$(( SECONDS + timeout_s )) rc=1
  while [ "$SECONDS" -le "$deadline" ]; do
    # Capture the failure code in the else branch. `rc=$?` AFTER a bare
    # `if "$@"; then return 0; fi` reads the IF's status, which POSIX defines
    # as 0 when the condition is false and there is no else - so retry() used
    # to report success even when the predicate never once passed, making
    # wait_vlog's timeout warning unreachable.
    if "$@"; then
      return 0
    else
      rc=$?
    fi
    sleep "$interval_s"
  done
  return "$rc"
}

# remote_squote <value> : single-quote <value> for literal embedding in a
# command string handed to the VM's remote shell. Standard POSIX
# escape - close the quote, emit an escaped literal quote, reopen the quote:
#   a'b  ->  'a'\''b'
# Inside single quotes nothing is special but the quote itself, so this is
# correct for ANY byte sequence a scenario's pattern can hold - double quotes,
# backslashes, spaces, glob characters - by construction, not because those
# are the cases someone thought to escape. Every pattern that crosses the SSH
# boundary must be wrapped with this before it lands in a vm_ssh command
# string; that is the single point the fix lives at, not a per-call-site
# workaround.
remote_squote() {
  printf "'%s'" "$(printf '%s' "$1" | sed "s/'/'\\\\''/g")"
}

# vlog_cite <pattern> <label> : grep the pulled run log for a specific action
# VLOG so an assertion can cite the concrete action line, never outcome-only.
# Records the matched line (or a MISSING note) as evidence; returns 0 if found.
vlog_cite() {
  local pattern="$1" label="$2" line
  # reads the REMOTE log, not RUN_LOG_LOCAL. Every call site is an
  # action's optional `vlog` field, and actions all run BEFORE run_scenario
  # pulls the log - so the local file never exists yet and this reported
  # "run log not pulled yet" every single time it was used. presenter-startup
  # carried such a cite on its Cmd+2 for weeks; it was the one signal that
  # would have said whether the space switch ran, and it could not fire.
  # wait_vlog already greps the live log on the VM (_vlog_present); this does
  # the same, so a cite reports what actually happened at the moment it is
  # asked rather than nothing at all.
  # pattern crosses via remote_squote, not a raw '${pattern}' splice -
  # see that helper for why.
  line="$(vm_ssh "grep -aE $(remote_squote "${pattern}") /tmp/atrium-run.log | tail -1" 2>/dev/null || true)"
  if [ -n "${line}" ]; then
    record_result INFO "vlog:${label}" "VLOG: ${line}"
    return 0
  fi
  record_result INFO "vlog:${label}" "no VLOG matching /${pattern}/ in the live run log"
  return 1
}

# _vlog_present <pattern> : retry predicate - true once the REMOTE run log
# (still on the VM, not yet pulled to host) carries a line matching <pattern>.
# Backs the `wait_vlog` action: a fixed sleep tuned
# to one observed worst case (900ms -> 3000ms for the results-list VLOG
# landing late under suite load) has no margin on a slower run; polling the
# actual log line removes the guesswork.
_vlog_present() {
  local pattern="$1"
  # A single quote in the pattern silently broke this (the
  # pattern was spliced raw into a single-quoted remote string, so its own
  # quote ended that string early) and the fix that landed only detected that
  # one case and refused to run - a case list, not a solution. A
  # legitimate wait_vlog pattern with a quote was
  # refused outright. remote_squote replaces the detect-and-refuse with a
  # quoting scheme that is correct for the single quote AND everything else a
  # pattern could hold, so there is nothing left to detect.
  vm_ssh "grep -aqE $(remote_squote "${pattern}") /tmp/atrium-run.log" 2>/dev/null
}

# _tool_built <tool> : true once stage_tools' DETACHED in-VM swiftc has both
# produced the binary and written its completion marker. Polled
# rather than waited on, because the compile deliberately outlives the ssh
# channel that started it - see stage_tools for why.
# the last line matching a pattern, and how many match. A drag that
# lands short is only visible in the grip band the window prints after it,
# and a collapse that never ran leaves the count where it was.
_vlog_last() {
  local pattern="$1"
  vm_ssh "grep -aE $(remote_squote "${pattern}") /tmp/atrium-run.log | tail -1" 2>/dev/null
}

_vlog_count() {
  local pattern="$1" n
  # grep -c prints 0 AND exits 1 on no match, so a fallback echo would make
  # two lines of it. Take the first line and let an empty answer read as 0.
  n="$(vm_ssh "grep -acE $(remote_squote "${pattern}") /tmp/atrium-run.log" 2>/dev/null | head -1)"
  echo "${n:-0}"
}

_tool_built() {
  local tool="$1"
  vm_ssh "test -s '${REMOTE_TOOLS}/${tool}.done' && test -x '${REMOTE_TOOLS}/${tool}'" 2>/dev/null
}

# what axresize said, and whether it said anything at all.
#
# The old version piped stderr to /dev/null and swallowed the exit code, so a
# missing binary, a denied accessibility grant and a broken ssh hop all came
# back as an empty string - which the caller then reported as "got=<none>
# windows:[]", reading exactly like a window that was not there.
#
# On 2026-08-09 a 33-scenario run went 1,1,1,1,1,1 and then <none> for every
# scenario after the 24th, never recovering, across clean-snapshot restores.
# Five scenarios were marked failed on that. Whether the app lost its window or
# the tool stopped answering is not recoverable from those artifacts, because
# the evidence was thrown away at the point of measurement.
#
# Sets AX_TOOL_RC and AX_TOOL_ERR for the caller to report.
AX_TOOL_RC=0
AX_TOOL_ERR=""

# _ax_run <verb> <min_area> : echo axresize's stdout; sets AX_TOOL_RC/ERR.
_ax_run() {
  local verb="$1" min_area="$2" err out rc
  err="$(mktemp)"
  out="$(vm_ssh "${REMOTE_TOOLS}/axresize" "${APP_PROCESS}" "${verb}" "${min_area}" 2>"${err}")"
  rc=$?
  AX_TOOL_RC="${rc}"
  AX_TOOL_ERR="$(tr '\n' ' ' <"${err}" | cut -c1-200)"
  rm -f "${err}"
  printf '%s' "${out}"
}

# _ax_count <min_area> : echo the integer real-window count from axresize.
_ax_count() {
  local out
  out="$(_ax_run count "$1")"
  # axresize prints: count=<n> minArea=<a> total=<t>
  printf '%s' "${out}" | sed -n 's/^count=\([0-9][0-9]*\).*/\1/p'
}

# _count_is <expected> <min_area> : predicate for retry - true when the real
# window count equals expected.
_count_is() { [ "$(_ax_count "$2")" = "$1" ]; }

# assert_ax_window_count <expected> <min_area>
# The presenter/window-jump regression signature is "a second NSWindow gets
# ordered on screen"; a clean cross-profile switch keeps exactly ONE real
# Atrium AX window. Retries to let the switch settle.
assert_ax_window_count() {
  # An absent `expected` reaches the comparison as an empty string, which
  # reports "expected= got=2" - a malformed scenario wearing the costume of a
  # real failure.
  if [ -z "${1}" ]; then
    record_result FAIL "ax_window_count" "scenario is missing 'expected' - nothing was compared"
    return 1
  fi
  local expected="$1" min_area="${2:-200000}" got raw
  # Retry the count: the window set is briefly in flux during a switch.
  retry 8 1 _count_is "${expected}" "${min_area}" || true
  got="$(_ax_count "${min_area}")"

  # a tool that did not answer is a HARNESS failure, not a product
  # one. Reporting it as "no windows" is a claim about the app that nothing
  # measured, and it is the shape a whole run of false failures takes.
  if [ -z "${got}" ]; then
    # Say whether the app was even there. axresize prints nothing when the
    # process is gone, so "no windows" and "no app" arrive identically - and
    # they are very different findings. An app that exited before the
    # assertions ran makes every later gate fail for one reason.
    local alive
    alive="$(vm_ssh pgrep -x "${APP_PROCESS}" 2>/dev/null || true)"
    record_result HARNESS "ax_window_count" \
      "axresize did not answer (rc=${AX_TOOL_RC}) - nothing was measured about the app. ${APP_PROCESS} process: ${alive:-GONE}. stderr: ${AX_TOOL_ERR:-<empty>}"
    return 1
  fi

  raw="$(_ax_run list "${min_area}")"
  if [ "${got}" = "${expected}" ]; then
    record_result PASS "ax_window_count" "expected=${expected} got=${got} (min_area=${min_area}) windows:[${raw//$'\n'/ | }]"
    return 0
  fi
  record_result FAIL "ax_window_count" "expected=${expected} got=${got} (min_area=${min_area}) windows:[${raw//$'\n'/ | }]"
  return 1
}

# assert_ax_window_kind <url_pattern> <expected> [min_area]
# How many open windows are built around a given piece of chrome.
#
# ax_window_count cannot answer "which KIND of window opened". On the
# external-link handover both paths produce two windows, and in the captured
# control run both were AXStandardWindow, both about 1024x680, both titled
# "Untitled" - so a count of two is equally true when the handover opens an
# ordinary space window, which is the exact thing the handover must not do.
# A window's chrome is what differs: the space window holds the sidebar WebUI,
# the little window holds the foyer strip and has no sidebar at all. Both are
# web areas in the window's own AX subtree carrying their route's URL, so this
# reads the window's kind off the window instead of off a log line saying what
# the app meant to open.
#
# Substring match on the URL, so "/foyer" and "/sidebar" are the two kinds.
assert_ax_window_kind() {
  if [ -z "${1}" ]; then
    record_result FAIL "ax_window_kind" "scenario is missing 'pattern' - nothing was matched"
    return 1
  fi
  # Same trap as ax_window_count/min_delta: an absent expected compares as ""
  # and reports "expected= got=1", a malformed scenario wearing the costume of
  # a real failure.
  if [ -z "${2}" ]; then
    record_result FAIL "ax_window_kind" "scenario is missing 'expected' - nothing was compared"
    return 1
  fi
  local pattern="$1" expected="$2" min_area="${3:-200000}" raw got
  # Retry: a window that has just opened may not have committed its chrome
  # document yet, and the same 8x1s budget ax_window_count uses covers that.
  retry 8 1 _kind_is "${pattern}" "${expected}" "${min_area}" || true
  raw="$(_ax_run kinds "${min_area}")"

  # A tool that did not answer is a HARNESS failure, not a product one.
  # Without this, "axresize is missing" reads identically to "no
  # window of that kind exists", and the second is a claim about the app.
  if ! printf '%s' "${raw}" | grep -q '^kinds='; then
    local alive
    alive="$(vm_ssh pgrep -x "${APP_PROCESS}" 2>/dev/null || true)"
    record_result HARNESS "ax_window_kind" \
      "axresize kinds did not answer (rc=${AX_TOOL_RC}) - nothing was measured. ${APP_PROCESS} process: ${alive:-GONE}. stderr: ${AX_TOOL_ERR:-<empty>}"
    return 1
  fi
  got="$(_kind_count "${pattern}" "${min_area}")"
  if [ "${got}" = "${expected}" ]; then
    record_result PASS "ax_window_kind" \
      "pattern='${pattern}' expected=${expected} got=${got} windows:[${raw//$'\n'/ | }]"
    return 0
  fi
  record_result FAIL "ax_window_kind" \
    "pattern='${pattern}' expected=${expected} got=${got} windows:[${raw//$'\n'/ | }]"
  return 1
}

# assert_ax_window_subrole <title> <expected>
# The AX subrole of the window with that title. A tiling window manager reads
# this to decide whether it manages a window.
assert_ax_window_subrole() {
  if [ -z "${1}" ] || [ -z "${2}" ]; then
    record_result FAIL "ax_window_subrole" "scenario is missing 'title' or 'expected' - nothing was compared"
    return 1
  fi
  local title="$1" expected="$2" raw got
  _subrole_of() { _ax_run list 0 | sed -n "s/^subrole=\([^ ]*\) .* title=\"${title}\"\$/\1/p" | head -1; }
  _subrole_is() { [ "$(_subrole_of)" = "${expected}" ]; }
  retry 8 1 _subrole_is || true
  got="$(_subrole_of)"
  raw="$(_ax_run list 0)"
  if [ -z "${raw}" ]; then
    record_result HARNESS "ax_window_subrole" "axresize list did not answer (rc=${AX_TOOL_RC}) - nothing was measured. stderr: ${AX_TOOL_ERR:-<empty>}"
    return 1
  fi
  if [ "${got}" = "${expected}" ]; then
    record_result PASS "ax_window_subrole" "title='${title}' expected=${expected} got=${got}"
    return 0
  fi
  record_result FAIL "ax_window_subrole" "title='${title}' expected=${expected} got=${got:-<no such window>} windows:[${raw//$'\n'/ | }]"
  return 1
}

# _kind_count <pattern> <min_area> : windows whose AX subtree holds a URL
# containing <pattern>. Counts WINDOW LINES, not URLs, so a window that somehow
# holds the route twice still counts once.
_kind_count() {
  _ax_run kinds "$2" | grep -c "^window .*urls=\[[^]]*$1" || true
}

_kind_is() { [ "$(_kind_count "$1" "$3")" = "$2" ]; }

# assert_theme_changed <before_frame> <after_frame> <region_csv> <min_delta>
# region_csv = x,y,w,h in the captured frame's PIXEL space (2x retina).
# Uses host pixcmp on raw pixels; a byte-identical pair exits 3 = FAKE.
assert_theme_changed() {
  local before="$1" after="$2" region="$3" min_delta="$4"
  local x y w h
  IFS=',' read -r x y w h <<<"${region}"
  if [ ! -f "${before}" ] || [ ! -f "${after}" ]; then
    record_result FAIL "theme_changed" "missing capture frame(s): before=${before} after=${after}"
    return 1
  fi
  # An absent min_delta reaches pixcmp as an empty argument, which it rejects
  # with no output at all - so the run reports a FAIL carrying nothing but the
  # region, which reads exactly like a measurement that came back empty. It is
  # a malformed scenario, and it should say so.
  if [ -z "${min_delta}" ]; then
    record_result FAIL "theme_changed" "scenario is missing min_delta - nothing was measured"
    return 1
  fi
  local out rc
  out="$("${HOST_PIXCMP}" diff "${before}" "${after}" "${x}" "${y}" "${w}" "${h}" "${min_delta}")"
  rc=$?
  case "${rc}" in
    0) record_result PASS "theme_changed" "${out} region=${region} before=$(basename "${before}") after=$(basename "${after}")" ; return 0 ;;
    3) record_result FAIL "theme_changed" "FAKE(byte-identical frames) ${out}" ; return 1 ;;
    *) record_result FAIL "theme_changed" "${out} region=${region}" ; return 1 ;;
  esac
}

# assert_pixels_changed <before_frame> <after_frame> <region_csv> <min_changed_pct> [tol]
#
# the assertion that measures what a mean cannot. theme_changed above
# grades pixcmp's `diff`, whose delta is a distance between the two region
# MEANS - and a mean has now been caught reading near zero for working behaviour
# twice: once because the moving thing lost its tint, and once because the region
# spanned both ends of the movement so what left one end cancelled what arrived
# at the other. This grades pixcmp's `moved` instead, which compares
# CORRESPONDING PIXELS and counts how many changed. Nothing is averaged before
# the comparison, so nothing cancels, and the count does not care how saturated
# the moving thing is.
#
# It is a companion to theme_changed, not a replacement: this one says the
# content moved, that one says the colour is right. The whole argument, and the
# refusals, are in moved_outcome.py, which decides the verdict from pixcmp's
# numbers the same way quit_outcome.py decides a shutdown from pgrep's.
assert_pixels_changed() {
  local before="$1" after="$2" region="$3" min_changed_pct="$4" tol="${5:-}"
  local label="pixels_changed"
  local x y w h
  IFS=',' read -r x y w h <<<"${region}"
  if [ ! -f "${before}" ] || [ ! -f "${after}" ]; then
    record_result FAIL "${label}" "missing capture frame(s): before=${before} after=${after}"
    return 1
  fi
  local out rc evidence grc
  set +e
  # shellcheck disable=SC2086  # tol is deliberately unquoted: empty means "omit
  # the optional argument and let pixcmp use its own default".
  out="$("${HOST_PIXCMP}" moved "${before}" "${after}" "${x}" "${y}" "${w}" "${h}" "${min_changed_pct}" ${tol} 2>&1)"
  rc=$?
  evidence="$(python3 "${MOVED_OUTCOME}" "${min_changed_pct}" "${rc}" "${out}" 2>&1)"
  grc=$?
  set -e
  case "${grc}" in
    0) record_result PASS "${label}" \
         "${evidence} region=${region} before=$(basename "${before}") after=$(basename "${after}")"
       return 0 ;;
    # 1 is the harness failing to measure - an unusable threshold, too small a
    # region, or pixcmp not answering. None of those is a statement about the
    # app, so none may be recorded as one (a harness failure is never an app verdict).
    1) record_result HARNESS "${label}" "${evidence} region=${region}"
       return 1 ;;
    *) record_result FAIL "${label}" \
         "${evidence} region=${region} before=$(basename "${before}") after=$(basename "${after}")"
       return 1 ;;
  esac
}

# assert_region_stable <before_frame> <after_frame> <region_csv> <max_delta>
# the inverse of assert_theme_changed - proves a region did NOT
# visibly change between two captures, for settle-time-pop regressions where
# the bug IS a difference (a decoration popping in/out) rather than an
# expected one. region_csv = x,y,w,h in the captured frame's PIXEL space (2x
# retina), same convention as theme_changed. Unlike theme_changed, a
# byte-identical pair here is the STRONGEST possible pass (perfectly stable),
# not a fake - so pixcmp's rc=3 is treated as PASS, not FAIL.
assert_region_stable() {
  local before="$1" after="$2" region="$3" max_delta="$4"
  local x y w h
  IFS=',' read -r x y w h <<<"${region}"
  if [ ! -f "${before}" ] || [ ! -f "${after}" ]; then
    record_result FAIL "region_stable" "missing capture frame(s): before=${before} after=${after}"
    return 1
  fi
  local out rc
  # pixcmp's own minDelta arg is reused as the boundary; its PASS/FAIL
  # semantics are inverted below rather than in the tool itself, so the same
  # binary serves both assertion directions.
  out="$("${HOST_PIXCMP}" diff "${before}" "${after}" "${x}" "${y}" "${w}" "${h}" "${max_delta}")"
  rc=$?
  case "${rc}" in
    0) record_result FAIL "region_stable" "${out} region=${region} - region changed by >= ${max_delta}, expected stable" ; return 1 ;;
    2) record_result PASS "region_stable" "${out} region=${region} before=$(basename "${before}") after=$(basename "${after}")" ; return 0 ;;
    3) record_result PASS "region_stable" "IDENTICAL(byte-identical frames) ${out} region=${region}" ; return 0 ;;
    *) record_result FAIL "region_stable" "${out} region=${region}" ; return 1 ;;
  esac
}

# assert_window_moved <x> <y> [tolerance]
# the position half of assert_resize_applied. Sends the move itself
# and reads the frame back, for the same reason that one does - a scenario that
# only asked and never checked would report a click's target as known when it
# was assumed.
#
# Why position and not size. Every Atrium window opens at pos=0,30, so two
# windows overlap exactly, their sidebars share one x band, and a click at a
# fixed coordinate names no particular surface. Resizing does not fix it: the
# content card moves and the sidebars stay stacked (measured on
# 2026-08-18). Moving one window apart is what makes the target unambiguous.
assert_window_moved() {
  local x="$1" y="$2" tol="${3:-40}"
  vm_ssh "${REMOTE_TOOLS}/axresize" "${APP_PROCESS}" pos "${x}" "${y}" >/dev/null 2>&1 || true
  sleep 1  # let the AX frame change settle before reading it back
  local got gx gy
  got="$(_ax_run get "")"
  # axresize get prints: pos X Y size W H
  gx="$(printf '%s' "${got}" | sed -n 's/.*pos \([0-9-][0-9]*\) [0-9-][0-9]*.*/\1/p' | tail -1)"
  gy="$(printf '%s' "${got}" | sed -n 's/.*pos [0-9-][0-9]* \([0-9-][0-9]*\).*/\1/p' | tail -1)"
  # Same split assert_resize_applied makes: "could not read it back" is the
  # harness failing to measure, not a claim that the move did not apply.
  if [ -z "${gx}" ] || [ -z "${gy}" ]; then
    record_result HARNESS "window_moved" \
      "axresize did not answer (rc=${AX_TOOL_RC}) - nothing was measured about the window. got='${got}' stderr: ${AX_TOOL_ERR:-<empty>}"
    return 1
  fi
  local dx=$(( gx - x )); dx=${dx#-}
  local dy=$(( gy - y )); dy=${dy#-}
  if [ "${dx}" -le "${tol}" ] && [ "${dy}" -le "${tol}" ]; then
    record_result PASS "window_moved" "target=${x},${y} got=${gx},${gy} tol=${tol} (dx=${dx} dy=${dy})"
    return 0
  fi
  record_result FAIL "window_moved" \
    "target=${x},${y} got=${gx},${gy} tol=${tol} (dx=${dx} dy=${dy})"
  return 1
}

# grade_quit <label> <chord_window_s> <fallback> <before> <after_chord> <after_fallback>
#
# The ONE place that decides whether a Cmd+Q ended the app. Both
# stopping paths - assert_quit here and shutdown_app in the runner - come
# through it, because they were answering the same question two different ways:
# this one failed on a process that was still there, and shutdown_app sent
# SIGTERM and said nothing, so a swallowed chord could not fail anything at all.
#
# <fallback> is `none` when the caller sent no fallback, or the signal it sent.
# The pids are the raw pgrep output at each step, passed through so the evidence
# line can name them.
#
# Reads the app's shutdown-signal lines out of the LIVE log inside the VM rather
# than RUN_LOG_LOCAL: the local copy is pulled on a schedule this function does
# not control, and the line in question is written in the last moments of the
# process. Kept as an artifact - when a verdict is later disputed, the input it
# was made from is the only way to answer.
grade_quit() {
  local label="$1" window_s="$2" fallback="$3"
  local before="$4" after_chord="$5" after_fallback="$6"
  local sig_file="${RESULTS_DIR}/quit-${LAUNCH_INDEX:-1}-signal-lines.txt"
  vm_ssh "grep -a 'Handling shutdown for signal' /tmp/atrium-run.log || true" \
    >"${sig_file}" 2>/dev/null || true
  # set -e off around the grader: a red verdict is a result to record, not a
  # reason to abort the run, and `rc=$?` after an `if` reads the IF's status
  # rather than the command's (see retry() above).
  local evidence rc
  set +e
  evidence="$(python3 "${QUIT_OUTCOME}" "${APP_PROCESS}" "${window_s}" \
      "${fallback}" "${before}" "${after_chord}" "${after_fallback}" \
      "${sig_file}" 2>&1)"
  rc=$?
  set -e
  if [ "${rc}" -eq 0 ]; then
    record_result PASS "${label}" "${evidence}"
    return 0
  fi
  record_result FAIL "${label}" "${evidence}"
  return 1
}

# assert_quit : Cmd+Q, then confirm the process is actually gone. keycode 12 = 'q'.
assert_quit() {
  # what the process was doing BEFORE the chord is half the
  # measurement. An app that had already died reads as a flawless quit to a
  # check that only asks whether it is gone afterwards.
  local before still=""
  before="$(vm_ssh pgrep -x "${APP_PROCESS}" 2>/dev/null || true)"
  vm_ssh "${REMOTE_TOOLS}/cgevent" keypress 12 cmd >/dev/null 2>&1 || true
  # Poll pgrep: gone is necessary, and grade_quit decides whether it is enough.
  local deadline=$(( SECONDS + 10 ))
  while [ "$SECONDS" -le "$deadline" ]; do
    still="$(vm_ssh pgrep -x "${APP_PROCESS}" 2>/dev/null || true)"
    [ -z "${still}" ] && break
    sleep 1
  done
  # No fallback here, deliberately: a wedged app under a `quit` assertion IS the
  # finding, and teardown forces it down afterwards either way.
  local rc=0
  grade_quit "quit" 10 none "${before}" "${still}" "" || rc=1
  # re-pull the log now that the quit has happened. run_scenario pulls
  # it once BEFORE evaluating assertions, so anything Cmd+Q itself emits - the
  # quit route taken, the shutdown sequence - is not in it. Without this a vlog
  # assertion about quit-time behaviour can never match, and worse, a
  # vlog_absent one passes trivially: measured on quit-with-tab, whose
  # "the palette did not route the quit" gate went green while the line it
  # looks for could not have been in the log either way.
  #
  # Consequence for scenario authors: assertions listed BEFORE `quit` see the
  # pre-quit log, assertions listed AFTER it see the post-quit log. Order
  # matters, which is why it is stated here and in the file header.
  pull_run_log
  # tell the teardown-assertion phase the app is already stopped, so
  # it does not send a second Cmd+Q into an empty desktop and then wait fifteen
  # seconds for a process that left long ago. Keyed on the process being gone,
  # NOT on the verdict: a quit that failed because a signal ended it is still a
  # stopped app, and chording an empty desktop is what the old harness did.
  # shellcheck disable=SC2034  # read by vm-scenario.sh, which sources this
  [ -z "${still}" ] && APP_QUIT=1
  return "${rc}"
}

# assert_vlog <pattern> [<label>]
# PASS when the pulled run log carries a line matching <pattern> (extended
# regex). Unlike vlog_cite (INFO-only evidence attached to a key action), this
# is a real PASS/FAIL gate - the only way to assert a behavior that shows up
# as a VLOG rather than a window/pixel change (e.g. the routing hooks emit
# "Atrium: routing rerouted ..." on a matched navigation, with no reliable
# window delta for a same-profile route). Cites the matched line as evidence.
assert_vlog() {
  local pattern="$1" label="${2:-vlog}" line
  if [ ! -f "${RUN_LOG_LOCAL}" ]; then
    record_result FAIL "vlog:${label}" "run log not pulled: ${RUN_LOG_LOCAL}"
    return 1
  fi
  line="$(grep -aE "${pattern}" "${RUN_LOG_LOCAL}" | tail -1 || true)"
  if [ -n "${line}" ]; then
    record_result PASS "vlog:${label}" "matched /${pattern}/ -> ${line}"
    return 0
  fi
  record_result FAIL "vlog:${label}" "no line matching /${pattern}/ in run log"
  return 1
}

# assert_vlog_after <after_pattern> <first_pattern> [<label>]
# PASS when a line matching <after_pattern> appears LATER in the run log than
# the first line matching <first_pattern>.
#
# exists because presence alone is not evidence of a transition, and
# two assertions were passing on that confusion. DevTools logs
# "devtools pane ... docked=0 bounds=0,0 0x0 hide_inspected=0" at LAUNCH, and
# the line it logs after a real undock is byte-identical - same pane pointer,
# same zero bounds. So "the pane reports docked=0" was true of an app that had
# never opened DevTools at all, and devtools-close-while-docked reported that
# assertion as PASS throughout the whole period DevTools was
# completely dead.
#
# Ordering is the only thing that separates them, and no existing assertion
# could express it: vlog speaks to presence, vlog_absent to its inverse.
assert_vlog_after() {
  local after_pattern="$1" first_pattern="$2" label="${3:-vlog_after}"
  if [ ! -f "${RUN_LOG_LOCAL}" ]; then
    record_result FAIL "vlog_after:${label}" "run log not pulled: ${RUN_LOG_LOCAL}"
    return 1
  fi
  local first_line
  first_line="$(grep -anE "${first_pattern}" "${RUN_LOG_LOCAL}" | head -1 | cut -d: -f1)"
  if [ -z "${first_line}" ]; then
    record_result FAIL "vlog_after:${label}" \
      "nothing matched the FIRST pattern /${first_pattern}/, so there is no transition to check"
    return 1
  fi
  local after_line
  after_line="$(awk -v n="${first_line}" 'NR > n' "${RUN_LOG_LOCAL}" \
    | grep -aE "${after_pattern}" | head -1 || true)"
  if [ -n "${after_line}" ]; then
    record_result PASS "vlog_after:${label}" \
      "/${after_pattern}/ follows /${first_pattern}/ (line ${first_line}) -> ${after_line}"
    return 0
  fi
  record_result FAIL "vlog_after:${label}" \
    "/${first_pattern}/ matched at line ${first_line} but no /${after_pattern}/ appears after it"
  return 1
}

# assert_vlog_absent <pattern> [<label>]
# The inverse gate: PASS when the pulled run log carries NO line matching
# <pattern>. Exists because a whole class of scenario depends on something
# NOT happening, and assert_vlog can only speak to presence.
#
# empty-window-quit asserted "Cmd+Q quits" while the command palette
# was still up, so every green run actually exercised the palette's
# pass-to-main-menu route and never the browser window's own. The quit
# assertion could not tell the two apart, and the bug it was guarding survived
# two closes underneath it. Asserting the palette route was NOT taken is what
# distinguishes them, and that is an absence.
#
# Same "run log not pulled" failure as assert_vlog: a missing log means the
# check did not run, which must never read as the absence it is looking for.
assert_vlog_absent() {
  local pattern="$1" label="${2:-vlog_absent}" line
  if [ ! -f "${RUN_LOG_LOCAL}" ]; then
    record_result FAIL "vlog_absent:${label}" "run log not pulled: ${RUN_LOG_LOCAL}"
    return 1
  fi
  line="$(grep -aE "${pattern}" "${RUN_LOG_LOCAL}" | tail -1 || true)"
  if [ -z "${line}" ]; then
    record_result PASS "vlog_absent:${label}" "no line matching /${pattern}/, as required"
    return 0
  fi
  record_result FAIL "vlog_absent:${label}" "expected NO match for /${pattern}/ but found -> ${line}"
  return 1
}

# An empty EVAL_RESULT makes ${!EVAL_RESULT[*]} unbound under `set -u`, and
# ${!name[*]:-default} is a bad substitution rather than a fallback.
eval_ids_that_read() {
  if [ "${#EVAL_RESULT[@]}" -eq 0 ]; then
    printf 'none'
    return 0
  fi
  printf '%s' "${!EVAL_RESULT[*]}"
}

# assert_eval_equals <eval-id> <expected>
# a blind read is HARNESS, not INFO. Nothing
# was measured so the app must not be accused, but INFO let a scenario report
# "5 passed, 0 failed" while looking at nothing. The available ids go in the
# message because the usual cause is a target that never matched.
assert_eval_equals() {
  local eid="$1" want="$2"
  if [ -z "${EVAL_RESULT[${eid}]+set}" ]; then
    record_result HARNESS "eval_equals:${eid}" \
      "no value stored - the eval could not look, so there is nothing to compare. ids that did read: $(eval_ids_that_read)"
    return 1
  fi
  if [ "${EVAL_RESULT[${eid}]}" = "${want}" ]; then
    record_result PASS "eval_equals:${eid}" "${eid} is ${want}"
    return 0
  fi
  record_result FAIL "eval_equals:${eid}" \
    "${eid} is ${EVAL_RESULT[${eid}]}, expected ${want}"
  return 1
}

# assert_eval_changed <before-id> <after-id>
assert_eval_changed() {
  local a="$1" b="$2"
  if [ -z "${EVAL_RESULT[${a}]+set}" ] || [ -z "${EVAL_RESULT[${b}]+set}" ]; then
    record_result HARNESS "eval_changed:${a}->${b}" \
      "one of the reads could not look, so a difference would mean nothing. ids that did read: $(eval_ids_that_read)"
    return 1
  fi
  if [ "${EVAL_RESULT[${a}]}" != "${EVAL_RESULT[${b}]}" ]; then
    record_result PASS "eval_changed:${a}->${b}" \
      "${EVAL_RESULT[${a}]} -> ${EVAL_RESULT[${b}]}"
    return 0
  fi
  record_result FAIL "eval_changed:${a}->${b}" "unchanged at ${EVAL_RESULT[${a}]}"
  return 1
}

# assert_region_mean <capture-id> <x> <y> <w> <h> <max_mean> [<min_mean>]
#                    [<label>] [<min_r>] [<max_g>] [<max_b>] [<max_r>]
# PASS when the region's mean brightness across the three channels sits between
# <min_mean> (default 0) and <max_mean>, and every given channel bound holds.
#
# every other pixel assertion compares two captures, and a surface
# that is half drawn is wrong in ONE frame - the blank half is not a change
# from anything, it is simply blank. A ceiling on brightness is what separates
# a dark panel from the white the body shows through where the panel stops.
#
# over pure red the mean cannot separate a clean corner beside
# macOS 26's window rim (91.6) from a darkening wedge (88.5). A wedge drops
# R, an added light fill lifts G/B, the rim does neither - the channel
# bounds are what actually fail on the defect.
assert_region_mean() {
  local id="$1" x="$2" y="$3" w="$4" h="$5" max="$6" min="${7:-0}" label="${8:-region_mean}"
  local min_r="${9:-}" max_g="${10:-}" max_b="${11:-}" max_r="${12:-}"
  local png="${CAPTURE_LAST[${id}]:-}"
  if [ -z "${png}" ] || [ ! -f "${png}" ]; then
    record_result FAIL "region_mean:${label}" "capture '${id}' has no frame to read"
    return 1
  fi
  local out mean r g b
  out="$("${HOST_PIXCMP}" stats "${png}" "${x}" "${y}" "${w}" "${h}" 2>/dev/null)" || {
    record_result FAIL "region_mean:${label}" "pixcmp stats failed on ${png}"
    return 1
  }
  read -r r g b <<<"$(echo "${out}" | sed -n 's/^mean=\([0-9.]*\),\([0-9.]*\),\([0-9.]*\).*/\1 \2 \3/p')"
  mean="$(awk "BEGIN{printf \"%.1f\", (${r}+${g}+${b})/3}")"
  if ! awk "BEGIN{exit !(${mean} >= ${min} && ${mean} <= ${max})}"; then
    record_result FAIL "region_mean:${label}" "mean ${mean} outside ${min}..${max} (${out}) region=${x},${y},${w},${h}"
    return 1
  fi
  if [ -n "${min_r}" ] && ! awk "BEGIN{exit !(${r} >= ${min_r})}"; then
    record_result FAIL "region_mean:${label}" "R ${r} below min_r ${min_r} - something darkens this region (${out}) region=${x},${y},${w},${h}"
    return 1
  fi
  if [ -n "${max_r}" ] && ! awk "BEGIN{exit !(${r} <= ${max_r})}"; then
    record_result FAIL "region_mean:${label}" "R ${r} above max_r ${max_r} - this region is lighter or redder than expected (${out}) region=${x},${y},${w},${h}"
    return 1
  fi
  if [ -n "${max_g}" ] && ! awk "BEGIN{exit !(${g} <= ${max_g})}"; then
    record_result FAIL "region_mean:${label}" "G ${g} above max_g ${max_g} - added light in this region (${out}) region=${x},${y},${w},${h}"
    return 1
  fi
  if [ -n "${max_b}" ] && ! awk "BEGIN{exit !(${b} <= ${max_b})}"; then
    record_result FAIL "region_mean:${label}" "B ${b} above max_b ${max_b} - added light in this region (${out}) region=${x},${y},${w},${h}"
    return 1
  fi
  record_result PASS "region_mean:${label}" "mean ${mean} within ${min}..${max}, channels ok (${out}) region=${x},${y},${w},${h}"
  return 0
}

# assert_region_every_frame <capture-id> <x> <y> <w> <h> <max_mean> [<min_mean>] [<label>]
# region_mean for every frame of the capture, not only the last. A white or
# black flash at first paint lasts one or two frames and is gone by the last.
assert_region_every_frame() {
  local id="$1" x="$2" y="$3" w="$4" h="$5" max="$6" min="${7:-0}" label="${8:-region_every_frame}"
  local last="${CAPTURE_LAST[${id}]:-}"
  if [ -z "${last}" ] || [ ! -f "${last}" ]; then
    record_result FAIL "region_every_frame:${label}" "capture '${id}' has no frame to read"
    return 1
  fi
  local png out mean frames=0
  while IFS= read -r png; do
    out="$("${HOST_PIXCMP}" stats "${png}" "${x}" "${y}" "${w}" "${h}" 2>/dev/null)" || {
      record_result FAIL "region_every_frame:${label}" "pixcmp stats failed on ${png}"
      return 1
    }
    mean="$(echo "${out}" | awk -F'[=,]' '/^mean=/{printf "%.1f", ($2+$3+$4)/3}')"
    frames=$((frames + 1))
    if ! awk "BEGIN{exit !(${mean} >= ${min} && ${mean} <= ${max})}"; then
      record_result FAIL "region_every_frame:${label}" \
        "$(basename "${png}") mean ${mean} outside ${min}..${max} region=${x},${y},${w},${h}"
      return 1
    fi
  done < <(find "$(dirname "${last}")" -name 'frame-*.png' 2>/dev/null | sort)
  record_result PASS "region_every_frame:${label}" \
    "${frames} frames within ${min}..${max} region=${x},${y},${w},${h}"
  return 0
}

# assert_band_step <capture-id> <x> <y> <len> <thick> <right|down> <expected>
#                  <tol> [<label>] [<colour_tol>]
# how wide a band is has no mean. A 5-point gap and a 10-point one
# average alike when the colour beyond the step is the same either way.
assert_band_step() {
  local id="$1" x="$2" y="$3" len="$4" thick="$5" dir="$6"
  local expected="$7" tol="$8" label="${9:-band_step}" colour_tol="${10:-}"
  local png="${CAPTURE_LAST[${id}]:-}"
  if [ -z "${png}" ] || [ ! -f "${png}" ]; then
    record_result FAIL "band_step:${label}" "capture '${id}' has no frame to read"
    return 1
  fi
  local out status
  out="$("${HOST_PIXCMP}" step "${png}" "${x}" "${y}" "${len}" "${thick}" "${dir}" \
    "${expected}" "${tol}" ${colour_tol:+"${colour_tol}"} 2>&1)"
  status=$?
  if [ "${status}" -eq 0 ]; then
    record_result PASS "band_step:${label}" "${out} scan=${x},${y} ${dir} ${len}"
    return 0
  fi
  record_result FAIL "band_step:${label}" "${out} scan=${x},${y} ${dir} ${len}"
  return 1
}

# assert_variance_collapsed <before-id> <after-id> <x> <y> <w> <h> <max_ratio> [<label>]
# A tint alone shrinks variance proportionally to its own alpha - region_mean
# cannot tell that apart from a real blur.
assert_variance_collapsed() {
  local before_id="$1" after_id="$2" x="$3" y="$4" w="$5" h="$6"
  local max_ratio="$7" label="${8:-variance_collapsed}"
  local before_png="${CAPTURE_LAST[${before_id}]:-}"
  local after_png="${CAPTURE_LAST[${after_id}]:-}"
  if [ -z "${before_png}" ] || [ ! -f "${before_png}" ] ||
     [ -z "${after_png}" ] || [ ! -f "${after_png}" ]; then
    record_result FAIL "variance_collapsed:${label}" \
      "missing capture frame(s): before=${before_png} after=${after_png}"
    return 1
  fi
  local before_out after_out before_var after_var bound
  before_out="$("${HOST_PIXCMP}" stats "${before_png}" "${x}" "${y}" "${w}" "${h}" 2>/dev/null)" || {
    record_result FAIL "variance_collapsed:${label}" "pixcmp stats failed on ${before_png}"
    return 1
  }
  after_out="$("${HOST_PIXCMP}" stats "${after_png}" "${x}" "${y}" "${w}" "${h}" 2>/dev/null)" || {
    record_result FAIL "variance_collapsed:${label}" "pixcmp stats failed on ${after_png}"
    return 1
  }
  before_var="$(echo "${before_out}" | sed -n 's/.*var=\([0-9.]*\).*/\1/p')"
  after_var="$(echo "${after_out}" | sed -n 's/.*var=\([0-9.]*\).*/\1/p')"
  bound="$(awk "BEGIN{printf \"%.1f\", ${before_var} * ${max_ratio}}")"
  if ! awk "BEGIN{exit !(${after_var} <= ${bound})}"; then
    record_result FAIL "variance_collapsed:${label}" \
      "after var ${after_var} above ${bound} (${max_ratio} of before var ${before_var}) region=${x},${y},${w},${h}"
    return 1
  fi
  record_result PASS "variance_collapsed:${label}" \
    "before var ${before_var} -> after var ${after_var} (ratio <= ${max_ratio}) region=${x},${y},${w},${h}"
  return 0
}

# assert_vlog_count <pattern> <max> [<min>] [<label>]
# PASS when the run log carries between <min> (default 1) and <max> lines
# matching <pattern>.
#
# "does not run on every paint" is a claim about HOW OFTEN, and
# presence cannot answer it - an event-driven sampler and a per-paint one both
# log at least once. A ceiling is the only thing that separates them.
#
# The floor is not decoration: a pattern that matches nothing would otherwise
# satisfy any ceiling, so the assertion would pass hardest when the feature was
# entirely absent.
assert_vlog_count() {
  local pattern="$1" max="$2" min="${3:-1}" label="${4:-vlog_count}" n
  if [ ! -f "${RUN_LOG_LOCAL}" ]; then
    record_result FAIL "vlog_count:${label}" "run log not pulled: ${RUN_LOG_LOCAL}"
    return 1
  fi
  n="$(grep -acE "${pattern}" "${RUN_LOG_LOCAL}" || true)"
  if [ "${n}" -ge "${min}" ] && [ "${n}" -le "${max}" ]; then
    record_result PASS "vlog_count:${label}" \
      "/${pattern}/ matched ${n} time(s), within ${min}..${max}"
    return 0
  fi
  record_result FAIL "vlog_count:${label}" \
    "/${pattern}/ matched ${n} time(s), outside ${min}..${max}"
  return 1
}

# assert_recency_walk_distinct <count> <pattern> [<after_pattern>] [<label>]
# checks only the LEADING <count> matches, not a trailing window.
assert_recency_walk_distinct() {
  local count="$1" pattern="$2" after_pattern="${3:-}" label="${4:-recency_walk_distinct}"
  if [ ! -f "${RUN_LOG_LOCAL}" ]; then
    record_result FAIL "recency_walk_distinct:${label}" "run log not pulled: ${RUN_LOG_LOCAL}"
    return 1
  fi
  local evidence grc
  set +e
  evidence="$(python3 - "${count}" "${pattern}" "${RUN_LOG_LOCAL}" "${after_pattern}" 2>&1 <<'PY'
import re
import sys

count = int(sys.argv[1])
pattern = sys.argv[2]
log_path = sys.argv[3]
after_pattern = sys.argv[4] or None

try:
    with open(log_path, encoding="utf-8", errors="replace") as handle:
        lines = handle.readlines()
except OSError as exc:
    print(f"unusable: could not read run log: {exc}")
    sys.exit(1)

if after_pattern:
    after_regex = re.compile(after_pattern)
    marker = next((i for i, line in enumerate(lines) if after_regex.search(line)), None)
    if marker is None:
        print(f"unusable: after_pattern {after_pattern!r} never matched")
        sys.exit(1)
    lines = lines[marker + 1:]

try:
    regex = re.compile(pattern)
except re.error as exc:
    print(f"unusable: bad pattern {pattern!r}: {exc}")
    sys.exit(1)

targets = []
for line in lines:
    match = regex.search(line)
    if not match or not match.groups():
        continue
    try:
        targets.append(int(match.group(1)))
    except (ValueError, IndexError):
        continue

if len(targets) < count:
    print(f"only {len(targets)} matching line(s), need {count}")
    sys.exit(1)

window = targets[:count]
if len(set(window)) == count:
    print(f"targets {window} all distinct")
    sys.exit(0)
print(f"targets {window} - oscillated (a value repeats)")
sys.exit(2)
PY
)"
  grc=$?
  set -e
  case "${grc}" in
    0) record_result PASS "recency_walk_distinct:${label}" "${evidence}"; return 0 ;;
    # 1 is the harness failing to measure (too few matches, a bad pattern, no
    # log) - not a statement about the app, so not recorded as one (the same
    # rule pixels_changed follows).
    1) record_result HARNESS "recency_walk_distinct:${label}" "${evidence}"; return 1 ;;
    *) record_result FAIL "recency_walk_distinct:${label}" "${evidence}"; return 1 ;;
  esac
}

# assert_file_contains <kind> <remote path> <staged copy> <json path> <expected json> [<label>]
#
# the first assertion that reads a FILE. Everything above it grades a
# window, a pixel or a log line, and all three are satisfied by a repaint - an
# app that draws the new value and writes nothing passes every one of them. For
# a persistence question that is the only assertion that means anything.
#
# The file is pulled to the artifact dir before it is graded, so a disputed
# result can be re-read rather than re-run. What the comparison is, and why the
# staged copy is part of it, is argued in file_value.py.
assert_file_contains() {
  local kind="$1" remote="$2" staged="$3" path="$4" expected="$5"
  local label="${6:-${kind}}" pulled evidence rc
  if [ -z "${remote}" ]; then
    record_result FAIL "file_contains:${label}" \
      "this scenario staged no '${kind}' file, so there is nothing in the VM to read"
    return 1
  fi
  pulled="${RESULTS_DIR}/readback-${kind}.json"
  # cat over ssh rather than scp: the failure to distinguish is an EMPTY file
  # (the app deleted or truncated it) from an absent one, and scp of a missing
  # path leaves the previous artifact in place looking like a successful read.
  if ! vm_ssh "test -f '${remote}' && cat '${remote}'" >"${pulled}" 2>/dev/null; then
    record_result FAIL "file_contains:${label}" \
      "no file at ${remote} inside the VM - it was staged there and is now gone"
    return 1
  fi
  set +e
  evidence="$(python3 "${FILE_VALUE}" "${pulled}" "${staged}" "${path}" "${expected}" 2>&1)"
  rc=$?
  set -e
  if [ "${rc}" -eq 0 ]; then
    record_result PASS "file_contains:${label}" "${kind} ${remote}: ${evidence}"
    return 0
  fi
  # rc 1 is the harness failing to do the comparison at all (unreadable JSON, no
  # staged copy) - not a statement about the app, so it must not be counted as
  # one.
  if [ "${rc}" -eq 1 ]; then
    record_result HARNESS "file_contains:${label}" \
      "could not grade ${kind} ${remote}: ${evidence}"
    return 1
  fi
  record_result FAIL "file_contains:${label}" "${kind} ${remote}: ${evidence}"
  return 1
}

# --- launch guard -------------------------------------------------
#
# A run that aborts can leave Atrium alive in the VM. SKIP_RESTORE=1 - the
# whole suite runs under it - means nothing resets that between scenarios.
# The next launch does not fail: Chromium's ProcessSingleton notifies the
# leftover process, the new launch exits, and every assertion after it grades
# whatever the OLD process shows. Measured 2026-08-10: a profile picker left
# over from an aborted run scored a scenario 1 of 4, and only a manual clean
# re-run showed the real result.
#
# Resolved from this file's own location rather than a caller-supplied var
# (see the header note above LAUNCH_OUTCOME): the guard has to work standalone,
# the same way grade_quit's QUIT_OUTCOME does once the runner defines it, but
# without requiring that wiring to exist yet.
ASSERTS_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LAUNCH_OUTCOME="${ASSERTS_LIB_DIR}/launch_outcome.py"

# require_no_existing_instance - refuse to start a scenario when Atrium
# is already running. Call this BEFORE staging fixtures or launching - staging
# for a launch that cannot use them (because ProcessSingleton will hand off to
# the leftover process instead) wastes the setup and hides the real problem
# behind assertions that measure the wrong process.
require_no_existing_instance() {
  local before evidence rc
  before="$(vm_ssh pgrep -x "${APP_PROCESS}" 2>/dev/null || true)"
  set +e
  evidence="$(python3 "${LAUNCH_OUTCOME}" "${before}" /dev/null 2>&1)"
  rc=$?
  set -e
  if [ "${rc}" -eq 0 ]; then
    record_result PASS "launch:preflight" "${evidence}"
    return 0
  fi
  record_result FAIL "launch:preflight" "${evidence}"
  return 1
}

# grade_launch_singleton <label> - a handoff through ProcessSingleton
# fails the run rather than grading the old process. Call this right after a
# launch attempt, once the run log has been pulled - reads the SAME log every
# vlog assertion reads, so a scenario's own assertions and this guard can never
# disagree about what the launch actually did.
grade_launch_singleton() {
  local label="${1:-launch:singleton}" evidence rc
  set +e
  evidence="$(python3 "${LAUNCH_OUTCOME}" "" "${RUN_LOG_LOCAL}" 2>&1)"
  rc=$?
  set -e
  if [ "${rc}" -eq 0 ]; then
    record_result PASS "${label}" "${evidence}"
    return 0
  fi
  record_result FAIL "${label}" "${evidence}"
  return 1
}

# grade_launch_dead <label> - a launch attempt that never produced
# a running Atrium process fails the scenario as a HARNESS error, not as a
# string of assertions read against untouched fixtures. Call this right after
# launch_atrium reports LAUNCH_FAILED (pgrep found no process a couple
# seconds after the launch command returned) and after the run log has been
# pulled, so the evidence names the real cause - the binary missing, or
# nothing written at all - instead of the assertions that would otherwise run
# next and report the fixtures as failures.
grade_launch_dead() {
  local label="${1:-launch}" evidence
  set +e
  evidence="$(python3 "${LAUNCH_OUTCOME}" dead "${RUN_LOG_LOCAL}" 2>&1)"
  set -e
  record_result HARNESS "${label}" "${evidence}"
}
