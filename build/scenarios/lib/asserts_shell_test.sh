#!/usr/bin/env bash
# build/scenarios/lib/asserts_shell_test.sh - shell-level regression test for grade_launch_dead.
#
# 142 Python unit tests covered launch_outcome.py's pure logic (its `dead`
# mode always returns exit 1 by design), but nothing exercised the shell
# wrapper that calls it. grade_launch_dead in asserts.sh called that command
# with no `set +e` guard, so under vm-scenario.sh's `set -euo pipefail` the
# always-1 exit code killed the function before record_result ever ran - the
# opposite of what the check is for. A pure-Python test cannot catch this;
# it only shows up when the function runs inside a real shell with `set -e`
# live, which is what this script reproduces.
#
# Run directly: bash build/scenarios/lib/asserts_shell_test.sh

set -euo pipefail

LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_DIR="$(mktemp -d)"
trap 'rm -rf "${RESULTS_DIR}"' EXIT

# Minimal stand-ins for the contract asserts.sh expects from vm-scenario.sh.
# Only HARNESS_ERRORS is read below; PASS_COUNT/FAIL_COUNT are part of the
# contract other assert_* helpers use but grade_launch_dead does not touch.
HARNESS_ERRORS=0
record_result() {
  local status="$1" name="$2"
  shift 2
  case "${status}" in
    HARNESS) HARNESS_ERRORS=$(( HARNESS_ERRORS + 1 )) ;;
  esac
  printf '[%s] %s :: %s\n' "${status}" "${name}" "$*" >>"${RESULTS_DIR}/results.log"
}
log() { :; }

# shellcheck source=build/scenarios/lib/asserts.sh
source "${LIB_DIR}/asserts.sh"

RUN_LOG_LOCAL="${RESULTS_DIR}/run.log"
: >"${RUN_LOG_LOCAL}"  # empty log: "never wrote a single line" branch

fail=0

# The real test: called as a BARE statement, exactly like vm-scenario.sh does
# right after LAUNCH_FAILED (not inside an if/&&/! condition, where set -e's
# exemption would hide the exact bug this guards against). If the python3
# call inside grade_launch_dead is unguarded, its always-1 exit kills this
# script right here under set -e, and REACHED_MARKER below is never written.
grade_launch_dead "launch" 2>"${RESULTS_DIR}/stderr.log"
: >"${RESULTS_DIR}/reached-marker"

if [ "${HARNESS_ERRORS}" -ne 1 ]; then
  echo "FAIL: expected exactly 1 HARNESS_ERRORS, got ${HARNESS_ERRORS}" >&2
  fail=1
fi

if ! grep -q '^\[HARNESS\] launch ::' "${RESULTS_DIR}/results.log" 2>/dev/null; then
  echo "FAIL: results.log has no HARNESS record for 'launch'" >&2
  cat "${RESULTS_DIR}/results.log" 2>/dev/null >&2 || echo "  (results.log missing)" >&2
  fail=1
fi

if [ "${fail}" -eq 0 ]; then
  echo "OK: grade_launch_dead records a HARNESS result under set -e"
fi

# --- a pattern with quotes must survive the trip to the VM -------
#
# _vlog_present and vlog_cite hand the pattern to `vm_ssh`, which sends it
# over ssh to be run by the VM's own shell. That second shell parse is the
# risky part - a Python unit test on remote_squote's escaping logic alone
# would never notice a mismatch with how vm_ssh actually joins its argument
# into a command line. So this fakes vm_ssh as a real, second shell
# invocation (`sh -c`), the same shape the real one puts the pattern through,
# and proves a pattern carrying a single quote, a double quote AND a
# backslash together survives it intact.
#
# The first fix for the single-quote case landed by DETECTING the quote and
# refusing to grep at all - that is why scrub-chord-agree's wait_vlog
# (pattern: "Atrium: active space 1 -> 2 'Third'") timed out on a build that
# was fine. This test is the regression guard for that refusal never coming
# back, and for any other quoting shape nobody thought to detect.
vm_ssh() {
  # Stand-in for the real vm_ssh (build/scripts/vm-scenario.sh): every call site here
  # builds ONE already-assembled command string and hands it as vm_ssh's sole
  # argument, exactly like the real one hands its sole argument to `ssh ...
  # "$@"` for the remote shell to parse. `sh -c "$1"` is that second parse.
  sh -c "$1"
}

