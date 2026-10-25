#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Release build steps for release-build.yml: prepare, build, package.
# A hosted job ends at 6 hours and a full build takes longer, so chained build
# jobs each resume the ninja state the last one packed.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
export CHROMIUM_DIR="${CHROMIUM_DIR:-$HOME/chromium}"
# shellcheck disable=SC1091
source "$REPO/build/scripts/env.sh"

# Pinned so a depot_tools change cannot alter a release build.
DEPOT_TOOLS_COMMIT=7f885f604cef0f45937752073e0fc34c9e43ec83
# The newest Xcode 26 on the macos-15 image.
XCODE_APP="${XCODE_APP:-/Applications/Xcode_26.3.app}"
OUT=out/release
TARGET="${TARGET:-chrome}"
CKPT_DIR="${CKPT_DIR:-${RUNNER_TEMP:-$HOME}/ckpt}"
RELEASE_DIR="${RELEASE_DIR:-${RUNNER_TEMP:-$HOME}/release}"
# GitHub kills a job at 360 min. The reserve covers the checkpoint pack.
JOB_MINUTES=330
PACK_RESERVE_MINUTES=35

log() { echo "==> $*"; }
die() { echo "ci-release: $*" >&2; exit 1; }
ver() { grep "^$1=" "$REPO/build/VERSION" | cut -d= -f2; }
set_output() { echo "$1=$2" >>"${GITHUB_OUTPUT:-/dev/stdout}"; }

# The script deletes Xcodes and kills compilers by name, so a dev machine must not run it.
[ "${GITHUB_ACTIONS:-}" = true ] || die "runs only inside GitHub Actions"

retry() { # retry <attempts> <cmd...>
  local n="$1" i=1
  shift
  until "$@"; do
    [ "$i" -lt "$n" ] || return 1
    log "attempt $i failed: $*"
    i=$((i + 1))
    sleep 30
  done
}

select_xcode() {
  [ -d "$XCODE_APP" ] || die "$XCODE_APP is not installed, set XCODE_APP to an installed Xcode 26"
  sudo xcode-select -s "$XCODE_APP"
  export DEVELOPER_DIR="$XCODE_APP/Contents/Developer"
  local sdk
  sdk="$(xcrun --show-sdk-version)"
  case "$sdk" in
    26.*) log "Xcode $XCODE_APP, SDK $sdk" ;;
    *) die "SDK $sdk is not a 26.x SDK" ;;
  esac
}

# The runner disk is too small for a Chromium tree plus build output.
disk_cleanup() {
  local keep app
  keep="$(realpath "$XCODE_APP")"
  for app in /Applications/Xcode*.app; do
    [ -e "$app" ] || [ -L "$app" ] || continue
    if [ "$(realpath "$app" 2>/dev/null || true)" != "$keep" ]; then
      sudo rm -rf "$app"
    fi
  done
  if [ -n "${ANDROID_HOME:-}" ] && [ -d "$ANDROID_HOME" ]; then
    sudo rm -rf "$ANDROID_HOME"
  fi
  xcrun simctl delete all || true
  sudo mdutil -a -i off || true
  df -h /
}

common_setup() {
  select_xcode
  disk_cleanup
}

get_depot_tools() {
  local d="$CHROMIUM_DIR/depot_tools"
  mkdir -p "$d"
  git -C "$d" init -q
  retry 3 git -C "$d" fetch -q --depth 1 \
    https://chromium.googlesource.com/chromium/tools/depot_tools.git "$DEPOT_TOOLS_COMMIT"
  git -C "$d" checkout -q FETCH_HEAD
  # DEPOT_TOOLS_UPDATE=0 also skips the Python bootstrap, and the gn wrapper fails without it.
  retry 3 "$d/ensure_bootstrap"
}

# depot_tools rides along because ninja actions can call its vpython3.
pack_ckpt() {
  mkdir -p "$CKPT_DIR"
  tar -C "$CHROMIUM_DIR" -cf - src depot_tools | zstd -q -f -11 -T0 -o "$CKPT_DIR/ckpt.tar.zst"
  ls -lh "$CKPT_DIR/ckpt.tar.zst"
}

