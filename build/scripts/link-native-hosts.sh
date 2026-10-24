#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Native hosts (1Password, Claude Code, Google Drive) write their manifest only
# into browser folders that they know. Atrium is on none of their lists.
set -euo pipefail
SRC_DIR="${HOME}/Library/Application Support/Google/Chrome/NativeMessagingHosts"
# Chromium reads user manifests from inside the user data dir, the one that
# run-atrium.sh passes.
DST_DIR="${HOME}/Library/Application Support/Atrium/Chromium/NativeMessagingHosts"

if [ ! -d "${SRC_DIR}" ]; then
  echo "link-native-hosts: no Chrome manifests at ${SRC_DIR}, nothing to link" >&2
  exit 0
fi

mkdir -p "${DST_DIR}"
linked=0
for manifest in "${SRC_DIR}"/*.json; do
  [ -e "${manifest}" ] || continue
  target="${DST_DIR}/$(basename "${manifest}")"
  if [ -e "${target}" ] || [ -L "${target}" ]; then
    continue
  fi
  ln -s "${manifest}" "${target}"
  linked=$((linked + 1))
done
echo "link-native-hosts: ${linked} new link(s) in ${DST_DIR}"
