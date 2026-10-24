#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Keeps the three copies of the design tokens together.
#
#   build/generated/tokens.css                        the reference
#   $SRC/chrome/browser/resources/atrium/tokens.css   what the WebUI serves
#   the Swift constants                               where the numbers come from
#
# The reference-versus-WebUI check runs FIRST and needs no Swift. That ordering
# is the point: the Swift generator is being deleted, and when it
# goes this guard has to keep working on the reference file rather than quietly
# passing on a stale one.
#
# The Swift check runs after, while there is still Swift to run. The Swift
# suite asserts the same thing (TokenExportTests); this runs from build-ui.sh
# too, so a stale stylesheet fails the dylib build before anything renders
# against the wrong numbers.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
COMMITTED="${REPO}/build/generated/tokens.css"

if [ ! -f "${COMMITTED}" ]; then
  echo "error: ${COMMITTED} missing; run: make tokens" >&2
  exit 1
fi

# 1. The WebUI copy. The routes serve a copy inside the Chromium tree because
# they cannot read a file outside it, and a copy is a second source of truth
# unless something asserts it. Same posture as verify-abi.sh.
SRC="${ATRIUM_CHROMIUM_SRC:-${HOME}/chromium-atrium/src}"
WEBUI="${SRC}/chrome/browser/resources/atrium/tokens.css"
webui_checked=0
if [ -f "${WEBUI}" ]; then
  if ! diff -q "${COMMITTED}" "${WEBUI}" >/dev/null; then
    echo "error: the WebUI copy of tokens.css does not match the reference;" \
         "run: make tokens" >&2
    diff -u "${COMMITTED}" "${WEBUI}" >&2 || true
    exit 1
  fi
  webui_checked=1
fi

# 2. The Swift generator, while it exists. After the cut this branch is simply
# not taken, and the check above is still a real check.
GENERATOR="${REPO}/swift/AtriumUI/Sources/AtriumUI/TokenExport.swift"
if [ -f "${GENERATOR}" ]; then
  TMP="$(mktemp -t atrium-tokens.XXXXXX)"
  trap 'rm -f "${TMP}"' EXIT
  swift run --package-path "${REPO}/swift/AtriumUI" -c release atrium-tokens > "${TMP}"

  if ! diff -q "${COMMITTED}" "${TMP}" >/dev/null; then
    echo "error: build/generated/tokens.css is stale; run: make tokens" >&2
    diff -u "${COMMITTED}" "${TMP}" >&2 || true
    exit 1
  fi

  if [ "${webui_checked}" = 1 ]; then
    echo "verify OK: tokens.css matches the Swift constants, and the WebUI copy matches it"
  else
    echo "verify OK: tokens.css matches the Swift constants (no Chromium checkout to check)"
  fi
  exit 0
fi

if [ "${webui_checked}" = 1 ]; then
  echo "verify OK: the WebUI copy of tokens.css matches the reference" \
       "(no Swift generator - see build/generated/README.md for where the numbers came from)"
else
  echo "warning: nothing to check - no Swift generator and no Chromium checkout" >&2
fi
