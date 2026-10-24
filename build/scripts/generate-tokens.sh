#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Regenerates build/generated/tokens.css.
# Generated, never hand-edited: the drift guards fail when the checked-in file
# and the generator disagree.
#
# the RUST generator writes it. The Swift one goes with the cut,
# and this is the regeneration half. Verification survives the cut separately. Both emit the same bytes today, and
# tokens_reference.rs is what holds the Rust one to the committed file.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${REPO}/build/generated/tokens.css"
mkdir -p "$(dirname "${OUT}")"
cargo run -q --manifest-path "${REPO}/rust/atrium-core/Cargo.toml" \
  --bin atrium-tokens > "${OUT}"
echo "wrote ${OUT} ($(wc -l < "${OUT}") lines)"

# The WebUI routes serve their own copy from the Chromium tree (they cannot
# reach outside it), so it is written here rather than left to drift. Skipped
# when the checkout is absent - a tokens regen must not require it.
SRC="${ATRIUM_CHROMIUM_SRC:-${HOME}/chromium-atrium/src}"
WEBUI="${SRC}/chrome/browser/resources/atrium/tokens.css"
if [ -d "$(dirname "${WEBUI}")" ]; then
  cp "${OUT}" "${WEBUI}"
  echo "wrote ${WEBUI}"
else
  echo "note: ${SRC} not present; skipped the WebUI copy" >&2
fi
