#!/usr/bin/env python3
"""Did the suite reach every scenario it named?

A full-suite run on 2026-08-10 was killed mid-deploy on scenario 14 of 55.
Nothing in-process could report that: SIGKILL gives a process no chance to run
cleanup code, trap handlers, or a final summary line. The only way to notice a
kill after the fact is a progress record written to disk BEFORE it can be
lost - vm-scenario.sh appends one line per scenario to a progress file the
instant that scenario STARTS, so a comparison against the full eligible list
can find the gap even when the process that would have printed "41 of 55
never ran" is the process that got killed.

This module is the pure comparison: given the list of scenarios the suite was
asked to run and the list it actually got to, say whether they match and
build the evidence line for the ones that did not.

CLI:
  suite_progress.py verdict <eligible-file> <started-file>

Each file holds one scenario name per line (started-file may be empty/absent
- a suite killed before writing anything). Exit 0 and prints "complete" if
every eligible name appears in started; exit 1 and prints the gap otherwise.
"""
import sys


def parse_names(text):
    """Raw file content -> ordered list of non-empty scenario names."""
    return [line.strip() for line in (text or "").splitlines() if line.strip()]


def missing(eligible, started):
    """Eligible names that never appear in started, in eligible's order."""
    seen = set(started)
    return [name for name in eligible if name not in seen]


def verdict(eligible, started):
    """(ok: bool, message: str) - did the suite reach every eligible scenario."""
    gap = missing(eligible, started)
    if not gap:
        return True, "suite reached all %d eligible scenario(s)" % len(eligible)
    return False, (
        "suite did not reach %d of %d eligible scenario(s): %s - the run "
        "either was killed before starting them or exited early; a partial "
        "run must not be reported as a finished one"
        % (len(gap), len(eligible), ", ".join(gap))
    )


def _read(path):
    try:
        with open(path, encoding="utf-8") as handle:
            return handle.read()
    except OSError:
        return ""


def main(argv):
    if len(argv) != 4 or argv[1] != "verdict":
        sys.stdout.write(__doc__ or "")
        return 2
    eligible = parse_names(_read(argv[2]))
    started = parse_names(_read(argv[3]))
    ok, message = verdict(eligible, started)
    print(message)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