# Single quote, double quote and a backslash together. The backslash escapes
# the period so the pattern is a valid ERE (an unescaped trailing backslash is
# undefined in POSIX ERE and GNU grep warns on it) - the point under test is
# whether the backslash CHARACTER survives the trip to the remote shell
# intact, not what grep's regex engine does with an unrelated construct.
NASTY_PATTERN="a'b\"c 3\\.14"
NASTY_LOG_LINE="a'b\"c 3.14"
NASTY_LOG="/tmp/atrium-run.log"
# _vlog_present and vlog_cite both hardcode this path (it is the live log
# still on the VM, not the pulled RUN_LOG_LOCAL copy) - so the test must
# target the same path to exercise the real functions rather than stand-ins.
had_log=0
[ -f "${NASTY_LOG}" ] && { had_log=1; cp "${NASTY_LOG}" "${RESULTS_DIR}/atrium-run.log.bak"; }
restore_nasty_log() {
  if [ "${had_log}" -eq 1 ]; then
    mv "${RESULTS_DIR}/atrium-run.log.bak" "${NASTY_LOG}"
  else
    rm -f "${NASTY_LOG}"
  fi
}
trap 'restore_nasty_log; rm -rf "${RESULTS_DIR}"' EXIT
printf '%s\n' "${NASTY_LOG_LINE}" >"${NASTY_LOG}"

if _vlog_present "${NASTY_PATTERN}"; then
  echo "OK: _vlog_present matched a pattern with a single quote, a double quote and a backslash"
else
  echo "FAIL: _vlog_present did not match /${NASTY_PATTERN}/ in ${NASTY_LOG}" >&2
  fail=1
fi

: >"${RESULTS_DIR}/results.log"
vlog_cite "${NASTY_PATTERN}" "nasty-quote-test" >/dev/null 2>&1 || true
if grep -qF "[INFO] vlog:nasty-quote-test :: VLOG: ${NASTY_LOG_LINE}" "${RESULTS_DIR}/results.log" 2>/dev/null; then
  echo "OK: vlog_cite cited the matched line unmangled"
else
  echo "FAIL: vlog_cite did not cite '${NASTY_LOG_LINE}' unmangled" >&2
  cat "${RESULTS_DIR}/results.log" 2>/dev/null >&2 || echo "  (results.log missing)" >&2
  fail=1
fi



# --- assert_ax_window_kind counts window LINES, not URLs ----------
#
# The counting is a grep over axresize's `kinds` output, and the shapes that
# can break it are all textual: a window holding two routes, a route named
# inside another window's page URL, a tool that answered nothing at all.
# Feeding those through the real function is the only way to see them; a
# scenario run costs six minutes and covers exactly one of them.
KINDS_FIXTURE=""
APP_PROCESS="Atrium"  # asserts.sh names the process in its HARNESS evidence
_ax_run() { printf '%s' "${KINDS_FIXTURE}"; }
vm_ssh() { printf ''; }  # pgrep stand-in for the HARNESS branch

kind_case() {  # <name> <expected-status> <pattern> <expected-count>
  local name="$1" want="$2"
  shift 2
  : >"${RESULTS_DIR}/results.log"
  assert_ax_window_kind "$@" >/dev/null 2>&1 || true
  if grep -q "^\[${want}\] ax_window_kind ::" "${RESULTS_DIR}/results.log" 2>/dev/null; then
    echo "OK: ${name}"
  else
    echo "FAIL: ${name} - wanted ${want}, got: $(cat "${RESULTS_DIR}/results.log")" >&2
    fail=1
  fi
}

