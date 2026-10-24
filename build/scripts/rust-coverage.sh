#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# ATRIUM_RUST_COVERAGE_MIN overrides the default threshold, mirroring
# webui-coverage.sh. Only builds rust/atrium-core, never the Chromium
# mirror, so profraw/target output can't land there.

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CRATE="${REPO}/rust/atrium-core"
MIN="${ATRIUM_RUST_COVERAGE_MIN:-92}"

cd "${CRATE}"

report="$(mktemp)"
trap 'rm -f "${report}"' EXIT

set +e
cargo llvm-cov --branch --summary-only 2>&1 | tee "${report}"
status="${PIPESTATUS[0]}"
set -e

if [[ "${status}" -ne 0 ]]; then
  echo
  echo "FAIL: cargo llvm-cov exited non-zero (see the output above)."
  exit "${status}"
fi

line_pct="$(grep '^TOTAL' "${report}" | awk '{print $10}' | tr -d '%')"
if [[ -z "${line_pct}" ]]; then
  echo "FAIL: could not find a TOTAL line coverage figure in the report." >&2
  exit 2
fi

if awk -v got="${line_pct}" -v min="${MIN}" 'BEGIN { exit !(got < min) }'; then
  echo
  echo "FAIL: line coverage ${line_pct}% is below ${MIN}%."
  exit 1
fi

echo
echo "ok: line coverage ${line_pct}% >= ${MIN}%"
