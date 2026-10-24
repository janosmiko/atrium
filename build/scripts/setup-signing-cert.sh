#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Creates a STABLE self-signed code-signing identity ("Atrium Dev") in the
# login keychain for local development. build/scripts/build-shell.sh re-signs the
# Atrium.app bundle on every build; signing ad-hoc (codesign --sign -) pins the
# designated requirement to the binary's CDHash, which changes every build, so
# the keychain "Always Allow" grant for Chromium's "Atrium Safe Storage" item
# is invalidated and macOS re-prompts on each rebuild. Signing with a fixed
# self-signed identity instead gives a stable DR (identifier + certificate leaf
# hash), so the grant persists across rebuilds.
#
# Idempotent: if the identity already exists it exits 0 without recreating it,
# so it is safe to call from a Makefile/bootstrap step on every run. All private
# key material lives in a temp dir wiped on every exit path - never under the
# git repo.
#
# Local-dev trade-offs (accepted): assumes single invocation - two concurrent
# runs could both pass the "not found" check and import duplicate "Atrium Dev"
# certs, which would make signing ambiguous until one is removed by hand. The
# imported key is not marked non-exportable, and the throwaway p12 passphrase
# is passed via argv (briefly visible to `ps`); both are fine for a self-signed
# CA:FALSE dev key whose only power is signing local Atrium builds.
set -euo pipefail

# Common name for the signing identity. Override to namespace per machine/user.
IDENTITY="${ATRIUM_SIGN_IDENTITY:-Atrium Dev}"

LOGIN_KEYCHAIN="$HOME/Library/Keychains/login.keychain-db"

# Already present? codesign only needs the identity to exist - nothing to do.
if security find-identity -v -p codesigning | grep -qF "\"$IDENTITY\""; then
  echo "setup-signing-cert: identity \"$IDENTITY\" already exists, nothing to do"
  security find-identity -v -p codesigning | grep -F "\"$IDENTITY\""
  exit 0
fi

# Private key + p12 are generated here and destroyed on exit (success, error,
# or interrupt). Nothing sensitive is ever written under the repo.
WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

KEY_PEM="$WORKDIR/key.pem"
CERT_PEM="$WORKDIR/cert.pem"
CERT_P12="$WORKDIR/cert.p12"

echo "setup-signing-cert: creating self-signed code-signing identity \"$IDENTITY\""

# Self-signed leaf usable as its own anchor. ~10y validity so the DR (which
# pins the cert) stays valid across years of rebuilds. Extensions match what
# codesign expects of a signing cert: not a CA, digital-signature key usage,
# codeSigning EKU (1.3.6.1.5.5.7.3.3).
if ! openssl req -x509 -newkey rsa:2048 -sha256 -days 3650 -nodes \
  -keyout "$KEY_PEM" -out "$CERT_PEM" \
  -subj "/CN=$IDENTITY" \
  -addext "basicConstraints=critical,CA:FALSE" \
  -addext "keyUsage=critical,digitalSignature" \
  -addext "extendedKeyUsage=codeSigning" >/dev/null 2>"$WORKDIR/openssl.err"; then
  echo "setup-signing-cert: openssl req failed:" >&2
  cat "$WORKDIR/openssl.err" >&2
  exit 1
fi

# Bundle key+cert as PKCS#12. -legacy uses the older PBE algorithms that
# macOS's security(1) import reliably accepts (OpenSSL 3's modern AES-based
# default trips up security import). The passphrase is a throwaway that only
# protects this temp file (deleted on exit) - it is NOT the keychain password;
# an empty p12 passphrase fails security import's MAC check, so we use a random
# one and hand it straight to the import below.
P12_PASS="$(openssl rand -hex 16)"
if ! openssl pkcs12 -export -legacy \
  -inkey "$KEY_PEM" -in "$CERT_PEM" \
  -name "$IDENTITY" -passout "pass:$P12_PASS" \
  -out "$CERT_P12" >/dev/null 2>"$WORKDIR/openssl.err"; then
  echo "setup-signing-cert: openssl pkcs12 failed (note: -legacy needs OpenSSL 3, not LibreSSL):" >&2
  cat "$WORKDIR/openssl.err" >&2
  exit 1
fi

# Import into the login keychain and pre-authorize codesign to use the key.
security import "$CERT_P12" -k "$LOGIN_KEYCHAIN" -P "$P12_PASS" \
  -T /usr/bin/codesign >/dev/null

# Mark the self-signed cert as trusted for code signing (user domain, no admin
# rights needed) so it shows up as a *valid* identity under
# `find-identity -v -p codesigning` - which codesign and this script's own
# idempotency check both rely on. Without trust it imports but reads back as
# CSSMERR_TP_NOT_TRUSTED. Non-fatal: codesign can still sign with an untrusted
# key and still produces the same stable DR, so a failure here is a warning.
if ! security add-trusted-cert -r trustRoot -p codeSign \
  -k "$LOGIN_KEYCHAIN" "$CERT_PEM" >/dev/null 2>&1; then
  echo "setup-signing-cert: note - could not mark the cert trusted for code signing;"
  echo "setup-signing-cert:       codesign will still sign with it, but it may not appear"
  echo "setup-signing-cert:       under 'security find-identity -v -p codesigning'."
fi

# set-key-partition-list lets codesign use the key with no GUI prompt, but it
# needs the login keychain password. We never hardcode or echo it: try an empty
# password non-interactively and, if that fails, fall back to a one-time GUI
# allow rather than failing the whole setup.
if security set-key-partition-list \
  -S apple-tool:,apple:,codesign: -s -k "" "$LOGIN_KEYCHAIN" >/dev/null 2>&1; then
  echo "setup-signing-cert: partition list set - codesign can use the key with no prompt"
else
  echo "setup-signing-cert: note - could not set the key partition list non-interactively;"
  echo "setup-signing-cert:       the FIRST build may show a one-time \"codesign wants to sign"
  echo "setup-signing-cert:       using key ...\" dialog. Click \"Always Allow\" once - it persists."
fi

# Final summary: identity name + its SHA-1 hash from the identity list.
echo
echo "setup-signing-cert: done. Code-signing identity:"
security find-identity -v -p codesigning | grep -F "\"$IDENTITY\""