KINDS_FIXTURE='window index=0 subrole=AXStandardWindow title="Untitled" urls=[chrome://atrium.top-chrome/foyer https://example.com/handed-over]
window index=1 subrole=AXStandardWindow title="Untitled" urls=[chrome://atrium.top-chrome/sidebar https://example.com/]
kinds=2 minArea=200000 total=2'
kind_case "one foyer window is found" PASS "/foyer" 1
kind_case "one sidebar window is found" PASS "/sidebar" 1
kind_case "a kind that is not there counts zero" PASS "/settings" 0
kind_case "a wrong expectation fails" FAIL "/foyer" 2

# Two space windows and no little one - the wrong-path shape the handover must not produce,
# and the one a window COUNT cannot see.
KINDS_FIXTURE='window index=0 subrole=AXStandardWindow title="Untitled" urls=[chrome://atrium.top-chrome/sidebar]
window index=1 subrole=AXStandardWindow title="Untitled" urls=[chrome://atrium.top-chrome/sidebar https://example.com/]
kinds=2 minArea=200000 total=2'
kind_case "no little window among two space windows fails a foyer gate" FAIL "/foyer" 1

# A page whose own URL contains the route name must not be read as chrome.
# The window is still counted once, not twice, because window LINES are what
# is counted.
KINDS_FIXTURE='window index=0 subrole=AXStandardWindow title="Untitled" urls=[chrome://atrium.top-chrome/foyer https://example.com/foyer]
kinds=1 minArea=200000 total=1'
kind_case "a window holding the route twice still counts once" PASS "/foyer" 1

# A tool that did not answer is a HARNESS failure. Reporting it as zero
# windows of that kind would be a claim about the app that nothing measured.
KINDS_FIXTURE=''
kind_case "an unanswered probe is HARNESS, not a product failure" HARNESS "/foyer" 1

# Malformed scenarios name themselves rather than reporting "expected= got=0".
KINDS_FIXTURE='kinds=0 minArea=200000 total=0'
kind_case "a missing pattern is refused" FAIL "" 1
kind_case "a missing expected is refused" FAIL "/foyer" ""

# --- assert_region_mean per-channel bounds ------------------------
# Over pure red the mean cannot separate a clean corner beside macOS 26's
# window rim (91.6) from the old dark wedge (88.5). A wedge drops R, a light
# fill lifts G/B, the rim does neither - only channel bounds can see that.
PIXCMP_STUB="${RESULTS_DIR}/pixcmp-stub.sh"
printf '#!/bin/sh\ncat "%s/pixcmp-stats.txt"\n' "${RESULTS_DIR}" >"${PIXCMP_STUB}"
chmod +x "${PIXCMP_STUB}"
HOST_PIXCMP="${PIXCMP_STUB}"
declare -A CAPTURE_LAST
: >"${RESULTS_DIR}/fake-frame.png"
CAPTURE_LAST[corner]="${RESULTS_DIR}/fake-frame.png"

mean_case() {  # <name> <expected-status> <stats-line> <args...>
  local name="$1" want="$2" stats="$3"
  shift 3
  printf '%s\n' "${stats}" >"${RESULTS_DIR}/pixcmp-stats.txt"
  : >"${RESULTS_DIR}/results.log"
  assert_region_mean corner 1 1 5 5 "$@" >/dev/null 2>&1 || true
  if grep -q "^\[${want}\] region_mean:" "${RESULTS_DIR}/results.log" 2>/dev/null; then
    echo "OK: ${name}"
  else
    echo "FAIL: ${name} - wanted ${want}, got: $(cat "${RESULTS_DIR}/results.log")" >&2
    fail=1
  fi
}

# args after the stats line: max_mean min_mean label min_r max_g max_b
mean_case "a clean corner beside the rim passes channel bounds" PASS \
  "mean=254.7,10.0,10.1 var=858.5 n=25" 95 0 region_mean 250 20 20
mean_case "a darkening wedge fails min_r though its mean is in range" FAIL \
  "mean=243.0,4.0,5.0 var=10.0 n=25" 95 0 region_mean 250 20 20
mean_case "an added light fill fails max_g/max_b though R is intact" FAIL \
  "mean=255.0,25.0,25.0 var=10.0 n=25" 256 0 region_mean 250 20 20
mean_case "channel bounds left unset keep the old mean-only contract" PASS \
  "mean=243.0,4.0,5.0 var=10.0 n=25" 95 0 region_mean

# max_r is what lets the dark quit-panel scenario prove its own appearance took
# effect. The three cases below are the three states its card patch can be in.
# args: max_mean min_mean label min_r max_g max_b max_r
mean_case "a dark brand card passes max_r" PASS \
  "mean=60.0,30.0,35.0 var=90.0 n=100" 150 0 region_mean "" "" "" 150
mean_case "a light card fails max_r, so a light run cannot pass as dark" FAIL \
  "mean=242.2,223.3,226.1 var=35.3 n=100" 256 0 region_mean "" "" "" 150
mean_case "an absent panel leaves pure red, which also fails max_r" FAIL \
  "mean=255.0,0.0,0.0 var=0.0 n=100" 256 0 region_mean "" "" "" 150

# --- region_every_frame: a flash in one frame fails the capture -------------
# The stub answers each frame with the stats file that sits beside it.
FRAME_STUB="${RESULTS_DIR}/pixcmp-frame-stub.sh"
# shellcheck disable=SC2016
printf '#!/bin/sh\ncat "$2.stats"\n' >"${FRAME_STUB}"
chmod +x "${FRAME_STUB}"
every_case() {  # <name> <expected-status> <stats-line>...
  local name="$1" want="$2" dir="${RESULTS_DIR}/every-$RANDOM" i=0 stats
  shift 2
  mkdir -p "${dir}"
  for stats in "$@"; do
    : >"${dir}/frame-00${i}.png"
    printf '%s\n' "${stats}" >"${dir}/frame-00${i}.png.stats"
    i=$((i + 1))
  done
  CAPTURE_LAST[paint]="${dir}/frame-00$((i - 1)).png"
  : >"${RESULTS_DIR}/results.log"
  HOST_PIXCMP="${FRAME_STUB}" assert_region_every_frame paint 1 1 5 5 120 15 >/dev/null 2>&1 || true
  if grep -q "^\[${want}\] region_every_frame:" "${RESULTS_DIR}/results.log" 2>/dev/null; then
    echo "OK: ${name}"
  else
    echo "FAIL: ${name} - wanted ${want}, got: $(cat "${RESULTS_DIR}/results.log")" >&2
    fail=1
  fi
}
every_case "a dark first paint that stays dark passes" PASS \
  "mean=83.0,86.0,87.0 var=0.0 n=25" "mean=30.0,30.0,30.0 var=0.0 n=25"
every_case "an opaque white first frame fails though the last frame is dark" FAIL \
  "mean=255.0,255.0,255.0 var=0.0 n=25" "mean=30.0,30.0,30.0 var=0.0 n=25"
every_case "a black frame in the middle fails" FAIL \
  "mean=30.0,30.0,30.0 var=0.0 n=25" "mean=0.0,0.0,0.0 var=0.0 n=25" \
  "mean=30.0,30.0,30.0 var=0.0 n=25"

# --- an assertion whose eval never read is HARNESS ----------------
# It reported INFO and the scenario summarised as passing, so a run that
# measured nothing looked exactly like one that measured everything.
declare -A EVAL_RESULT
EVAL_RESULT[saw-it]='"true"'
EVAL_RESULT[saw-other]='"false"'

eval_case() {  # <name> <expected-status> <fn> <args...>
  local name="$1" want="$2" fn="$3"
  shift 3
  : >"${RESULTS_DIR}/results.log"
  "${fn}" "$@" >/dev/null 2>&1 || true
  if grep -q "^\[${want}\] " "${RESULTS_DIR}/results.log" 2>/dev/null; then
    echo "OK: ${name}"
  else
    echo "FAIL: ${name} - wanted ${want}, got: $(cat "${RESULTS_DIR}/results.log")" >&2
    fail=1
  fi
}

eval_case "a matching read passes" PASS \
  assert_eval_equals saw-it '"true"'
eval_case "a mismatching read is a product failure" FAIL \
  assert_eval_equals saw-it '"false"'
eval_case "an id that never read is HARNESS, not a pass" HARNESS \
  assert_eval_equals never-looked '"true"'
eval_case "a changed pair passes" PASS \
  assert_eval_changed saw-it saw-other
eval_case "an unchanged pair is a product failure" FAIL \
  assert_eval_changed saw-it saw-it
eval_case "a blind half of a pair is HARNESS" HARNESS \
  assert_eval_changed saw-it never-looked

# The message has to name what WAS read, or a target typo looks like a defect.
: >"${RESULTS_DIR}/results.log"
assert_eval_equals never-looked '"true"' >/dev/null 2>&1 || true
if grep -q "ids that did read: .*saw-it" "${RESULTS_DIR}/results.log"; then
  echo "OK: the blind message lists the ids that did read"
else
  echo "FAIL: the blind message did not list the available ids" >&2
  fail=1
fi

# --- recency-walk verdicts. The log scan lives in the assert itself.
recency_case() {  # <name> <expected-status> <log-lines...>
  local name="$1" want="$2"
  shift 2
  printf '%s\n' "$@" >"${RUN_LOG_LOCAL}"
  : >"${RESULTS_DIR}/results.log"
  assert_recency_walk_distinct 3 "Atrium: recency cycle step direction 1 target ([0-9]+)" \
    >/dev/null 2>&1 || true
  if grep -q "^\[${want}\] recency_walk_distinct:" "${RESULTS_DIR}/results.log" 2>/dev/null; then
    echo "OK: ${name}"
  else
    echo "FAIL: ${name} - wanted ${want}, got: $(cat "${RESULTS_DIR}/results.log")" >&2
    fail=1
  fi
}

recency_case "three distinct targets pass" PASS \
  "Atrium: recency cycle step direction 1 target 3" \
  "Atrium: recency cycle step direction 1 target 2" \
  "Atrium: recency cycle step direction 1 target 1"
recency_case "a repeated target fails the oscillation of the old backreference bug" FAIL \
  "Atrium: recency cycle step direction 1 target 2" \
  "Atrium: recency cycle step direction 1 target 3" \
  "Atrium: recency cycle step direction 1 target 2"
recency_case "too few matches is unusable, not a fail" HARNESS \
  "Atrium: recency cycle step direction 1 target 3" \
  "Atrium: recency cycle step direction 1 target 2"

# --- assert_band_step over a REAL pixcmp -------------------------
# The scanner is the part that can be wrong, and a stub printing a canned line
# would test only the wrapper.
PIXCMP_BIN="${RESULTS_DIR}/pixcmp"
swiftc -O "${LIB_DIR}/../../vm-tools/pixcmp.swift" -o "${PIXCMP_BIN}" 2>/dev/null
HOST_PIXCMP="${PIXCMP_BIN}"
python3 - "${RESULTS_DIR}" <<'PY'
import struct, sys, zlib
out = sys.argv[1]
def png(name, pix):
    w = h = 40
    raw = b"".join(b"\x00" + b"".join(bytes(pix(x, y)) for x in range(w)) for y in range(h))
    def chunk(t, d):
        c = t + d
        return struct.pack(">I", len(d)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)
    open(f"{out}/{name}", "wb").write(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw))
        + chunk(b"IEND", b""))
