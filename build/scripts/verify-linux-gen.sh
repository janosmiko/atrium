#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Standing gate for the Linux port.
#
# Two things are checked, in this order:
#
#   1. `gn gen` still runs for target_os = "linux".
#   2. Every Atrium source that reaches the Linux build could actually build
#      there. An Objective-C++ file in the Linux source list cannot, so it is
#      the case this gate exists to catch.
#
# Check 2 is the one that needs a gate. `gn gen` is happy to hand a .mm file to
# a Linux toolchain - GN does not compile anything - so a misplaced source sits
# in the target unnoticed until someone has a Linux builder, which is H3.7/H3.8
# and does not exist yet. Without this, the only thing standing between the
# is_mac arm and the portable arm of chrome/browser/ui/BUILD.gn is whoever last
# edited it.
#
# Extension is the signal. A .h in the list that happens to pull in AppKit is
# not caught here; the compiler catches those the day a Linux builder exists.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
# shellcheck disable=SC1091
source "${REPO}/build/scripts/env.sh"

OUT_DIR="${ATRIUM_LINUXGEN_DIR:-out/linuxgen}"
TARGET="//chrome/browser/ui:ui"

# Sources known to be in the wrong arm today, each with the reason, in the same
# posture as a VM scenario's `cannot_pass` field: the gate stays green on a
# defect that is already recorded, and goes red the moment a NEW one appears.
# A listed file that leaves the Linux list also fails the gate - that is the
# signal to delete its line here rather than let the mark rot.
#
# Format: one "<path>|<why>" per line. Empty when there is no known defect to
# excuse - the classifier below treats an empty list the same as any other.
KNOWN_NON_PORTABLE=""

# Classify a newline-separated source list. Prints findings, returns non-zero if
# any source is non-portable and unexpected, or any expected one has gone.
# Kept separate from the gn plumbing so --selftest can drive it directly.
classify_sources() {
  local sources="$1"
  local rc=0

  local offenders
  offenders="$(printf '%s\n' "${sources}" | grep -E '\.(mm|m|swift)$' || true)"

  local unexpected=""
  local line
  while IFS= read -r line; do
    [ -n "${line}" ] || continue
    if ! printf '%s\n' "${KNOWN_NON_PORTABLE}" | grep -qF "${line}|"; then
      unexpected="${unexpected}${line}"$'\n'
    fi
  done <<< "${offenders}"

  local known why stale=""
  while IFS= read -r known; do
    [ -n "${known}" ] || continue
    why="${known#*|}"
    known="${known%%|*}"
    if printf '%s\n' "${offenders}" | grep -qxF "${known}"; then
      echo "  EXPECTED FAILURE  ${known}"
      echo "                    ${why}"
    else
      stale="${stale}${known}"$'\n'
    fi
  done <<< "${KNOWN_NON_PORTABLE}"

  if [ -n "${unexpected}" ]; then
    echo "" >&2
    echo "FAIL: a source that cannot build on Linux is in the Linux source list:" >&2
    printf '%s' "${unexpected}" | sed 's/^/  /' >&2
    echo "" >&2
    echo "Objective-C and Swift do not compile for target_os=linux. Move it into" >&2
    echo "the is_mac arm of chrome/browser/ui/BUILD.gn, or make it portable." >&2
    rc=1
  fi

  if [ -n "${stale}" ]; then
    echo "" >&2
    echo "FAIL: a source marked non-portable is no longer in the Linux list:" >&2
    printf '%s' "${stale}" | sed 's/^/  /' >&2
    echo "" >&2
    echo "It was fixed. Delete its line from KNOWN_NON_PORTABLE in $0." >&2
    rc=1
  fi

  return "${rc}"
}

# Prove the classifier can fail. No gn, no checkout, no network. Uses a
# synthetic mark rather than a real KNOWN_NON_PORTABLE entry, so the selftest
# keeps exercising all three paths even when there is no live defect to excuse.
if [ "${1:-}" = "--selftest" ]; then
  KNOWN_NON_PORTABLE='//chrome/browser/ui/atrium/atrium_fake_non_portable.mm|synthetic entry for the selftest, not a real source'

  echo "selftest 1/3: a clean list passes (with the known mark present)"
  classify_sources "$(printf '%s\n' \
    '//chrome/browser/ui/atrium/atrium_capture.cc' \
    '//chrome/browser/ui/atrium/atrium_fake_non_portable.mm')" >/dev/null \
    || { echo "SELFTEST FAILED: a clean list did not pass" >&2; exit 1; }

  echo "selftest 2/3: an unmarked .mm fails"
  classify_sources "$(printf '%s\n' \
    '//chrome/browser/ui/atrium/atrium_capture.cc' \
    '//chrome/browser/ui/atrium/atrium_fake_non_portable.mm' \
    '//chrome/browser/ui/atrium/atrium_new_thing.mm')" >/dev/null 2>&1 \
    && { echo "SELFTEST FAILED: an unmarked .mm exited 0" >&2; exit 1; }

  echo "selftest 3/3: a mark with nothing behind it fails"
  classify_sources '//chrome/browser/ui/atrium/atrium_capture.cc' >/dev/null 2>&1 \
    && { echo "SELFTEST FAILED: a stale mark exited 0" >&2; exit 1; }

  echo "selftest OK: the gate passes clean lists and fails both dirty ones"
  exit 0
fi

cd "${SRC}"

# A missing sysroot stops gn gen in build/config/sysroot.gni long before it
# reaches an Atrium file, so that failure says nothing about the Atrium sources.
# Name it as a prerequisite rather than letting it read as a port regression.
if [ ! -d "${SRC}/build/linux/debian_bullseye_arm64-sysroot" ]; then
  echo "error: the Linux sysroot is missing, so gn gen cannot reach any Atrium file." >&2
  echo "       run: (cd ${SRC} && python3 build/linux/sysroot_scripts/install-sysroot.py --arch=arm64)" >&2
  exit 2
fi

mkdir -p "${OUT_DIR}"
printf 'target_os = "linux"\nis_debug = false\nis_component_build = true\n' > "${OUT_DIR}/args.gn"

echo "gn gen ${OUT_DIR} (target_os=linux)"
gen_out="$(gn gen "${OUT_DIR}")"
printf '%s\n' "${gen_out}" | sed 's/^/  /'

targets="$(printf '%s\n' "${gen_out}" | sed -n 's/^Done\. Made \([0-9]*\) targets.*/\1/p')"
if [ -z "${targets}" ]; then
  echo "error: gn gen did not report a target count; treating that as a failure" >&2
  exit 1
fi

sources="$(gn desc "${OUT_DIR}" "${TARGET}" sources | grep 'atrium/' || true)"
count="$(printf '%s' "${sources}" | grep -c . || true)"

echo "targets: ${targets}"
echo "atrium sources in ${TARGET}: ${count}"

# Zero means the query stopped matching - a renamed directory, a target that no
# longer exists, or the whole atrium block back inside is_mac. Any of those make
# the check below vacuous, so they are failures, not a pass.
if [ "${count}" -eq 0 ]; then
  echo "" >&2
  echo "FAIL: no atrium sources in ${TARGET} for target_os=linux." >&2
  echo "Either they moved back into the is_mac arm or this query stopped matching." >&2
  echo "A gate that checks nothing is not a pass." >&2
  exit 1
fi

classify_sources "${sources}"

marked="$(printf '%s\n' "${KNOWN_NON_PORTABLE}" | grep -c . || true)"
echo "verify OK: gn gen made ${targets} targets; ${count} atrium sources for Linux," \
     "${marked} of them marked non-portable and no new ones"
