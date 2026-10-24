#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# The atrium C++ gtests. They live inside Chromium's giant unit_tests target,
# so this builds that target and then runs only Atrium's own cases.
#
# The filter matters: unit_tests holds tens of thousands of Chromium cases and
# running all of them here would say nothing about this fork while taking a
# very long time. Pass a narrower one to focus, for example
#   ./build/scripts/cpp-test.sh AtriumIconPath*
set -euo pipefail
REPO_BUILD="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$REPO_BUILD/env.sh"

# a hand-maintained prefix list (Atrium*, SlotPresentationTable*, ...)
# has the same hole every time a suite gets named outside it - it happened to
# SidebarSnapshotGateTest, silently, and the run stayed green. So the default
# gate no longer trusts a prefix list: it scopes "ours" to every suite defined
# under Atrium's own unittest files, then asks gtest itself - the honest
# source of truth for what the binary actually compiled - whether each one
# got selected. A suite that exists but the filter missed fails the gate by
# name, not by silence.
ATRIUM_TEST_DIR="chrome/browser/ui/atrium"

cd "$SRC"
ncpu="$(sysctl -n hw.ncpu 2>/dev/null || echo 8)"
build_jobs="${ATRIUM_BUILD_JOBS:-$(( ncpu > 4 ? ncpu - 4 : 1 ))}"
caffeinate -i autoninja -C out/atrium unit_tests -j "$build_jobs"

BIN="out/atrium/unit_tests"

# Every suite name compiled into the binary, per gtest itself.
list_suites() {
  "$BIN" --gtest_filter="${1:-*}" --gtest_list_tests 2>/dev/null \
    | grep -E '^[A-Za-z_]' | sed 's/\.$//' | sort -u
}

compiled_suites="$(list_suites '*')"

# Suites that belong to Atrium: anything TEST/TEST_F/TEST_P defines in
# Atrium's own unittest sources, independent of what it happens to be named.
# Cross-checked against compiled_suites so a macro this grep misparses can't
# silently widen or shrink the set.
atrium_suites="$(grep -hoE '^(TEST|TEST_F|TEST_P)\([A-Za-z0-9_]+' \
    "$ATRIUM_TEST_DIR"/*_unittest.cc \
  | sed -E 's/^(TEST|TEST_F|TEST_P)\(//' | sort -u)"
atrium_suites="$(comm -12 <(echo "$atrium_suites") <(echo "$compiled_suites"))"

if [ -n "${1:-}" ]; then
  # An explicit filter means the caller is deliberately narrowing focus -
  # skip the completeness check, it isn't meant to cover everything.
  FILTER="$1"
else
  # Default gate: every suite Atrium owns, generated fresh each run so there
  # is no list to fall out of date. gtest_filter matches the full
  # "Suite.Case" string, so each suite name needs its own trailing wildcard -
  # a bare name matches nothing and silently selects zero tests.
  FILTER="$(echo "$atrium_suites" | sed 's/$/.*/' | paste -sd: -)"

  selected_suites="$(list_suites "$FILTER")"
  missed="$(comm -23 <(echo "$atrium_suites") <(echo "$selected_suites"))"
  if [ -n "$missed" ]; then
    echo "cpp-test: these Atrium suites exist but the filter selected none of their cases - the gate would go green without running them:" >&2
    echo "$missed" | sed 's/^/  /' >&2
    exit 1
  fi
fi

"$BIN" --gtest_filter="$FILTER"
