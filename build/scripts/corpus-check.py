#!/usr/bin/env python3
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
"""Run the golden store corpus against any store implementation.

The Swift suite has its own runner (GoldenStoreCorpusTests), which
is the one `make test` uses. This one exists so a NON-Swift store can be held
to the same files. It knows nothing about Swift, Rust, or JSON schemas - it
copies an input, hands the copy to a command, and compares what comes back to
the expectation.

The command is called once per case:

    <command> <store-name> <path-to-a-writable-copy-of-the-input>

It must load that file through the named store and write the loaded state back
to the same path. Anything else about how it works is its own business.

    ./build/scripts/corpus-check.py --roundtrip ./my-runner
    ./build/scripts/corpus-check.py --roundtrip cat --expect-fail   # see it discriminate

Exit code 0 when every case matches, 1 otherwise.
"""

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

CORPUS = Path(__file__).resolve().parent.parent.parent / "fixtures" / "stores"


def load_manifest():
    with open(CORPUS / "manifest.json") as f:
        return json.load(f)["cases"]


def run_case(case, command, workdir):
    src = CORPUS / case["input"]
    want_path = CORPUS / case["expected"]
    working = workdir / Path(case["input"]).name
    shutil.copyfile(src, working)

    result = subprocess.run(
        command + [case["store"], str(working)],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        return False, f"the runner exited {result.returncode}: {result.stderr.strip()}"

    try:
        with open(working) as f:
            got = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        return False, f"the runner left something that is not JSON: {e}"

    with open(want_path) as f:
        want = json.load(f)

    if got == want:
        return True, ""
    return False, "the state does not match the expectation"


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--roundtrip", required=True,
                   help="command that loads a store file and writes it back")
    p.add_argument("--expect-fail", action="store_true",
                   help="invert the exit code, for proving the check discriminates")
    args = p.parse_args()

    command = args.roundtrip.split()
    cases = load_manifest()
    failures = []

    with tempfile.TemporaryDirectory() as tmp:
        workdir = Path(tmp)
        for case in cases:
            ok, why = run_case(case, command, workdir)
            mark = "ok  " if ok else "FAIL"
            print(f"{mark} {case['store']:<30} {case['input']}")
            if not ok:
                print(f"     {why}")
                failures.append(case["input"])

    print(f"\n{len(cases) - len(failures)}/{len(cases)} cases matched")

    failed = bool(failures)
    if args.expect_fail:
        if failed:
            print("expected failures, and got them")
            return 0
        print("expected failures, and every case passed", file=sys.stderr)
        return 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
