#!/usr/bin/env bash
# build/scenarios/lib/keycombo_shell_test.sh - held-modifier tests.
#
# The risk in keycombo.sh is real bash, not the case-statement lookup table
# alone: send_held_taps must build the RIGHT cgevent argv from a scenario's
# modifier/key/taps/delay, and it must do so under `set -e` the same way
# vm-scenario.sh runs it, since a `local x; x="$(cmd)"` pattern with an
# unguarded nonzero exit inside is exactly the class of bug
# asserts_shell_test.sh was written to catch for grade_launch_dead. A
# pure-Python test cannot see either of those - only a real shell can.
#
# Run directly: bash build/scenarios/lib/keycombo_shell_test.sh

set -euo pipefail

LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Minimal stand-ins for the contract keycombo.sh expects from vm-scenario.sh.
APP_PROCESS="Atrium"
REMOTE_TOOLS="/remote/tools"
VM_SSH_CALLS=()
vm_ssh() {
  # Real vm_ssh forwards ALL args to ssh ("$@"), the way ssh itself takes a
  # remote command as several argv words - send_combo/send_held_taps call it
  # that way (vm_ssh "${REMOTE_TOOLS}/cgevent" heldtaps ...), so a stand-in
  # that only looked at $1 would silently drop everything the real assertion
  # below depends on.
  VM_SSH_CALLS+=("$*")
  # The one real command keycombo.sh reads output from: the frontmost-app
  # check. Answering with APP_PROCESS itself keeps every test's own intended
  # call (the cgevent invocation, always LAST) free of the frontmost warning
  # noise, the same way a real run looks once Atrium is actually frontmost.
  case "$1" in
    *"frontmost is true"*) printf '%s\n' "${APP_PROCESS}" ;;
    *) : ;;
  esac
}
die() { echo "DIE: $*" >&2; exit 1; }
log() { echo "$*" >>"${LOG_FILE}"; }

# shellcheck source=build/scenarios/lib/keycombo.sh
source "${LIB_DIR}/keycombo.sh"

fail=0
LOG_FILE="$(mktemp)"
INJECT_TMP="$(mktemp -d)"
trap 'rm -f "${LOG_FILE}"; rm -rf "${INJECT_TMP}"' EXIT

# --- send_held_taps builds the exact cgevent argv, called bare under set -e,
# the same way vm-scenario.sh's do_action calls it (not inside an if/&&,
# where set -e's exemption would hide an unguarded failure inside).
VM_SSH_CALLS=()
send_held_taps "ctrl" "tab" "3" "400"
last="${VM_SSH_CALLS[-1]}"
expected="${REMOTE_TOOLS}/cgevent heldtaps 'ctrl' 48 3 400"
if [ "${last}" != "${expected}" ]; then
  echo "FAIL: send_held_taps sent '${last}', expected '${expected}'" >&2
  fail=1
fi

# --- an unknown key dies rather than silently sending a garbage keycode.
VM_SSH_CALLS=()
set +e
( set -e; send_held_taps "ctrl" "nonexistent-key" "3" "400" ) 2>/tmp/keycombo-die.$$
rc=$?
set -e
if [ "${rc}" -eq 0 ]; then
  echo "FAIL: send_held_taps accepted an unknown key" >&2
  fail=1
elif ! grep -q "DIE: unknown key" /tmp/keycombo-die.$$; then
  echo "FAIL: send_held_taps failed for the wrong reason" >&2
  cat /tmp/keycombo-die.$$ >&2
  fail=1
fi
rm -f /tmp/keycombo-die.$$

# --- send_combo still works after the split out of vm-scenario.sh (regression guard for
# the extraction itself, not just the new function).
VM_SSH_CALLS=()
send_combo "cmd+t"
last="${VM_SSH_CALLS[-1]}"
expected="${REMOTE_TOOLS}/cgevent keypress 17 'cmd'"
if [ "${last}" != "${expected}" ]; then
  echo "FAIL: send_combo sent '${last}', expected '${expected}'" >&2
  fail=1
fi

