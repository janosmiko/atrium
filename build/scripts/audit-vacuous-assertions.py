#!/usr/bin/env python3
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
"""Find scenario assertions that would pass against an app that did nothing.

Two assertions were passing on lines Atrium emits at LAUNCH. One of
them, devtools-close-while-docked's "devtools-undocked", reported PASS for the
whole period that DevTools was completely dead - the scenario could not
have failed if it tried.

THE METHOD. idle-survival launches, waits for a page to commit, idles three
minutes and quits. Anything in its run log is emitted regardless of what a
scenario does, so any vlog pattern matching it is suspect.

Position in the log is judged by TIMESTAMP, not by line number. Startup emits
most of the log, so a teardown line can sit at 38% by line and 99% by time -
which is exactly how the first version of this audit produced two false
positives on `tab N closed`.

WHAT IT CANNOT TELL YOU, so read the output rather than trusting the count:

  - Assertions listed BEFORE `quit` are evaluated against a log pulled before
    teardown, so a control match in the teardown window is harmless. Those are
    reported as LATE and are almost always fine.
  - The control runs ONE fixture. A pattern naming a URL that happens to be the
    control's startup tab may be perfectly sound in its own scenario, where the
    same URL belongs to a space only reachable by switching. presenter-startup's
    space-switched is exactly that case.

So this narrows the field to a handful worth reading. It does not judge them.

    python3 build/scripts/audit-vacuous-assertions.py
    python3 build/scripts/audit-vacuous-assertions.py path/to/some/atrium-run.log
"""
import json
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
SCENARIOS = REPO / "build" / "scenarios"
RESULTS = REPO / "build" / "vm-fixtures" / "results"
# Chromium's log prefix carries MMDD/HHMMSS; the Swift NSLog lines carry a full
# timestamp. Either is enough to order a line within one run.
STAMP = re.compile(r"\b\d{4}/(\d{2})(\d{2})(\d{2})\b|\b(\d{2}):(\d{2}):(\d{2})\.")


def seconds(line):
    m = STAMP.search(line)
    if not m:
        return None
    h, mi, s = (m.group(1), m.group(2), m.group(3)) if m.group(1) else (m.group(4), m.group(5), m.group(6))
    return int(h) * 3600 + int(mi) * 60 + int(s)


def newest_control():
    runs = sorted(RESULTS.glob("idle-survival-*/atrium-run.log"))
    if not runs:
        sys.exit("no idle-survival run found - run `build/scripts/vm-scenario.sh idle-survival` first")
    return runs[-1]


def main():
    control = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else newest_control()
    lines = control.read_text(errors="ignore").splitlines()
    stamps = [(i, seconds(line), line) for i, line in enumerate(lines)]
    known = [s for _, s, _ in stamps if s is not None]
    if not known:
        sys.exit(f"no timestamps found in {control}")
    start, end = known[0], known[-1]
    span = max(1, end - start)

    print(f"control: {control}")
    print(f"span: {span}s over {len(lines)} lines\n")

    findings = 0
    for spec in sorted(SCENARIOS.glob("*.json")):
        scenario = json.loads(spec.read_text())
        for assertion in scenario.get("assertions", []):
            if assertion.get("type") not in ("vlog", "vlog_after"):
                continue
            pattern = assertion.get("pattern", "")
            try:
                rx = re.compile(pattern)
            except re.error:
                continue
            hit = next(((i, s, line) for i, s, line in stamps if s is not None and rx.search(line)), None)
            if not hit:
                continue
            # A vlog_after is guarded by its ordering requirement, not by its
            # pattern. Only flag one when the EARLIER pattern also appears in a
            # control run that did nothing - otherwise the sequence cannot be
            # satisfied by startup noise, which is the whole point of that type.
            if assertion.get("type") == "vlog_after":
                try:
                    earlier = re.compile(assertion.get("after_pattern", ""))
                except re.error:
                    continue
                if not any(earlier.search(line) for _, s2, line in stamps if s2 is not None):
                    continue
            _, when, _ = hit
            pct = 100 * (when - start) // span
            verdict = "STARTUP - would pass against an app that did nothing" if pct <= 25 else \
                      "late in the run, probably teardown - read before acting"
            findings += 1
            print(f"{spec.stem}")
            print(f"  label:   {assertion.get('label', '?')}")
            print(f"  pattern: {pattern}")
            print(f"  control match at {pct}% of the run: {verdict}\n")

    print(f"{findings} assertion(s) matched the control. Zero is the goal; a small "
          f"number is normal and needs reading, not fixing.")


if __name__ == "__main__":
    main()
