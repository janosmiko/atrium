#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Asserts the BUILT bundle carries Atrium's own app.icns.
# A rebase dropping chrome/BUILD.gn's copy targets restores the stock icon
# silently: the only other source is AtriumApplyAppIcon, a runtime override
# that the Auto choice clears on purpose.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
# shellcheck disable=SC1091
source "$REPO/build/scripts/env.sh"

# --invert exists so the selftest can prove this guard is able to fail.
INVERT=0
if [[ "${1:-}" == "--invert" ]]; then
  INVERT=1
fi

BUNDLED="$CHROMIUM_DIR/src/out/atrium/Atrium.app/Contents/Resources/app.icns"
STOCK="$CHROMIUM_DIR/src/chrome/app/theme/chromium/mac/app.icns"

for f in "$BUNDLED" "$STOCK"; do
  if [[ ! -f "$f" ]]; then
    echo "FAIL: no file at $f. Run make build first." >&2
    exit 1
  fi
done

bundled_sum="$(md5 -q "$BUNDLED")"
stock_sum="$(md5 -q "$STOCK")"
echo "bundled app.icns: $bundled_sum ($(wc -c <"$BUNDLED" | tr -d ' ') bytes)"
echo "stock   app.icns: $stock_sum ($(wc -c <"$STOCK" | tr -d ' ') bytes)"

same=0
if [[ "$bundled_sum" == "$stock_sum" ]]; then
  same=1
fi
if [[ $INVERT -eq 1 ]]; then
  same=$((1 - same))
fi

if [[ $same -eq 1 ]]; then
  echo "FAIL: the built bundle's app.icns is byte-identical to the stock" \
       "Chromium one, so the Dock shows the Chromium icon until the runtime" \
       "override runs - and forever on the Auto choice." >&2
  exit 1
fi

# Editing upstream's file in place would satisfy the comparison above while
# putting 84KB of binary into a patch series exported as text.
if ! git -C "$CHROMIUM_DIR/src" diff --quiet -- \
    chrome/app/theme/chromium/mac/app.icns; then
  echo "FAIL: chrome/app/theme/chromium/mac/app.icns is modified. The Atrium" \
       "art belongs in the bundle through BUILD.gn's copy targets, not in" \
       "upstream's file." >&2
  exit 1
fi
echo "stock app.icns is unmodified in the checkout"

echo "verify OK: the built bundle carries Atrium's own app.icns"