# The reporter's two readings: a surround band of #AE6F75 against panel #BA3C6D.
png("band5.png", lambda x, y: (174, 111, 117) if x < 5 else (186, 60, 109))
png("band10.png", lambda x, y: (174, 111, 117) if x < 10 else (186, 60, 109))
png("bottom5.png", lambda x, y: (174, 111, 117) if y >= 35 else (186, 60, 109))
PY

step_case() {  # <name> <expected-status> <frame> <args...>
  local name="$1" want="$2" frame="$3"
  shift 3
  CAPTURE_LAST[band]="${RESULTS_DIR}/${frame}"
  : >"${RESULTS_DIR}/results.log"
  assert_band_step band "$@" >/dev/null 2>&1 || true
  if grep -q "^\[${want}\] band_step:" "${RESULTS_DIR}/results.log" 2>/dev/null; then
    echo "OK: ${name}"
  else
    echo "FAIL: ${name} - wanted ${want}, got: $(cat "${RESULTS_DIR}/results.log")" >&2
    fail=1
  fi
}

# args after the frame: x y len thick dir expected tol label
step_case "a 5-point band reads 5" PASS band5.png 0 20 16 8 right 5 1 five
step_case "the 10-point band the report describes fails the same gate" FAIL \
  band10.png 0 20 16 8 right 5 1 five
step_case "a scan that never leaves one colour fails rather than reads 0" FAIL \
  band5.png 8 20 8 8 right 5 1 five
step_case "a negative start scans up from the bottom edge" PASS \
  bottom5.png 20 -1 16 8 up 5 1 bottom
step_case "a missing frame is a fail, not a silent pass" FAIL \
  no-such-frame.png 0 20 16 8 right 5 1 five
step_case "expected -1 passes only where the strip holds one colour" PASS \
  band5.png 8 20 8 8 right -1 0 flat
step_case "expected -1 fails the moment a step appears in that strip" FAIL \
  band5.png 0 20 16 8 right -1 0 flat

exit "${fail}"