# --- a non-frontmost app produces the warning line, for both send functions.
: >"${LOG_FILE}"
vm_ssh() { printf 'SomeOtherApp\n'; }
send_held_taps "ctrl" "tab" "2" "400" >/dev/null 2>&1 || true
if ! grep -q "WARNING: about to send ctrl+tab" "${LOG_FILE}"; then
  echo "FAIL: send_held_taps did not warn when another app was frontmost" >&2
  fail=1
fi

# --- a non-frontmost app is RAISED, not merely named. The chord
# that follows is worthless if it leaves for whatever is in front, and every
# scenario here targets Atrium, so there is nothing else it could be meant
# for. Asserts the raise goes out BEFORE the key.
: >"${LOG_FILE}"
RAISE_CALLS=()
vm_ssh() {
  RAISE_CALLS+=("$*")
  case "$1" in
    *"frontmost is true"*) printf 'SomeOtherApp\n' ;;
    *) : ;;
  esac
}
send_combo "cmd+s"
raised=0
keyed=0
for call in "${RAISE_CALLS[@]}"; do
  case "${call}" in
    *"set frontmost of first process whose name is \"Atrium\""*) raised=1 ;;
    *"cgevent keypress 1 'cmd'"*) [ "${raised}" -eq 1 ] && keyed=1 ;;
  esac
done
if [ "${raised}" -ne 1 ]; then
  echo "FAIL: send_combo did not raise the app when another was frontmost" >&2
  fail=1
elif [ "${keyed}" -ne 1 ]; then
  echo "FAIL: send_combo sent the key before raising the app" >&2
  fail=1
fi

# --- a cgevent that fails names itself. A swallowed failure would make a
# key that never left the harness look like one that landed.
: >"${LOG_FILE}"
vm_ssh() { case "$1" in *"frontmost is true"*) printf '%s\n' "${APP_PROCESS}" ;; *) return 3 ;; esac; }
send_combo "right"
if ! grep -q "KEY SEND FAILED: right (keycode 124) - cgevent exited 3" "${LOG_FILE}"; then
  echo "FAIL: send_combo swallowed a failed cgevent" >&2
  cat "${LOG_FILE}" >&2
  fail=1
fi


# a combo/modifier with shell metacharacters must reach cgevent
# as literal text, not run in the remote shell's re-parse of vm_ssh's
# joined argv - mirrors vm-scenario-quoting-test.sh's `sh -c "$*"` proof.
MARK_SEMI="${INJECT_TMP}/mark-semi"
MARK_BACKTICK="${INJECT_TMP}/mark-backtick"
MARK_DOLLAR="${INJECT_TMP}/mark-dollar"
# Both quotes close BEFORE the metacharacters (NASTY_TEXT's shape in
# vm-scenario-quoting-test.sh) - wrapping the metacharacters between two
# quotes instead self-quotes even with vm_q broken, proving nothing.
NASTY="it's a value'; touch ${MARK_SEMI}; \`touch ${MARK_BACKTICK}\`; true \$(touch ${MARK_DOLLAR})"

vm_ssh() {
  if [[ "$1" == *"frontmost is true"* ]]; then
    printf '%s\n' "${APP_PROCESS}"
  else
    sh -c "$*"
  fi
}

assert_no_injection() {  # <label> <command...>
  local label="$1"
  shift
  rm -f "${MARK_SEMI}" "${MARK_BACKTICK}" "${MARK_DOLLAR}"
  "$@" >/dev/null 2>&1 || true
  if [ -e "${MARK_SEMI}" ] || [ -e "${MARK_BACKTICK}" ] || [ -e "${MARK_DOLLAR}" ]; then
    echo "FAIL: ${label} let a scenario-authored value run shell commands" >&2
    fail=1
  else
    echo "OK: ${label} carried the quote/semicolon/backtick/\$() through as literal text"
  fi
}

assert_no_injection "send_combo modifier" send_combo "${NASTY}+t"
assert_no_injection "send_held_taps modifier" send_held_taps "${NASTY}" "t" "1" "1"

if [ "${fail}" -eq 0 ]; then
  echo "OK: keycombo.sh's send_held_taps/send_combo behave under a real shell"
fi
exit "${fail}"
