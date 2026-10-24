#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
set -euo pipefail
cd "$(dirname "$0")"
# shellcheck disable=SC1091
source ./env.sh

command -v xcodebuild >/dev/null || { echo "ERROR: Xcode required"; exit 1; }

mkdir -p "$CHROMIUM_DIR"

# Portable free-space check (works with both BSD and GNU df),
# measured on the volume that will actually hold the checkout.
avail_gb=$(df -Pk "$CHROMIUM_DIR" | awk 'NR==2 {print int($4/1048576)}')
[ "${avail_gb}" -ge 250 ] || { echo "ERROR: need >=250GB free at $CHROMIUM_DIR, have ${avail_gb}GB"; exit 1; }
cd "$CHROMIUM_DIR"

if [ ! -d depot_tools ]; then
  git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
fi

if [ ! -d src ]; then
  caffeinate -i fetch --nohooks chromium
fi

cd src
caffeinate -i gclient runhooks
echo "bootstrap: OK"
