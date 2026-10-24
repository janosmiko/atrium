#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Runs every WebUI unit suite under coverage and enforces the line threshold.
#
#   make webui-coverage            gate at the default threshold
#   ATRIUM_COVERAGE_MIN=90 make webui-coverage
#
# Two things this guards that `node --test-coverage-lines` alone does not:
#
#   1. Coverage only counts files that were LOADED. A module no suite imports is
#      absent from the report entirely, so it cannot drag the average down - the
#      number goes UP by ignoring untested code. Every module in the resources
#      directory must therefore appear in the report or this fails.
#   2. The suite list is a glob, so a new build/scripts/*-test.mjs is gated the moment it
#      lands rather than when someone remembers to add it to the Makefile.

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="${ATRIUM_CHROMIUM_SRC:-${HOME}/chromium-atrium/src}"
MODULE_DIR="${SRC}/chrome/browser/resources/atrium"
MIN="${ATRIUM_COVERAGE_MIN:-80}"

cd "${REPO}"

shopt -s nullglob
suites=(build/scripts/*-test.mjs)
shopt -u nullglob
if [[ ${#suites[@]} -eq 0 ]]; then
  echo "no build/scripts/*-test.mjs suites found" >&2
  exit 2
fi

report="$(mktemp)"
trap 'rm -f "${report}"' EXIT

set +e
node --test --experimental-test-coverage \
  --test-coverage-include='**/resources/atrium/*.js' \
  --test-coverage-lines="${MIN}" \
  "${suites[@]}" 2>&1 | tee "${report}"
status="${PIPESTATUS[0]}"
set -e

# Every module must have been exercised by something. Without this a module that
# no suite imports is simply missing from the table above, which reads as a pass.
missing=()
for module in "${MODULE_DIR}"/*.js; do
  name="$(basename "${module}")"
  if ! grep -qF " ${name} " "${report}"; then
    missing+=("${name}")
  fi
done

if [[ ${#missing[@]} -gt 0 ]]; then
  echo
  echo "FAIL: ${#missing[@]} module(s) loaded by no suite, so absent from the coverage table:"
  printf '  %s\n' "${missing[@]}"
  echo "Import each from a suite - an unmeasured module silently raises the average."
  exit 1
fi

if [[ "${status}" -ne 0 ]]; then
  echo
  echo "FAIL: line coverage is below ${MIN}% (see the table above)."
  exit "${status}"
fi

echo
echo "ok: ${#suites[@]} suites, every module measured, line coverage >= ${MIN}%"
