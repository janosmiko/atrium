#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# build/scripts/vm-scenario-quoting-test.sh - proves vm_q/vm_num.

set -euo pipefail

TEST_TMP="$(mktemp -d)"
trap 'rm -rf "${TEST_TMP}"' EXIT

# A fake `open` ahead of PATH keeps open_url's `open -a` call off the host.
STUB_BIN="${TEST_TMP}/bin"
mkdir -p "${STUB_BIN}"
printf '#!/bin/sh\nexit 0\n' >"${STUB_BIN}/open"
chmod +x "${STUB_BIN}/open"

# shellcheck source=build/scripts/vm-scenario.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/vm-scenario.sh"

fail=0

# do_action's real vm_ssh joins argv the way ssh hands the VM's login shell a
# single string - this stand-in does the same join with `sh -c "$*"`, so an
# unquoted call site fails here too, not only against the real VM.
vm_ssh() { PATH="${STUB_BIN}:${PATH}" sh -c "$*"; }

# Quote count kept even in each payload - an odd one just fails to parse
# rather than proving injected commands can run.
MARK_SEMI="${TEST_TMP}/mark-semi"
MARK_BACKTICK="${TEST_TMP}/mark-backtick"
MARK_DOLLAR="${TEST_TMP}/mark-dollar"
NASTY_URL="x'; touch ${MARK_SEMI}; \`touch ${MARK_BACKTICK}\`; true \$(touch ${MARK_DOLLAR}); echo '"
NASTY_TEXT="it's a value'; touch ${MARK_SEMI}; \`touch ${MARK_BACKTICK}\`; true \$(touch ${MARK_DOLLAR})"

assert_no_injection() {  # <action-record> <label>
  local record="$1" label="$2"
  rm -f "${MARK_SEMI}" "${MARK_BACKTICK}" "${MARK_DOLLAR}"
  do_action "${record}" >/dev/null 2>&1 || true
  if [ -e "${MARK_SEMI}" ] || [ -e "${MARK_BACKTICK}" ] || [ -e "${MARK_DOLLAR}" ]; then
    echo "FAIL: ${label} let a scenario-authored value run shell commands" >&2
    fail=1
  else
    echo "OK: ${label} carried the quote/semicolon/backtick/\$() through as literal text"
  fi
}

assert_no_injection "$(printf 'open_url\turl=%s' "${NASTY_URL}")" "open_url url"
assert_no_injection "$(printf 'type\ttext=%s' "${NASTY_TEXT}")" "type text"

# vm_num must refuse a non-numeric field rather than pass it to the shell.
if (out="$(vm_num "12x" x 2>&1)"); then
  echo "FAIL: vm_num accepted '12x' as a number: ${out}" >&2
  fail=1
else
  echo "OK: vm_num refused a non-numeric field"
fi

if vm_num "-42" dx >/dev/null 2>&1; then
  echo "OK: vm_num accepts a signed integer"
else
  echo "FAIL: vm_num refused a valid signed integer" >&2
  fail=1
fi

exit "${fail}"
