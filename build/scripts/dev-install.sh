#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# A terminal launch makes macOS check privacy permissions such as Local Network
# against the terminal app, so this installs a copy and starts it with open.
set -euo pipefail
REPO_BUILD="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091
source "$REPO_BUILD/env.sh"

INSTALL_DIR="${ATRIUM_DEV_INSTALL_DIR:-$HOME/Applications}"
APP="$INSTALL_DIR/Atrium.app"
BUILT="$SRC/out/atrium-static/Atrium.app"
LOG="$REPO_BUILD/../atrium-run.log"

fail() {
  echo "dev-install: $*" >&2
  exit 1
}

case "$INSTALL_DIR" in
  /*) ;;
  *) fail "ATRIUM_DEV_INSTALL_DIR must be an absolute path" ;;
esac

ATRIUM_STATIC_BUILD=1 "$REPO_BUILD/build-shell.sh" chrome
[ -d "$BUILT" ] || fail "$BUILT is missing after the build"
if otool -L "$BUILT/Contents/Frameworks/Atrium Framework.framework/Atrium Framework" | grep -q '@rpath/'; then
  fail "$BUILT links a dylib from the out dir, so a copy of it cannot start"
fi

# Only an Atrium on the default profile holds the profile lock that the new copy
# needs. Probes and tests run on their own --user-data-dir and must survive, so
# pkill -x Atrium is not safe here (see vm-scenario.sh).
profile_pids() {
  local pid args
  for pid in $(pgrep -x Atrium); do
    args="$(ps -o args= -p "$pid")" || continue
    if [[ "$args" == *--user-data-dir=* ]] &&
      [[ "$args " != *"--user-data-dir=$HOME/Library/Application Support/Atrium/Chromium "* ]]; then
      continue
    fi
    echo "$pid"
  done
}

# SIGTERM makes Chromium quit the normal way, so the session is saved.
pids="$(profile_pids)"
if [ -n "$pids" ]; then
  echo "dev-install: quitting the running Atrium (pid $(echo "$pids" | xargs))"
  # shellcheck disable=SC2086
  kill -TERM $pids 2>/dev/null || true
  for _ in $(seq 1 120); do
    [ -z "$(profile_pids)" ] && break
    sleep 0.25
  done
  [ -z "$(profile_pids)" ] || fail "Atrium is still running. Quit it and try again."
fi

mkdir -p "$INSTALL_DIR"
rm -rf "$APP.new"
ditto "$BUILT" "$APP.new"
rm -rf "$APP"
mv "$APP.new" "$APP"
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "$APP"
echo "dev-install: installed $APP"

# open appends to the log, so rotate it the same way run-atrium.sh does.
[ -f "$LOG" ] && mv -f "$LOG" "$LOG.prev"
echo "=== atrium dev-install run $(date '+%Y-%m-%d %H:%M:%S') ===" >"$LOG"
open --stdout "$LOG" --stderr "$LOG" "$APP"

# open returns once the launch starts, so a crash at startup looks like success.
for _ in $(seq 1 20); do
  pgrep -f "^$APP/Contents/MacOS/Atrium" >/dev/null && break
  sleep 0.5
done
sleep 3
if ! pgrep -f "^$APP/Contents/MacOS/Atrium" >/dev/null; then
  tail -20 "$LOG" >&2
  fail "$APP did not stay running. See $LOG"
fi
echo "dev-install: Atrium is running from $APP"
