#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# build/scripts/reset-atrium.sh - Move aside the persisted Atrium state for a clean
# first-run, so manual re-testing (spaces, themes, settings, hotkeys,
# routing, Chromium profiles) starts from scratch.
#
# Why: testing Atrium involves a lot of manual relaunches; wiping
# state by hand is error-prone and risks losing real session data. This
# never deletes anything - it `mv`s the whole state dir aside to a
# timestamped backup so it can be restored if needed.
#
# Usage:
#   build/scripts/reset-atrium.sh [--yes|-y] [--stores-only] [--dry-run] [-h|--help]
#
#   --yes, -y       Required to actually move anything (also honors FORCE=1).
#   --stores-only   Only reset the Swift-side stores (spaces.json, themes.json,
#                   settings.json, hotkeys.json, routing.json, favicons/,
#                   command-bar-selection-memory.json).
#                   Leaves the Chromium user-data-dir (profiles, cookies,
#                   logins) untouched.
#   --dry-run       Print what would be moved without touching anything.
set -euo pipefail

APP_SUPPORT_DIR="${HOME}/Library/Application Support/Atrium"
CHROMIUM_DIR="${APP_SUPPORT_DIR}/Chromium"
STORE_FILES=(spaces.json spaces.json.bak themes.json settings.json hotkeys.json routing.json command-bar-selection-memory.json)
STORE_DIRS=(favicons)

YES=0
STORES_ONLY=0
DRY_RUN=0

usage() {
  sed -n '2,19p' "$0" | sed 's/^# \{0,1\}//'
}

for arg in "$@"; do
  case "${arg}" in
    --yes|-y) YES=1 ;;
    --stores-only) STORES_ONLY=1 ;;
    --dry-run) DRY_RUN=1 ;;
    -h|--help) usage; exit 0 ;;
    *)
      echo "error: unknown argument: ${arg}" >&2
      usage
      exit 1
      ;;
  esac
done

if [ "${FORCE:-0}" = "1" ]; then
  YES=1
fi

# No confirmation available (non-interactive shell) and no --yes/-y given:
# never guess - fall back to dry-run instead of risking an unattended wipe.
if [ "${YES}" != "1" ] && [ "${DRY_RUN}" != "1" ] && [ ! -t 0 ]; then
  echo "Non-interactive shell and no --yes/-y given; forcing --dry-run." >&2
  DRY_RUN=1
fi

if pgrep -x "Atrium" >/dev/null 2>&1; then
  echo "error: Atrium is currently running. Quit it before resetting state." >&2
  exit 1
fi

if [ ! -d "${APP_SUPPORT_DIR}" ]; then
  echo "Nothing to reset: ${APP_SUPPORT_DIR} does not exist."
  exit 0
fi

TIMESTAMP="$(date +%Y%m%d-%H%M%S)"

if [ "${STORES_ONLY}" = "1" ]; then
  BACKUP_DIR="${APP_SUPPORT_DIR}.reset-backup-${TIMESTAMP}"
  echo "Mode: stores-only (Chromium profiles under ${CHROMIUM_DIR} are kept)"
  echo "Will move the following into ${BACKUP_DIR}/ :"
  MOVE_TARGETS=()
  for f in "${STORE_FILES[@]}"; do
    path="${APP_SUPPORT_DIR}/${f}"
    if [ -e "${path}" ]; then
      echo "  ${path}"
      MOVE_TARGETS+=("${path}")
    fi
  done
  for d in "${STORE_DIRS[@]}"; do
    path="${APP_SUPPORT_DIR}/${d}"
    if [ -e "${path}" ]; then
      echo "  ${path}"
      MOVE_TARGETS+=("${path}")
    fi
  done

  if [ "${#MOVE_TARGETS[@]}" -eq 0 ]; then
    echo "Nothing to reset: no store files found under ${APP_SUPPORT_DIR}."
    exit 0
  fi

  if [ "${DRY_RUN}" = "1" ]; then
    echo "Dry run: would create ${BACKUP_DIR}/ and move the paths above into it."
    exit 0
  fi

  if [ "${YES}" != "1" ]; then
    echo "error: refusing to move state in ${APP_SUPPORT_DIR} without --yes" >&2
    exit 1
  fi

  mkdir -p "${BACKUP_DIR}"
  for path in "${MOVE_TARGETS[@]}"; do
    mv "${path}" "${BACKUP_DIR}/"
  done
  echo "Moved store state to ${BACKUP_DIR}/"
  echo "Chromium profiles kept at ${CHROMIUM_DIR}"
else
  BACKUP_DIR="${APP_SUPPORT_DIR}.reset-backup-${TIMESTAMP}"
  echo "Mode: full reset (Swift stores + Chromium user-data-dir)"
  echo "Will move ${APP_SUPPORT_DIR} -> ${BACKUP_DIR}"

  if [ "${DRY_RUN}" = "1" ]; then
    echo "Dry run: would move the entire directory above."
    exit 0
  fi

  if [ "${YES}" != "1" ]; then
    echo "error: refusing to move ${APP_SUPPORT_DIR} without --yes" >&2
    exit 1
  fi

  mv "${APP_SUPPORT_DIR}" "${BACKUP_DIR}"
  echo "Moved ${APP_SUPPORT_DIR} to ${BACKUP_DIR}"
fi

echo "Next launch of Atrium will start from a clean first-run state."
