#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Install this repo's git hooks. Idempotent; run again after changing them.
#
# WHY A PRE-PUSH AND NOT A PRE-COMMIT. The patch series is regenerated FROM the
# Chromium commit stack, so the correct order is: commit in $SRC, run
# patches-regen, commit the refreshed patches here. A pre-commit hook would
# refuse the middle of that sequence every single time. A push is the first
# moment the series has to be right, because that is when it stops being local.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/../.." && pwd)"
HOOK="${REPO}/.git/hooks/pre-push"

cat > "${HOOK}" <<'HOOKEOF'
#!/usr/bin/env bash
# Installed by build/scripts/install-hooks.sh - edit that, not this.
#
# refuse a push that leaves the patch series behind the committed
# Chromium stack. The series drifted for a week because patches-verify is a make
# target nobody is obliged to run, so every individual commit looked fine while
# the recorded tree fell 15 patches behind.
set -euo pipefail
REPO="$(git rev-parse --show-toplevel)"
if [ -n "${ATRIUM_SKIP_PATCH_GUARD:-}" ]; then
  echo "pre-push: patch guard skipped (ATRIUM_SKIP_PATCH_GUARD set)" >&2
  exit 0
fi
if ! "${REPO}/build/scripts/verify-patches.sh" >/dev/null 2>&1; then
  echo "pre-push REFUSED: the patch series does not match the committed stack." >&2
  echo "  Run: make patches-regen  (then commit the refreshed patches)" >&2
  echo "  See why: make patches-verify" >&2
  echo "  To push anyway: ATRIUM_SKIP_PATCH_GUARD=1 git push ..." >&2
  exit 1
fi
exit 0
HOOKEOF

chmod +x "${HOOK}"
echo "installed: ${HOOK}"
