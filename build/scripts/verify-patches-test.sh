#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
set -euo pipefail

REPO_BUILD="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# shellcheck disable=SC1091
source "$REPO_BUILD/owned-roots.sh"
# shellcheck disable=SC1091
source "$REPO_BUILD/verify-patches.sh"

mkdir -p "$WORK/patches-owned" "$WORK/patches-foreign" "$WORK/patches-quoted"

cat > "$WORK/patches-owned/0001-touch-owned.patch" <<'EOF'
diff --git a/chrome/browser/ui/atrium/x.cc b/chrome/browser/ui/atrium/x.cc
index 0000000..1111111 100644
--- a/chrome/browser/ui/atrium/x.cc
+++ b/chrome/browser/ui/atrium/x.cc
@@ -1 +1 @@
-old
+new
EOF

cat > "$WORK/patches-foreign/0001-touch-foreign.patch" <<'EOF'
diff --git a/chrome/browser/foo.cc b/chrome/browser/foo.cc
index 0000000..1111111 100644
--- a/chrome/browser/foo.cc
+++ b/chrome/browser/foo.cc
@@ -1 +1 @@
-old
+new
EOF

# git quotes the header when a path has a space, so it never matches the plain
# a/...  b/... form.
cat > "$WORK/patches-quoted/0001-touch-owned-quoted.patch" <<'EOF'
diff --git "a/chrome/browser/ui/atrium/spacey file.cc" "b/chrome/browser/ui/atrium/spacey file.cc"
index 0000000..1111111 100644
--- "a/chrome/browser/ui/atrium/spacey file.cc"
+++ "b/chrome/browser/ui/atrium/spacey file.cc"
@@ -1 +1 @@
-old
+new
EOF

run() {
  local name="$1" expect_status="$2" expect_grep="$3" dir="$4"
  local status=0
  check_both_halves "$dir" > "$WORK/out.log" 2>&1 || status=$?
  if [ "$status" -ne "$expect_status" ]; then
    echo "FAIL: $name - expected exit $expect_status, got $status" >&2
    cat "$WORK/out.log" >&2
    exit 1
  fi
  if [ -n "$expect_grep" ] && ! grep -qF "$expect_grep" "$WORK/out.log"; then
    echo "FAIL: $name - expected output to contain: $expect_grep" >&2
    cat "$WORK/out.log" >&2
    exit 1
  fi
  echo "PASS: $name"
}

run "owned root touched fails" 1 \
  "BOTH: 0001-touch-owned.patch: chrome/browser/ui/atrium/x.cc is under an owned root, it belongs in overlay/" \
  "$WORK/patches-owned"

run "foreign path only passes" 0 "" "$WORK/patches-foreign"

run "quoted header with a space fails" 1 \
  "BOTH: 0001-touch-owned-quoted.patch: chrome/browser/ui/atrium/spacey file.cc is under an owned root, it belongs in overlay/" \
  "$WORK/patches-quoted"

echo "all verify-patches-test cases passed"
