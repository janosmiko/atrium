#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
#
# Regression: autoninja can exit 0 while its own output says the
# build failed. Runs the real script against stubbed build tools.
set -euo pipefail

REPO_BUILD="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# The real build-shell.sh and verify-rust.sh, run from a fixture that mirrors
# the repo layout both scripts walk up to, with a stub env.sh/inject-icns.sh.
FIXTURE="$WORK/repo/build/scripts"
CANON="$WORK/repo/rust/atrium-core/src"
OVERLAY="$WORK/repo/overlay/third_party/atrium/core/src"
COPY="$WORK/src/third_party/atrium/core/src"
mkdir -p "$FIXTURE" "$WORK/depot_tools" "$CANON" "$OVERLAY" "$COPY"
cp "$REPO_BUILD/build-shell.sh" "$FIXTURE/build-shell.sh"
cp "$REPO_BUILD/verify-rust.sh" "$FIXTURE/verify-rust.sh"
cp "$REPO_BUILD/vm-lock.sh" "$FIXTURE/vm-lock.sh"
touch "$WORK/repo/build/args.gn"
echo 'pub fn tint() -> u32 { 0 }' >"$CANON/lib.rs"
cp "$CANON/lib.rs" "$OVERLAY/lib.rs"
cp "$CANON/lib.rs" "$COPY/lib.rs"

cat >"$FIXTURE/env.sh" <<EOF
export SRC="$WORK/src"
export PATH="$WORK/depot_tools:\$PATH"
export VM_LOCK_ROOT="$WORK/vm-locks"
EOF

INJECT_MARKER="$WORK/inject-icns-ran"
cat >"$FIXTURE/inject-icns.sh" <<EOF
#!/usr/bin/env bash
touch "$INJECT_MARKER"
EOF
chmod +x "$FIXTURE/inject-icns.sh"

# overlay-verify.sh diffs $SRC's git history against overlay/ - out of scope
# for this fixture, which has neither. build-shell.sh runs it unconditionally.
cat >"$FIXTURE/overlay-verify.sh" <<'EOF'
#!/usr/bin/env bash
exit 0
EOF
chmod +x "$FIXTURE/overlay-verify.sh"

cat >"$WORK/depot_tools/gn" <<'EOF'
#!/usr/bin/env bash
exit 0
EOF
chmod +x "$WORK/depot_tools/gn"

AUTONINJA_MARKER="$WORK/autoninja-ran"

run_case() {
  local name="$1" autoninja_exit="$2" autoninja_text="$3" expect_status="$4"
  local expect_autoninja="${5:-yes}"
  rm -f "$INJECT_MARKER" "$AUTONINJA_MARKER"
  cat >"$WORK/depot_tools/autoninja" <<EOF
#!/usr/bin/env bash
touch "$AUTONINJA_MARKER"
echo "$autoninja_text"
exit $autoninja_exit
EOF
  chmod +x "$WORK/depot_tools/autoninja"

  local actual_status=0
  bash "$FIXTURE/build-shell.sh" content_shell >"$WORK/out.log" 2>&1 || actual_status=$?

  if [ "$actual_status" -ne "$expect_status" ]; then
    echo "FAIL: $name - expected exit $expect_status, got $actual_status" >&2
    cat "$WORK/out.log" >&2
    exit 1
  fi
  if [ "$expect_status" -eq 0 ] && [ ! -f "$INJECT_MARKER" ]; then
    echo "FAIL: $name - expected inject-icns.sh to run, it did not" >&2
    exit 1
  fi
  if [ "$expect_status" -ne 0 ] && [ -f "$INJECT_MARKER" ]; then
    echo "FAIL: $name - inject-icns.sh ran after a failed build" >&2
    exit 1
  fi
  if [ "$expect_autoninja" = "no" ] && [ -f "$AUTONINJA_MARKER" ]; then
    echo "FAIL: $name - autoninja ran, the guard should have stopped first" >&2
    cat "$WORK/out.log" >&2
    exit 1
  fi
  if [ "$expect_status" -ne 0 ] && [ -f "$AUTONINJA_MARKER" ] \
    && ! grep -q "build-shell: build failed" "$WORK/out.log"; then
    echo "FAIL: $name - autoninja ran and failed but the failure message never printed" >&2
    cat "$WORK/out.log" >&2
    exit 1
  fi
  echo "PASS: $name"
}

# The regression case: autoninja exits 0 but its own output says the build
# failed. build-shell.sh must not trust the exit code alone.
run_case "autoninja exit 0 with failure text" 0 "The build has finished with an error." 1

# A build that both fails and reports it correctly.
run_case "autoninja exit 1 with failure text" 1 "2 errors generated" 1

# The regression this bug covers: pipefail+errexit must not abort the
# pipeline before build_status is read, even when autoninja prints nothing.
run_case "autoninja exit 1, no output" 1 "" 1

# The passing case, so the failure checks above are not just always-fail.
run_case "autoninja exit 0, no failure text" 0 "The build has finished successfully." 0

# one line of drift between the two atrium-core copies must stop the
# build before it starts, and say which command fixes it.
echo 'pub fn tint() -> u32 { 1 }' >"$COPY/lib.rs"
run_case "atrium-core drift stops the build" 0 "The build has finished successfully." 1 no
if ! grep -q "make rust-sync" "$WORK/out.log"; then
  echo "FAIL: atrium-core drift - the message does not name \`make rust-sync\`" >&2
  cat "$WORK/out.log" >&2
  exit 1
fi
echo "PASS: the drift message names make rust-sync"

# Sync the copies back, so the case above proves drift and not the fixture.
cp "$CANON/lib.rs" "$COPY/lib.rs"
run_case "synced atrium-core copies build" 0 "The build has finished successfully." 0

echo "build-shell-test.sh: all cases passed"
