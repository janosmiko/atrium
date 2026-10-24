#!/usr/bin/env python3
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
"""Injected JS must survive a document that has no root element yet.

C++ pushes appearance into a surface by executing a script in it. A push can
land before the document commits, and `document.documentElement` is null then,
so an unguarded `.style` or `.classList` throws. The console reports those with
`source: (1)` and no file, which is why one such site went unnoticed long
enough to write 37 errors into a single scenario run.

The guard is an optional chain. Nothing is lost by it: there is no document to
receive the value at that moment either way.
"""

import os
import pathlib
import re
import sys

# The same resolution verify-license-headers.py uses. Deriving it from this
# file's own location instead pointed at a directory that does not exist, and
# the guard then passed vacuously with a real violation in the tree.
SRC = pathlib.Path(
    os.path.expanduser(
        os.environ.get("ATRIUM_CHROMIUM_SRC", "~/chromium-atrium/src")))

SRC_ROOTS = [
    "chrome/browser/ui/atrium",
    "chrome/browser/ui/webui/atrium",
    "chrome/browser/resources/atrium",
]

# The WebUI modules in that resources directory always run against a committed
# document, so only the scripts C++ injects into arbitrary pages are scanned.
INJECTED_JS = {"vim-page.js", "vim-grammar.js", "vim-popup.js"}

# `documentElement` followed by a plain dot. The optional chain is the fix, so
# `documentElement?.` is what a compliant site looks like.
UNGUARDED = re.compile(r"documentElement\.(style|classList)\b")


def offenders(root: pathlib.Path):
    for src_root in SRC_ROOTS:
        base = root / src_root
        if not base.is_dir():
            continue
        for path in sorted(base.iterdir()):
            if path.suffix not in (".cc", ".mm", ".h", ".js"):
                continue
            if path.suffix == ".js" and path.name not in INJECTED_JS:
                continue
            # A browsertest reads a document it has already waited for.
            if "browsertest" in path.name or "unittest" in path.name:
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            for number, line in enumerate(text.splitlines(), 1):
                if UNGUARDED.search(line):
                    yield f"{path.relative_to(root)}:{number}: {line.strip()}"


def main() -> int:
    if not SRC.is_dir():
        print(f"FAIL: no Chromium checkout at {SRC}. Set ATRIUM_CHROMIUM_SRC.",
              file=sys.stderr)
        return 1
    scanned = [d for d in SRC_ROOTS if (SRC / d).is_dir()]
    if not scanned:
        print(f"FAIL: none of {SRC_ROOTS} exist under {SRC}, so this guard "
              "would pass without reading anything.", file=sys.stderr)
        return 1
    found = list(offenders(SRC))
    if found:
        print("FAIL: injected JS reads documentElement without an optional "
              "chain. A push before the document commits throws there:",
              file=sys.stderr)
        for hit in found:
            print(f"  {hit}", file=sys.stderr)
        print("  Write documentElement?.style / documentElement?.classList.",
              file=sys.stderr)
        return 1
    print(f"verify OK: every injected documentElement read is optional "
          f"({len(scanned)} root(s) scanned)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
