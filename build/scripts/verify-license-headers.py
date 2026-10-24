#!/usr/bin/env python3
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
"""Drift guard: every Atrium-authored source file carries the Apache-2.0 header.

Atrium is a BSD-3 base with an Apache-2.0 layer on top (docs/LICENSING.md draws
the boundary path by path). That arrangement is legally clean, but only while
a reader can tell which license governs the file in front of them. A file with
no header, or with the wrong one, reads as BSD-3 by inheritance from its
neighbours - which is a relicensing claim nobody made.

Two checks:

  A. Every Atrium-authored file under the repo roots below opens with the
     header (after a shebang, if it has one).
  B. Every wholly new file the patch series adds to the Chromium checkout
     under an Atrium-only path carries it too. 200 of those still carry the
     Chromium BSD-3 header they were copied from, so they sit in the
     allowlist with the task that owns the fix.

The allowlist expires on contact, the way the unwired-paths one does: an
allowlisted file that GAINS the correct header fails until its entry is
removed. An allowlist that stays true after the fact is how a stale entry
outlives the fact underneath it.

  ./build/scripts/verify-license-headers.py            # check
  ./build/scripts/verify-license-headers.py --fix      # write missing headers
  ./build/scripts/verify-license-headers.py --selftest # prove it can fail
"""

import json
import os
import shutil
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.expanduser(os.environ.get("ATRIUM_CHROMIUM_SRC", "~/chromium-atrium/src"))
ALLOWLIST_PATH = os.path.join(REPO, "build/scripts/license-header-allowlist.json")

HEADER_LINES = [
    "Copyright 2026 The Atrium Authors",
    "Use of this source code is governed by the Apache License, Version 2.0",
    "found in the LICENSE file.",
]
# Matched on the first line alone, so rewording the two lines under it later
# does not read as 174 missing headers.
MARKER = HEADER_LINES[0]
CHROMIUM_MARKER = "The Chromium Authors"

# Extension -> line-comment token, or BLOCK for a language that has no line
# comment. A file type with neither (JSON, Markdown, plain data) is not in
# scope, because there is nowhere to put a header.
BLOCK = "/*"
COMMENT = {
    ".rs": "//",
    ".mjs": "//",
    ".js": "//",
    ".ts": "//",
    ".cc": "//",
    ".h": "//",
    ".mm": "//",
    ".swift": "//",
    ".css": BLOCK,
    ".py": "#",
    ".sh": "#",
    ".gn": "#",
    ".gni": "#",
    ".toml": "#",
}

# Repo directories whose files are wholly Atrium-authored.
REPO_ROOTS = ["rust", "build/scripts"]
# Atrium-only paths inside the Chromium checkout: these files did not exist
# upstream, so BSD-3 never governed them.
SRC_ROOTS = [
    "chrome/browser/ui/atrium",
    "chrome/browser/ui/webui/atrium",
    "chrome/browser/resources/atrium",
]
SKIP_DIRS = {"target", "node_modules", "__pycache__", ".git"}


def load_allowlist():
    if not os.path.isfile(ALLOWLIST_PATH):
        return {}
    with open(ALLOWLIST_PATH, encoding="utf-8") as f:
        return {entry["path"]: entry["reason"] for entry in json.load(f)["files"]}


def in_scope(path):
    return os.path.splitext(path)[1] in COMMENT


def walk(root):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for name in filenames:
            full = os.path.join(dirpath, name)
            if in_scope(full):
                yield full


def collect():
    """(display path, absolute path) for every file the guard governs."""
    for root in REPO_ROOTS:
        base = os.path.join(REPO, root)
        if os.path.isdir(base):
            for full in walk(base):
                yield os.path.relpath(full, REPO), full
    for root in SRC_ROOTS:
        base = os.path.join(SRC, root)
        if os.path.isdir(base):
            for full in walk(base):
                yield os.path.join("$SRC", root, os.path.relpath(full, base)), full


def has_header(text):
    # Only the opening lines count. A file mentioning the marker halfway down
    # (this script, the allowlist reason strings) has not thereby licensed
    # itself.
    return MARKER in "".join(text.splitlines(keepends=True)[:6])


BSL_HEADER = (
    "// Copyright 2026 The Atrium Authors\n"
    "// Use of this source code is governed by the Business Source License 1.1\n"
    "// found in the LICENSE file. This is not an open source license.\n"
)


