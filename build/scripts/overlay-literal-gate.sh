#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Every overlay sheet reads its colors, radii and font from overlay.css. A
# literal in a consumer sheet is the drift this layer exists to stop.
set -euo pipefail
SRC="${ATRIUM_CHROMIUM_SRC:-$HOME/chromium-atrium/src}"
DIR="$SRC/chrome/browser/resources/atrium"
FILES=(commandbar.css toast-stack.css permsheet.css hint.css peek-controls.css courtyard.css find.css quickmenu.css)

# font-size/font-weight never match: the property must be "font" or
# "font-family" immediately before the colon.
font_violations() {
  grep -nE '\bfont(-family)?[[:space:]]*:' "$1" 2>/dev/null \
    | grep -vE 'var\(--ov-font' \
    | grep -vE ':[[:space:]]*(inherit|initial|unset|revert)[[:space:]]*;' || true
}

# A ring layered on var(--ov-shadow) still reads its elevation from the
# token, so only a value with no --ov-* reference at all is a violation.
shadow_violations() {
  grep -nE 'box-shadow[[:space:]]*:[^;]*[0-9]' "$1" 2>/dev/null | grep -v 'var(--ov-' || true
}

status=0
for f in "${FILES[@]}"; do
  if [[ ! -f "$DIR/$f" ]]; then
    echo "missing: $f"
    status=1
    continue
  fi
  if grep -nE 'rgba?\(|#[0-9a-fA-F]{3,8}\b' "$DIR/$f"; then
    echo "literal color in $f"
    status=1
  fi
  if grep -nE 'border-radius[[:space:]]*:[^;]*[0-9]+px' "$DIR/$f"; then
    echo "literal border-radius in $f"
    status=1
  fi
  font_hits="$(font_violations "$DIR/$f")"
  if [[ -n "$font_hits" ]]; then
    echo "$font_hits"
    echo "literal font-family in $f"
    status=1
  fi
  shadow_hits="$(shadow_violations "$DIR/$f")"
  if [[ -n "$shadow_hits" ]]; then
    echo "$shadow_hits"
    echo "literal box-shadow geometry in $f"
    status=1
  fi
done
if [[ ! -f "$DIR/overlay.css" ]]; then
  echo "missing: overlay.css"
  status=1
fi

# sidebar.css is shared with rules this gate has no business judging (other
# surfaces still carry their own literals), so only the row-menu block -
# .ctxmenu through .ctxrule's closing brace - joins the check.
MENU_CSS="$DIR/sidebar.css"
if [[ -f "$MENU_CSS" ]]; then
  menu_block="$(awk '
    /\.ctxmenu[[:space:]]*\{/ { grab = 1 }
    grab { print }
    grab && /\.ctxrule[[:space:]]*\{/ { in_rule = 1 }
    grab && in_rule && /^}/ { exit }
  ' "$MENU_CSS")"
  if [[ -z "$menu_block" ]]; then
    echo "sidebar.css menu block moved or was renamed"
    status=1
  elif grep -nE 'rgba?\(|#[0-9a-fA-F]{3,8}\b' <<< "$menu_block" > /dev/null; then
    echo "literal color in sidebar.css menu block"
    status=1
  elif grep -nE 'border-radius[[:space:]]*:[^;]*[0-9]+px' <<< "$menu_block" > /dev/null; then
    echo "literal border-radius in sidebar.css menu block"
    status=1
  elif [[ -n "$(font_violations <(echo "$menu_block"))" ]]; then
    echo "literal font-family in sidebar.css menu block"
    status=1
  elif [[ -n "$(shadow_violations <(echo "$menu_block"))" ]]; then
    echo "literal box-shadow geometry in sidebar.css menu block"
    status=1
  fi
else
  echo "missing: sidebar.css"
  status=1
fi
exit $status
