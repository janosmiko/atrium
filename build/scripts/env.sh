#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Shared environment for all Atrium build scripts. Source, don't execute.
# Machine-local overrides (e.g. CHROMIUM_DIR on an external SSD) live in
# local.env, which is gitignored.
# Resolve this file's directory under bash AND zsh (BASH_SOURCE is bash-only).
if [ -n "${BASH_SOURCE:-}" ]; then
  _atrium_env_dir="$(dirname "${BASH_SOURCE[0]}")"
else
  # shellcheck disable=SC2296  # zsh-only expansion, guarded above
  _atrium_env_dir="$(dirname "${(%):-%N}")"
fi
# shellcheck disable=SC1091
[ -f "$_atrium_env_dir/local.env" ] && source "$_atrium_env_dir/local.env"
unset _atrium_env_dir
export CHROMIUM_DIR="${CHROMIUM_DIR:-$HOME/chromium-atrium}"
export PATH="$CHROMIUM_DIR/depot_tools:$PATH"
export SRC="$CHROMIUM_DIR/src"

# Code-signing identity for local dev builds. A stable self-signed identity
# (build/scripts/setup-signing-cert.sh) keeps the macOS keychain "Always Allow" grant
# for "Atrium Safe Storage" valid across rebuilds; ad-hoc ("-") pins the
# designated requirement to the binary hash and re-prompts on every build.
export ATRIUM_SIGN_IDENTITY="${ATRIUM_SIGN_IDENTITY:-Atrium Dev}"

# Re-sign an app bundle with the stable identity when it exists, else ad-hoc.
atrium_codesign() {
  local bundle="$1"
  if security find-identity -v -p codesigning 2>/dev/null | grep -qF "\"$ATRIUM_SIGN_IDENTITY\""; then
    codesign --force --sign "$ATRIUM_SIGN_IDENTITY" "$bundle"
  else
    echo "warning: signing identity '$ATRIUM_SIGN_IDENTITY' not found; ad-hoc signing" \
         "(keychain re-prompts every build - run build/scripts/setup-signing-cert.sh)" >&2
    codesign --force --sign - "$bundle"
  fi
}
