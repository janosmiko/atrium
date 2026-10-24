#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
set -euo pipefail

REPO_BUILD="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

FIXTURE="$WORK/repo/build/scripts"
OVERLAY="$WORK/repo/overlay"
mkdir -p "$FIXTURE" "$OVERLAY"
# The guard lists overlay files through git, so the fixture repo must be one.
git init --quiet "$WORK/repo"
git -C "$WORK/repo" config user.email test@example.com
git -C "$WORK/repo" config user.name test
cp "$REPO_BUILD/overlay-verify.sh" "$FIXTURE/overlay-verify.sh"
cp "$REPO_BUILD/owned-roots.sh" "$FIXTURE/owned-roots.sh"
chmod +x "$FIXTURE/overlay-verify.sh"

cat > "$FIXTURE/env.sh" <<EOF
export SRC="$WORK/src"
EOF

git init --quiet "$WORK/src"
git -C "$WORK/src" config user.email test@example.com
git -C "$WORK/src" config user.name test

mkdir -p "$WORK/src/chrome/browser/ui/atrium" \
         "$WORK/src/chrome/browser/resources/atrium" \
         "$WORK/src/chrome/browser/ui/webui/atrium" \
         "$WORK/src/third_party/atrium/core/target"
echo 'content-a' > "$WORK/src/chrome/browser/ui/atrium/a.cc"
echo 'content-b' > "$WORK/src/chrome/browser/resources/atrium/b.ts"
echo 'content-w' > "$WORK/src/chrome/browser/ui/webui/atrium/w.cc"
echo 'content-c' > "$WORK/src/third_party/atrium/c.rs"
# Untracked build output under an owned root - must never count as extra.
echo 'build-artifact' > "$WORK/src/third_party/atrium/core/target/d.o"
git -C "$WORK/src" add chrome third_party/atrium/c.rs
git -C "$WORK/src" commit --quiet -m "fixture"

mkdir -p "$OVERLAY/chrome/browser/ui/atrium" \
         "$OVERLAY/chrome/browser/resources/atrium" \
         "$OVERLAY/chrome/browser/ui/webui/atrium" \
         "$OVERLAY/third_party/atrium"
echo 'content-a' > "$OVERLAY/chrome/browser/ui/atrium/a.cc"
echo 'content-b' > "$OVERLAY/chrome/browser/resources/atrium/b.ts"
echo 'content-w' > "$OVERLAY/chrome/browser/ui/webui/atrium/w.cc"
echo 'content-c' > "$OVERLAY/third_party/atrium/c.rs"

run() {
  local name="$1" expect_status="$2" expect_grep="$3"
  local status=0
  bash "$FIXTURE/overlay-verify.sh" > "$WORK/out.log" 2>&1 || status=$?
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

# Baseline: overlay/ mirrors $SRC exactly, including the gitignored target/
# dir being invisible to the walk.
run "clean checkout passes" 0 "overlay-verify OK"

# Content drift.
echo 'content-a-EDITED' > "$OVERLAY/chrome/browser/ui/atrium/a.cc"
run "content mismatch fails" 1 "DRIFT: chrome/browser/ui/atrium/a.cc"
echo 'content-a' > "$OVERLAY/chrome/browser/ui/atrium/a.cc"

# A file added under an owned root in $SRC but never adopted into overlay/.
echo 'content-e' > "$WORK/src/third_party/atrium/e.rs"
git -C "$WORK/src" add third_party/atrium/e.rs
git -C "$WORK/src" commit --quiet -m "unadopted file"
run "extra tracked file not adopted fails" 1 "EXTRA: third_party/atrium/e.rs"
git -C "$WORK/src" rm --quiet third_party/atrium/e.rs
git -C "$WORK/src" commit --quiet -m "revert"

# Same check under the webui root.
echo 'content-f' > "$WORK/src/chrome/browser/ui/webui/atrium/f.cc"
git -C "$WORK/src" add chrome/browser/ui/webui/atrium/f.cc
git -C "$WORK/src" commit --quiet -m "unadopted webui file"
run "extra tracked webui file not adopted fails" 1 "EXTRA: chrome/browser/ui/webui/atrium/f.cc"
git -C "$WORK/src" rm --quiet chrome/browser/ui/webui/atrium/f.cc
git -C "$WORK/src" commit --quiet -m "revert"

# A tracked overlay file removed from disk must not count as ORPHAN. The path
# sorts last, so the guard's final read is the deleted one: that case once
# killed the script under pipefail.
echo 'content-g' > "$OVERLAY/third_party/atrium/zz-gone.rs"
git -C "$WORK/repo" add overlay
git -C "$WORK/repo" commit --quiet -m "tracked then deleted"
rm "$OVERLAY/third_party/atrium/zz-gone.rs"
run "tracked overlay file deleted from disk is not ORPHAN" 0 "overlay-verify OK"
git -C "$WORK/repo" rm --quiet --cached overlay/third_party/atrium/zz-gone.rs
git -C "$WORK/repo" commit --quiet -m "revert"

# A gitignored file under an owned root must not count as ORPHAN.
echo '*.ignored' > "$WORK/repo/.gitignore"
git -C "$WORK/repo" add .gitignore
git -C "$WORK/repo" commit --quiet -m "add gitignore"
echo 'content-h' > "$OVERLAY/third_party/atrium/h.ignored"
run "gitignored overlay file is not ORPHAN" 0 "overlay-verify OK"
rm "$OVERLAY/third_party/atrium/h.ignored"

# An untracked new overlay file the tree lacks must count as ORPHAN.
echo 'content-i' > "$OVERLAY/third_party/atrium/i.rs"
run "untracked overlay file not in tree is ORPHAN" 1 "ORPHAN: overlay/third_party/atrium/i.rs"
rm "$OVERLAY/third_party/atrium/i.rs"

# Confirmed clean again, so the two failures above are not just always-fail.
run "clean checkout passes again" 0 "overlay-verify OK"

echo "all overlay-verify-test cases passed"
