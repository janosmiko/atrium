#!/usr/bin/env bash
# build/scenarios/lib/keycombo.sh - key/combo -> cgevent argument translation
# for the VM scenario runner (split out of vm-scenario.sh). Sourced by
# vm-scenario.sh; not executable alone.
#
# This shell logic has its own test, like asserts.sh
# (asserts_shell_test.sh) - a pure-Python test cannot exercise a bash `case`
# statement or `set -e` interaction, only a real shell can.
#
# Contract - the runner must define before sourcing:
#   vm_ssh <cmd...>     run a command inside the probe VM, stdout returned
#   die <msg>           abort the scenario (used when a combo/key is unknown)
#   log <msg>           write one line to the run's own log
#   APP_PROCESS         app process name (e.g. Atrium)
#   REMOTE_TOOLS        dir inside the VM holding compiled probe binaries

# combo/modifier text reaching vm_ssh's argv needs the same
# quoting vm-scenario.sh's own fields get, so this pulls the shared helpers
# by its own path rather than trusting the runner to have sourced them.
_KEYCOMBO_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=build/scenarios/lib/quoting.sh
source "${_KEYCOMBO_LIB_DIR}/quoting.sh"

# --- combo -> cgevent keycode+modifier -------------------------------------
# Only the keys scenarios actually use. cgevent takes a keycode + optional
# '+'-joined modifier string (cmd|shift|ctrl|opt).
keycode_for() {
  case "$1" in
    1) echo 18 ;; 2) echo 19 ;; 3) echo 20 ;; 4) echo 21 ;; 5) echo 23 ;;
    6) echo 22 ;; 7) echo 26 ;; 8) echo 28 ;; 9) echo 25 ;; 0) echo 29 ;;
    q) echo 12 ;; w) echo 13 ;; s) echo 1 ;; t) echo 17 ;;
    i) echo 34 ;; n) echo 45 ;; r) echo 15 ;;
    # the letters the edit-focus gate's chord classification cares
    # about (cmd+a/c/v/x/z and the ctrl emacs set), plus l for cmd+l - so a
    # scenario can drive both a GATED and an ungated chord.
    a) echo 0 ;; b) echo 11 ;; c) echo 8 ;; d) echo 2 ;; e) echo 14 ;;
    f) echo 3 ;; h) echo 4 ;; j) echo 38 ;; k) echo 40 ;; l) echo 37 ;;
    # cmd+g and cmd+shift+g walk the find matches, and no scenario could press
    # either until this entry existed.
    g) echo 5 ;;
    o) echo 31 ;; p) echo 35 ;; v) echo 9 ;; x) echo 7 ;; y) echo 16 ;;
    z) echo 6 ;;
    tab) echo 48 ;; return|enter) echo 36 ;; esc|escape) echo 53 ;;
    left) echo 123 ;; right) echo 124 ;; down) echo 125 ;; up) echo 126 ;;
    # cmd+comma is the standard macOS Settings shortcut, and the only way a
    # scenario can open the settings window - its menu item is not reachable
    # through the click/key actions.
    comma) echo 43 ;;
    # nextTab and previousTab are bound to the brackets, so a scenario cannot
    # walk tabs without them.
    rbracket|\]) echo 30 ;; lbracket|\[) echo 33 ;;
    # Page zoom in and out.
    equal) echo 24 ;; minus) echo 27 ;;
    *) return 1 ;;
  esac
}

# _ensure_frontmost <what-is-about-to-be-sent> : shared by send_combo and
# send_held_taps so both give the same warning.
#
# cgevent posts to .cghidEventTap, which delivers to the FRONTMOST
# application. Input sent while something else is in front is not delayed or
# ignored, it goes to that app, and nothing here noticed - the loss surfaced
# later as whichever assertion depended on the input, which reads as the
# feature being broken. Measured on settings-tab-row across eight runs: four
# failures, and on every one the app's own keyDown monitor logged nothing.
#
# Editing anything under build/vm-tools breaks stage_tools' byte-identical
# comparison permanently, so every later run recompiles that tool and the
# fresh binary loses the TCC grants the clean snapshot earned. For cgevent
# that would silently stop synthetic input altogether - the same symptom this
# is trying to explain. System Events is already how `activate` works, so
# this adds no new permission surface.
#
# it now RAISES the app rather than only naming the loss. Every one
# of the 133 scenarios targets Atrium, so there is no case here where the key
# was meant for whatever happened to be in front. The `activate` action a
# scenario runs once at the top does not hold: a notification banner, a
# Finder window or the app's own quit dialog can take the front at any later
# point, and the next chord then leaves for that app. A panel scenario
# lost its collapse this way - the whole run measured a docked
# pane, and the log carried exactly one command, the Cmd+Q at the end.
#
# The warning stays for the case the raise cannot fix, so a key that still
# leaves for another app names itself.
_ensure_frontmost() {
  local what="$1"
  local frontmost
  frontmost="$(_frontmost_process)"
  if [ -z "${frontmost}" ] || [ "${frontmost}" = "${APP_PROCESS}" ]; then
    return 0
  fi
  log "  '${frontmost}' is frontmost, not '${APP_PROCESS}' - raising it before ${what}"
  vm_ssh "osascript -e 'tell application \"System Events\" to set frontmost of first process whose name is \"${APP_PROCESS}\" to true'" >/dev/null 2>&1 || true
  sleep 0.4
  frontmost="$(_frontmost_process)"
  if [ -n "${frontmost}" ] && [ "${frontmost}" != "${APP_PROCESS}" ]; then
    log "  WARNING: about to send ${what} but '${frontmost}' is frontmost, not '${APP_PROCESS}' - it will go to that app"
  fi
}