def is_stale(text):
    return "Business Source License" in "".join(text.splitlines(keepends=True)[:6])


def header_block(path):
    token = COMMENT[os.path.splitext(path)[1]]
    if token == BLOCK:
        body = "\n".join(f" * {line}" for line in HEADER_LINES)
        return f"/*\n{body}\n */\n"
    return "".join(f"{token} {line}\n" for line in HEADER_LINES)


def apply_header(path):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    if has_header(text):
        return False
    lines = text.splitlines(keepends=True)
    # A shebang must stay on line 1 or the kernel never sees it.
    at = 1 if lines and lines[0].startswith("#!") else 0
    lines.insert(at, header_block(path))
    with open(path, "w", encoding="utf-8") as f:
        f.write("".join(lines))
    return True


def check(allowlist, quiet=False):
    missing, stale, wrong, old = [], [], [], []
    for display, full in collect():
        try:
            with open(full, encoding="utf-8") as f:
                text = f.read()
        except (OSError, UnicodeDecodeError):
            continue
        if is_stale(text):
            old.append(display)
            continue
        ours = has_header(text)
        excused = display in allowlist
        if ours and excused:
            stale.append(display)
        elif not ours and not excused:
            (wrong if CHROMIUM_MARKER in text[:400] else missing).append(display)
    if not quiet:
        for path in missing:
            print(f"FAIL: no license header: {path}")
        for path in wrong:
            print(f"FAIL: carries the Chromium BSD-3 header, not Atrium's: {path}")
        for path in old:
            print(f"FAIL: still carries the Business Source License header: {path}")
        for path in stale:
            print(f"FAIL: header is now correct, so drop the allowlist entry: {path}")
    return missing + wrong + stale + old


def selftest():
    """Prove the guard can fail: a headered file that loses its header, and an
    allowlisted file that gains one. A guard nobody has seen go red is a guard
    nobody knows is wired up."""
    with tempfile.TemporaryDirectory() as tmp:
        victim = os.path.join(tmp, "victim.rs")
        shutil.copyfile(os.path.join(REPO, "build/scripts/dom-shim.mjs"),
                        os.path.join(tmp, "sample.mjs"))
        with open(victim, "w", encoding="utf-8") as f:
            f.write("fn main() {}\n")
        with open(victim, encoding="utf-8") as f:
            text = f.read()
        if has_header(text):
            print("SELFTEST FAILED: a file with no header read as headered")
            return 1
        apply_header(victim)
        with open(victim, encoding="utf-8") as f:
            victim_text = f.read()
        if not has_header(victim_text):
            print("SELFTEST FAILED: --fix wrote a header the check cannot see")
            return 1
        if "Apache License, Version 2.0" not in victim_text:
            print("SELFTEST FAILED: --fix wrote a header that is not Apache-2.0")
            return 1
        stale = os.path.join(tmp, "stale.rs")
        with open(stale, "w", encoding="utf-8") as f:
            f.write(BSL_HEADER + "fn main() {}\n")
        with open(stale, encoding="utf-8") as f:
            stale_text = f.read()
        if not is_stale(stale_text):
            print("SELFTEST FAILED: a leftover BSL header read as current")
            return 1
        shebang = os.path.join(tmp, "tool.sh")
        with open(shebang, "w", encoding="utf-8") as f:
            f.write("#!/usr/bin/env bash\nset -euo pipefail\n")
        apply_header(shebang)
        with open(shebang, encoding="utf-8") as f:
            shebang_text = f.read()
        if not shebang_text.startswith("#!"):
            print("SELFTEST FAILED: the header displaced a shebang")
            return 1
    print("selftest OK: a missing header fails, --fix satisfies it, "
          "and a shebang survives")
    return 0


def main():
    if "--selftest" in sys.argv:
        return selftest()
    allowlist = load_allowlist()
    if "--fix" in sys.argv:
        written = [d for d, full in collect()
                   if d not in allowlist and apply_header(full)]
        for path in written:
            print(f"wrote header: {path}")
        print(f"--fix: {len(written)} file(s) written")
    failures = check(allowlist)
    if failures:
        print(f"\nFAIL: {len(failures)} file(s); see docs/LICENSING.md for the "
              "Apache-2.0/BSD-3 boundary")
        return 1
    total = sum(1 for _ in collect())
    print(f"verify OK: {total} Atrium-authored file(s) carry the Apache-2.0 header "
          f"({len(allowlist)} allowlisted)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