unpack_ckpt() {
  local f="$CKPT_DIR/ckpt.tar.zst"
  [ -f "$f" ] || die "no checkpoint at $f"
  mkdir -p "$CHROMIUM_DIR"
  zstd -d -q -T0 -c "$f" | tar -C "$CHROMIUM_DIR" -xf -
  rm -f "$f"
  df -h /
}

cmd_prepare() {
  common_setup
  export DEPOT_TOOLS_UPDATE=0 DEPOT_TOOLS_METRICS=0
  get_depot_tools
  git config --global user.name "atrium-ci"
  git config --global user.email "atrium-ci@users.noreply.github.com"

  cd "$CHROMIUM_DIR"
  # The .git dirs are cut below, and tools/metrics/BUILD.gn then drops a target
  # that location tags still depend on, so gn gen fails.
  gclient config --unmanaged --name src --custom-var=generate_location_tags=False \
    https://chromium.googlesource.com/chromium/src.git
  echo 'target_os = ["mac"]' >>.gclient
  retry 3 gclient sync --no-history --shallow --nohooks -r "src@$(ver chromium_commit)"
  gclient runhooks

  # These two read the patched tree, so they run after apply-patches.sh.
  "$REPO/build/scripts/apply-patches.sh"
  "$REPO/build/scripts/verify-rust.sh"
  "$REPO/build/scripts/overlay-verify.sh"

  # build/signing holds copies of Chromium's entitlements. A Chromium bump can change them.
  local n
  for n in app helper-renderer helper-gpu; do
    cmp -s "$REPO/build/signing/$n-entitlements.plist" "$SRC/chrome/app/$n-entitlements.plist" ||
      die "build/signing/$n-entitlements.plist differs from chrome/app, copy the new one"
  done

  # The checkpoint travels as an artifact, so cut what the build does not read.
  # This runs before gn gen, so a path the graph needs fails here and not in
  # a build stage. gn loads web_tests/BUILD.gn, so that one file stays.
  find "$SRC" -name .git -prune -exec rm -rf {} +
  find "$SRC/third_party/blink/web_tests" -mindepth 1 ! -name BUILD.gn -delete
  du -sh "$SRC"/* 2>/dev/null | sort -rh | sed -n '1,15p' || true

  mkdir -p "$SRC/$OUT"
  cp "$REPO/build/args.release.gn" "$SRC/$OUT/args.gn"
  echo "atrium_repo_dir = \"$REPO\"" >>"$SRC/$OUT/args.gn"
  (cd "$SRC" && gn gen "$OUT")
  pack_ckpt
}

# Sends SIGTERM at the budget and SIGKILL 7 minutes later. A poll loop instead
# of one long sleep, so killing the watchdog leaves no 7-minute orphan.
watchdog() { # watchdog <pid> <budget seconds> <flag file>
  local pid="$1" end grace
  end=$(($(date +%s) + $2))
  while kill -0 "$pid" 2>/dev/null; do
    if [ "$(date +%s)" -ge "$end" ]; then
      : >"$3"
      kill -TERM "$pid" 2>/dev/null || true
      grace=$(($(date +%s) + 420))
      while kill -0 "$pid" 2>/dev/null && [ "$(date +%s)" -lt "$grace" ]; do sleep 5; done
      kill -KILL "$pid" 2>/dev/null || true
      return 0
    fi
    sleep 5
  done
}

stop_watchdog() {
  if [ -n "${WD_PID:-}" ]; then
    kill "$WD_PID" 2>/dev/null || true
    wait "$WD_PID" 2>/dev/null || true
    WD_PID=
  fi
}

# ninja starts its jobs in their own sessions, so a SIGKILL on ninja leaves
# them running and writing into the tree that pack_ckpt reads.
reap_compilers() {
  local re='third_party/(llvm-build|rust-toolchain)/' i
  pkill -TERM -f "$re" 2>/dev/null || true
  for i in $(seq 1 30); do
    pgrep -f "$re" >/dev/null || return 0
    sleep 1
  done
  pkill -KILL -f "$re" 2>/dev/null || true
  sleep 3
  if pgrep -f "$re" >/dev/null; then die "compiler processes outlived ninja"; fi
}

build_cleanup() {
  stop_watchdog
  if [ -n "${NINJA_PID:-}" ] && kill -0 "$NINJA_PID" 2>/dev/null; then
    kill "$NINJA_PID" 2>/dev/null || true
  fi
}

# Seconds ninja may run. STAGE_MINUTES overrides the automatic budget.
build_budget() {
  local mins="${STAGE_MINUTES:-0}" elapsed
  case "$mins" in '' | *[!0-9]*) die "STAGE_MINUTES must be a whole number" ;; esac
  if [ "$mins" -gt 0 ]; then
    echo $((mins * 60))
    return
  fi
  elapsed=$(($(date +%s) - ${JOB_START_EPOCH:-$(date +%s)}))
  echo $(((JOB_MINUTES - PACK_RESERVE_MINUTES) * 60 - elapsed))
}

cmd_build() {
  case "$TARGET" in '' | -* | *[!A-Za-z0-9_./:+-]*) die "bad TARGET '$TARGET'" ;; esac
  common_setup
  export DEPOT_TOOLS_UPDATE=0 DEPOT_TOOLS_METRICS=0
  unpack_ckpt

  local budget flag rc=0
  budget="$(build_budget)"
  # Under 10 minutes a stage makes no real progress and the chain never ends.
  [ "$budget" -ge 600 ] || die "only ${budget}s left for ninja"
  log "ninja budget ${budget}s, target $TARGET"
  flag="$(mktemp -u -t atrium-ninja-killed)"

  # The COPY edges in chrome/BUILD.gn read build/atrium.icns and Assets.car from
  # the repo. A fresh checkout makes them newer than the copies and reruns the
  # bundle steps in every stage.
  git -C "$REPO" ls-files -z | (cd "$REPO" && xargs -0 touch -h -t 200001010000)

  trap build_cleanup EXIT
  trap 'exit 130' INT TERM
  cd "$SRC"
  "$SRC/third_party/ninja/ninja" -C "$OUT" -j3 "$TARGET" &
  NINJA_PID=$!
  watchdog "$NINJA_PID" "$budget" "$flag" &
  WD_PID=$!
  wait "$NINJA_PID" || rc=$?
  stop_watchdog
  reap_compilers

  if [ "$rc" -eq 0 ]; then
    if [ "$TARGET" = chrome ]; then
      do_package
    fi
    set_output status finished
  elif [ -e "$flag" ]; then
    log "budget reached (ninja exit $rc), saving the checkpoint"
    pack_ckpt
    set_output status running
  else
    die "ninja failed with exit $rc"
  fi
}

run_limited() { # run_limited <seconds> <cmd...>, exit 124 on timeout
  local limit="$1" pid end rc=0
  shift
  "$@" &
  pid=$!
  end=$(($(date +%s) + limit))
  while kill -0 "$pid" 2>/dev/null; do
    if [ "$(date +%s)" -ge "$end" ]; then
      kill -KILL "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
      return 124
    fi
    sleep 1
  done
  wait "$pid" || rc=$?
  return "$rc"
}

do_package() {
  local app="$SRC/$OUT/Atrium.app" fw deps udd rc=0 zip
  [ -d "$app" ] || die "$app is missing after the build"
  fw="$app/Contents/Frameworks/Atrium Framework.framework/Atrium Framework"

  # No signing identity on a runner, so this ad-hoc signs.
  ATRIUM_OUT_DIR="$OUT" "$REPO/build/scripts/inject-icns.sh"

  [ "$(lipo -archs "$app/Contents/MacOS/Atrium")" = arm64 ] || die "Atrium is not arm64 only"
  [ "$(lipo -archs "$fw")" = arm64 ] || die "Atrium Framework is not arm64 only"
  # Same check as dev-install.sh: an @rpath dylib means a copy cannot start.
  deps="$(otool -L "$fw")"
  case "$deps" in *@rpath/*) die "$fw links a dylib from the out dir" ;; esac
  codesign --verify --deep --strict "$app"

  udd="$(mktemp -d)"
  run_limited 60 "$app/Contents/MacOS/Atrium" --headless=new --dump-dom \
    --no-first-run --use-mock-keychain --user-data-dir="$udd" about:blank >/dev/null || rc=$?
  rm -rf "$udd"
  [ "$rc" -eq 0 ] || die "smoke test exited $rc (124 means it timed out after 60s)"

  mkdir -p "$RELEASE_DIR"
  zip="$RELEASE_DIR/atrium-$(ver chromium_tag)-macos-arm64.zip"
  rm -f "$zip"
  ditto -c -k --keepParent "$app" "$zip"
  ls -lh "$zip"
}

# A release tag must point at a commit that is on main.
cmd_verify_tag() {
  [ "${GITHUB_REF_TYPE:-}" = tag ] || die "not a tag ref"
  [ -n "${GITHUB_SHA:-}" ] || die "GITHUB_SHA is empty"
  # persist-credentials is off, so on a private repo this fetch can fail. The
  # checkout's own origin/main (fetch-depth 0) is the fallback.
  GIT_TERMINAL_PROMPT=0 git -C "$REPO" fetch -q origin main ||
    log "fetch of origin/main failed, using the checkout's copy"
  git -C "$REPO" merge-base --is-ancestor "$GITHUB_SHA" origin/main ||
    die "tag commit $GITHUB_SHA is not on main"
  log "tag commit is on main"
}

# Every step below reads credentials from the environment and prints none.
csign() { codesign --force --timestamp --keychain "$SIGN_KC" --sign "$SIGN_ID" "$@"; }

# Signs inside out, following chrome/installer/mac/signing/parts.py. The
# renderer and GPU helpers drop library validation because it cannot coexist
# with the JIT entitlement.
sign_bundle() {
  local app="$1" fwk f name opts ent bid ident
  fwk="$app/Contents/Frameworks/Atrium Framework.framework"
  [ -d "$fwk" ] || die "$fwk is missing"
  bid="$(plutil -extract CFBundleIdentifier raw "$app/Contents/Info.plist")"

  while IFS= read -r -d '' f; do
    csign "$f"
  done < <(find "$app" -type f -name '*.dylib' -print0)

  while IFS= read -r -d '' f; do
    ident="$(basename "$f")"
    [ "$ident" != web_app_shortcut_copier ] || ident="$bid.web_app_shortcut_copier"
    csign --options restrict,library,runtime,kill -i "$ident" "$f"
  done < <(find "$app/Contents" -type f \( -name chrome_crashpad_handler -o -name app_mode_loader \
    -o -name web_app_shortcut_copier -o -name atrium_ai_fm_helper \) -print0)

  while IFS= read -r -d '' f; do
    name="$(basename "$f")"
    opts=restrict,library,runtime,kill
    ent=
    case "$name" in
      *"(Renderer)"*) opts=restrict,runtime,kill ent=helper-renderer ;;
      *"(GPU)"*) opts=restrict,runtime,kill ent=helper-gpu ;;
    esac
    if [ -n "$ent" ]; then
      csign --options "$opts" --entitlements "$REPO/build/signing/$ent-entitlements.plist" "$f"
    else
      csign --options "$opts" "$f"
    fi
  done < <(find "$fwk/Versions" -type d -name '*Helper*.app' -print0)

  csign "$fwk"
  csign --options restrict,library,runtime,kill \
    --entitlements "$REPO/build/signing/app-entitlements.plist" "$app"
}

# A named list can miss a nested binary, and notarization then fails an hour
# later. Dies with the list of Mach-O files that are unsigned or ad-hoc signed.
check_signed() { # check_signed <app>
  local f desc info bad=""
  while IFS= read -r -d '' f && IFS= read -r desc; do
    case "$desc" in *Mach-O*) ;; *) continue ;; esac
    if ! codesign --verify "$f" 2>/dev/null; then
      bad="$bad$f"$'\n'
      continue
    fi
    info="$(codesign -dv "$f" 2>&1 || true)"
    case "$info" in *Signature=adhoc*) bad="$bad$f"$'\n' ;; esac
  done < <(find "$1" -type f -print0 | xargs -0 file -0)
  [ -z "$bad" ] || die "unsigned or ad-hoc Mach-O files:"$'\n'"$bad"
}

# --wait exits non-zero on an Invalid result, so the id and status come from
# the JSON whatever the exit code is.
notarize() { # notarize <file>
  local out errf rc=0 status id
  errf="$(mktemp)"
  out="$(xcrun notarytool submit "$1" --key "$NOTARY_KEY" --key-id "$NOTARY_API_KEY_ID" \
    --issuer "$NOTARY_API_ISSUER_ID" --wait --output-format json 2>"$errf")" || rc=$?
  status="$(jq -r '.status // empty' <<<"$out" 2>/dev/null || true)"
  id="$(jq -r '.id // empty' <<<"$out" 2>/dev/null || true)"
  if [ "$rc" -eq 0 ] && [ "$status" = Accepted ]; then
    rm -f "$errf"
    log "notarized $1 ($id)"
    return
  fi
  cat "$errf" >&2
  rm -f "$errf"
  echo "$out" >&2
  if [ -n "$id" ]; then
    xcrun notarytool log "$id" --key "$NOTARY_KEY" --key-id "$NOTARY_API_KEY_ID" \
      --issuer "$NOTARY_API_ISSUER_ID" >&2 || true
  fi
  die "notarization of $1 failed (exit $rc, status '${status:-none}')"
}

# Gatekeeper must accept the DMG and the app inside it, each with a ticket.
verify_dmg() { # verify_dmg <dmg>
  local mnt rc=0
  xcrun stapler validate "$1"
  spctl -a -t open --context context:primary-signature -vv "$1"
  mnt="$(mktemp -d)"
  hdiutil attach "$1" -readonly -nobrowse -mountpoint "$mnt" >/dev/null
  { spctl -a -t exec -vv "$mnt/Atrium.app" && xcrun stapler validate "$mnt/Atrium.app"; } || rc=$?
  hdiutil detach "$mnt" >/dev/null || hdiutil detach -force "$mnt" >/dev/null || true
  [ "$rc" -eq 0 ] || die "Gatekeeper rejects the app inside $1"
}

cmd_sign() {
  select_xcode
  local base="${RUNNER_TEMP:-$HOME}" app work stage dmg
  app="${APP_PATH:-$base/app/Atrium.app}"
  SIGN_ID="${MACOS_SIGN_IDENTITY:?}"
  SIGN_KC="${SIGN_KEYCHAIN:-$base/atrium-sign.keychain-db}"
  NOTARY_KEY="${NOTARY_KEY_PATH:-$base/notary-key.p8}"
  : "${NOTARY_API_KEY_ID:?}" "${NOTARY_API_ISSUER_ID:?}"
  [ -d "$app" ] || die "$app is missing"
  [ -f "$SIGN_KC" ] || die "keychain $SIGN_KC is missing"
  [ -f "$NOTARY_KEY" ] || die "notary key $NOTARY_KEY is missing"
  work="$(mktemp -d)"

  sign_bundle "$app"
  codesign --verify --deep --strict "$app"
  check_signed "$app"
  ditto -c -k --keepParent "$app" "$work/app.zip"
  notarize "$work/app.zip"
  xcrun stapler staple "$app"

  stage="$work/dmg"
  mkdir -p "$stage" "$RELEASE_DIR"
  ditto "$app" "$stage/Atrium.app"
  ln -s /Applications "$stage/Applications"
  dmg="$RELEASE_DIR/atrium-$(ver chromium_tag)-macos-arm64.dmg"
  rm -f "$dmg"
  hdiutil create -volname Atrium -srcfolder "$stage" -format UDZO "$dmg" >/dev/null
  csign "$dmg"
  notarize "$dmg"
  xcrun stapler staple "$dmg"
  verify_dmg "$dmg"
  ls -lh "$dmg"
  rm -rf "$work"
}

case "${1:-}" in
  prepare) cmd_prepare ;;
  build) cmd_build ;;
  package)
    common_setup
    do_package
    ;;
  verify-tag) cmd_verify_tag ;;
  sign) cmd_sign ;;
  *)
    echo "usage: ci-release.sh prepare|build|package|verify-tag|sign" >&2
    exit 2
    ;;
esac