_frontmost_process() {
  vm_ssh "osascript -e 'tell application \"System Events\" to name of first application process whose frontmost is true'" 2>/dev/null | tr -d '\r'
}

# send_combo <combo>  e.g. "cmd+2", "cmd+opt+right", "cmd+q"
send_combo() {
  local combo="$1"
  local -a parts
  IFS='+' read -ra parts <<<"${combo}"
  local key="${parts[-1]}"
  local mods=("${parts[@]:0:${#parts[@]}-1}")
  local keycode
  keycode="$(keycode_for "${key}")" || die "unknown key in combo '${combo}'"
  # name the app that is about to receive this key.
  _ensure_frontmost "${combo}"
  local mod_str=""
  [ "${#mods[@]}" -gt 0 ] && mod_str="$(IFS='+'; echo "${mods[*]}")"
  _post_key "${combo}" "${keycode}" "${mod_str}"
}

# _post_key <what> <keycode> [modifier] : the one place a key
# actually leaves the harness.
#
# A caller must not end in `>/dev/null 2>&1 || true`: a cgevent that failed -
# missing binary, dropped ssh channel, a post refused for want of a TCC grant -
# would look like one that worked, and the run would say nothing about the key
# that never arrived. So the status and any stderr are reported.
#
# Still non-fatal: this is an instrument, and a scenario that dies on the first
# hiccup measures less than one that says what happened and carries on.
_post_key() {
  local what="$1" keycode="$2" mod_str="$3"
  local status mod_q=""
  # mod_str is scenario-authored and ssh re-joins vm_ssh's argv
  # for the remote shell to parse, so it needs vm_q, not just local quoting.
  [ -n "${mod_str}" ] && mod_q="$(vm_q "${mod_str}")"
  # Called bare, not in a command substitution: a subshell would swallow the
  # very reporting this exists for, and vm_ssh's argv must stay word-for-word
  # what it was.
  set +e
  vm_ssh "${REMOTE_TOOLS}/cgevent" keypress "${keycode}" ${mod_q:+"${mod_q}"} >/dev/null 2>&1
  status=$?
  set -e
  if [ "${status}" -ne 0 ]; then
    log "  KEY SEND FAILED: ${what} (keycode ${keycode}${mod_str:+ +${mod_str}}) - cgevent exited ${status}"
  fi
}

# send_held_taps <modifier> <key> <taps> <tap_delay_ms>
#
# press <modifier> ONCE, tap <key> <taps> times under it, release
# <modifier> ONCE - one cgevent process, one `heldTaps` call, so the modifier
# is never released between taps the way chaining several `key` actions
# (each its own send_combo, each its own withModifier press/release) would.
# See cgevent.swift's heldTaps doc for why that gap mattered: Ctrl-Tab
# recency cycling is meant to freeze its candidate list while Control stays
# down and commit only when it comes up.
send_held_taps() {
  local modifier="$1" key="$2" taps="$3" tap_delay_ms="$4"
  local keycode
  keycode="$(keycode_for "${key}")" || die "unknown key '${key}' in key_hold_taps"
  # modifier is scenario text reaching vm_ssh's argv, and taps/
  # tap_delay_ms are interpolated unquoted below, so both need the same
  # guard vm-scenario.sh's own numeric fields get.
  vm_num "${taps}" taps
  vm_num "${tap_delay_ms}" tap_delay_ms
  local mod_q; mod_q="$(vm_q "${modifier}")"
  _ensure_frontmost "${modifier}+${key} x${taps} (held)"
  # Same reporting as _post_key: a held-tap burst that never left the
  # harness must not pass silently.
  local status
  set +e
  vm_ssh "${REMOTE_TOOLS}/cgevent" heldtaps "${mod_q}" "${keycode}" "${taps}" "${tap_delay_ms}" >/dev/null 2>&1
  status=$?
  set -e
  if [ "${status}" -ne 0 ]; then
    log "  KEY SEND FAILED: ${modifier}+${key} x${taps} (held) - cgevent exited ${status}"
  fi
}
