#!/usr/bin/env python3
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
"""Partition the fork's diff against Chromium into overlay, patch, and foreign.

A path is overlay if, and only if, it is absent from chromium_commit AND
sits under an owned root.
Everything else is patch (present in the base) or foreign (new, but outside
every owned root - a file the partition rule does not cover).

    ./build/scripts/classify-overlay.py
"""

import os
import subprocess
import sys

ROOTS = (
    "chrome/browser/ui/atrium",
    "chrome/browser/resources/atrium",
    "chrome/browser/ui/webui/atrium",
    "third_party/atrium",
)

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.expanduser(os.environ.get("ATRIUM_CHROMIUM_SRC", "~/chromium-atrium/src"))


def _under_owned_root(path, owned_roots):
    return any(path == root or path.startswith(root + "/") for root in owned_roots)


def classify_path(path, base_files, owned_roots):
    if path in base_files:
        return "patch"
    if _under_owned_root(path, owned_roots):
        return "overlay"
    return "foreign"


def _read_version_key(key):
    with open(os.path.join(REPO, "build/VERSION"), encoding="utf-8") as f:
        for line in f:
            if line.startswith(key + "="):
                return line.strip().split("=", 1)[1]
    raise KeyError(f"{key} not found in build/VERSION")


def _diff_entries(src, base_commit, branch):
    out = subprocess.run(
        ["git", "-C", src, "diff", "--name-status", base_commit, branch],
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    entries = []
    for line in out.splitlines():
        if not line:
            continue
        status, path = line.split("\t", 1)
        entries.append((status, path))
    return entries


def classify_diff(src, base_commit, branch, owned_roots=ROOTS):
    entries = _diff_entries(src, base_commit, branch)
    base_files = {path for status, path in entries if not status.startswith("A")}
    results = {"overlay": [], "patch": [], "foreign": []}
    for _status, path in entries:
        results[classify_path(path, base_files, owned_roots)].append(path)
    return results


def main(argv):
    base_commit = _read_version_key("chromium_commit")
    branch = f"atrium/{_read_version_key('chromium_tag')}"

    results = classify_diff(SRC, base_commit, branch)
    for bucket in ("overlay", "patch", "foreign"):
        print(f"{bucket}: {len(results[bucket])}")

    if results["foreign"]:
        print("foreign paths (outside every owned root):", file=sys.stderr)
        for path in sorted(results["foreign"]):
            print(f"  {path}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
