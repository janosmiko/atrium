#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
set -euo pipefail
REPO_BUILD="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$REPO_BUILD/env.sh"

# GN links third_party/atrium/core, so an edit that never reached that copy
# ships the old core while rust-test stays green.
if ! "$REPO_BUILD/verify-rust.sh"; then
  echo "build-shell: the atrium-core copies differ - run \`make rust-sync\`" >&2
  exit 1
fi

if ! "$REPO_BUILD/overlay-verify.sh"; then
  echo "build-shell: overlay/ and the Chromium tree differ - run \`make overlay-sync\`" >&2
  exit 1
fi

# Ensure the stable dev signing identity exists before the post-build re-sign
# steps (idempotent; a no-op once created). A failure here never blocks the
# build - inject scripts fall back to ad-hoc signing.
if [ -x "$REPO_BUILD/setup-signing-cert.sh" ]; then
  "$REPO_BUILD/setup-signing-cert.sh" ||
    echo "build-shell: setup-signing-cert.sh failed; continuing with ad-hoc signing" >&2
fi

TARGET="${1:-content_shell}"
# Parallel lanes share one out/atrium, so two autoninja runs at once thrash
# the same objects. Queue on a lock instead - the vm-lock helper works for any
# name, and its callers must define log and die first.
log() { echo "==> $*"; }
die() { echo "$*" >&2; exit 1; }
# shellcheck source=build/scripts/vm-lock.sh
source "$REPO_BUILD/vm-lock.sh"
# A component app loads its dylibs from the out dir, so dev-install.sh copies a
# static build. Its first build takes hours, longer than a lock waiter waits.
OUT=atrium
LOCK=build
if [ "${ATRIUM_STATIC_BUILD:-0}" = "1" ]; then
  OUT=atrium-static
  LOCK=build-static
fi
export ATRIUM_OUT_DIR="out/$OUT"
trap 'vm_lock_release "$LOCK"' EXIT
vm_lock_acquire "$LOCK"
cd "$SRC"
mkdir -p "out/$OUT"
# GN rejects a sysroot outside the build dir. args.gn says why this SDK.
# Pinned to 26.5: a newer Command Line Tools install points MacOSX.sdk at the
# 27 SDK, which ld64.lld cannot read.
mkdir -p "out/$OUT/sdk"
ln -sfn /Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk "out/$OUT/sdk/clt_sdk"
if [ "$OUT" = atrium-static ]; then
  sed -e "s|//out/atrium/|//out/$OUT/|" \
    -e 's|^is_component_build = true$|is_component_build = false|' \
    "$REPO_BUILD/../args.gn" >"out/$OUT/args.gn"
  grep -q '^is_component_build = false$' "out/$OUT/args.gn" ||
    die "build-shell: could not turn off is_component_build in out/$OUT/args.gn"
else
  cp "$REPO_BUILD/../args.gn" "out/$OUT/args.gn"
fi
# chrome/BUILD.gn's icon copy targets need this to find build/atrium.icns and
# build/Assets.car outside the source tree - checking them into chrome/app/theme
# would make every icon change a multi-megabyte binary patch (regen-patches.sh).
ATRIUM_REPO="$(cd "$REPO_BUILD/../.." && pwd)"
echo "atrium_repo_dir = \"$ATRIUM_REPO\"" >>"out/$OUT/args.gn"
gn gen "out/$OUT" --export-compile-commands=all
# Cap parallel jobs so a long build leaves the machine responsive instead of
# saturating every core. Default: all cores minus 4 (min 1). Override with
# ATRIUM_BUILD_JOBS (set it to the core count for an unthrottled build).
ncpu="$(sysctl -n hw.ncpu 2>/dev/null || echo 8)"
build_jobs="${ATRIUM_BUILD_JOBS:-$(( ncpu > 4 ? ncpu - 4 : 1 ))}"
echo "build-shell: -j ${build_jobs} (of ${ncpu} cores; set ATRIUM_BUILD_JOBS to change)"
# autoninja's own exit code has been seen to report success on a build that
# failed (a compile error, exit 0, and a stale binary ran under a
# passing test). Tee to a log and check its text too, not just PIPESTATUS.
BUILD_LOG="$(mktemp -t atrium-build-log.XXXXXX)"
trap 'rm -f "$BUILD_LOG"; vm_lock_release "$LOCK"' EXIT
# set +e: pipefail+errexit would abort on this line before build_status is
# read, skipping the failure message and the log-text check below.
set +e
caffeinate -i autoninja -C "out/$OUT" "$TARGET" -j "$build_jobs" 2>&1 | tee "$BUILD_LOG"
build_status="${PIPESTATUS[0]}"
set -e
if [ "$build_status" -ne 0 ] || grep -q "finished with an error" "$BUILD_LOG"; then
  echo "build-shell: build failed (autoninja exit ${build_status})" >&2
  exit 1
fi
"$REPO_BUILD/inject-icns.sh"

# Force LaunchServices to re-register the bundle after all mutations (icon
# injection + code signing). IconServices caches the app icon; Finder's Get Info
# would show a stale icon until the cache clears naturally. Calling lsregister
# invalidates that cache, and touching the bundle mtime helps Finder notice the
# change immediately. Only runs if the bundle exists; no-op otherwise.
# The static bundle stays unregistered: dev-install.sh registers the installed
# copy, and a second registered copy can hide the app in System Settings lists.
APP_BUNDLE="$SRC/out/atrium/Atrium.app"
if [ "$OUT" = atrium ] && [ -d "$APP_BUNDLE" ]; then
  /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "$APP_BUNDLE"
  touch "$APP_BUNDLE"
fi
