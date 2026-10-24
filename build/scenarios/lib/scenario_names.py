#!/usr/bin/env python3
"""Parse the scenario-name list `all` runs against, and catch a truncation.

vm-scenario.sh's `all` branch used to do:

    IFS=' ' read -ra all_names <<<"$(list_scenarios)"

`list_scenarios` emits one scenario name PER LINE. `read` (unlike
`mapfile`/`readarray`) stops at the FIRST newline no matter what IFS is set
to, so that line silently collapsed a 57-name list to its first entry - a
full-suite run reported "batch summary: named 1, ran 1, skipped 0" and
"suite reached all 1 eligible scenario(s)", both truthfully, because nothing
downstream knew the input had been cut short.

The shell fix (a line-based `mapfile` instead of a here-string `read`) is not
itself testable without a VM. What IS testable, and is the general form of
the bug, is: does the number of names a source claims to have match the
number that got through? `count_mismatch` is that check, callable against any
future producer of a scenario-name list, not just this one code path.

CLI:
  scenario_names.py parse <file>            print one parsed name per line
  scenario_names.py check <parsed> <disk>   ok/mismatch for the two counts
"""
import sys


def parse_names(text):
    """Raw multi-line scenario-name text -> ordered list of non-empty names.

    One entry per LINE, matching what `list_scenarios` emits and what
    `mapfile -t` reads - not `text.split()`, which would fold on any
    whitespace and hide the very distinction (line vs token) that the bug
    ignored.
    """
    return [line.strip() for line in (text or "").splitlines() if line.strip()]


def count_mismatch(parsed_count, disk_count):
    """(ok: bool, message: str) - does a parsed name count look truncated
    against the number of scenario files actually on disk?

    The bug this guards produced a syntactically fine result (one valid
    name) that was semantically wrong (56 others silently dropped) - no
    check on individual names could have caught that, since the list wasn't
    wrong, it was short. A parsed count belonging to the FULL suite must
    never be smaller than the number of eligible files on disk; it is
    allowed to be larger only in the degenerate disk_count == 0 case.
    """
    if parsed_count >= disk_count:
        return True, "parsed %d name(s), %d eligible scenario file(s) on disk" % (
            parsed_count,
            disk_count,
        )
    return False, (
        "parsed only %d scenario name(s) but %d are eligible on disk - this "
        "looks like a truncated list, not a real subset; refusing to run a "
        "partial suite as if it were the whole one"
        % (parsed_count, disk_count)
    )


def main(argv):
    if len(argv) == 3 and argv[1] == "parse":
        with open(argv[2], encoding="utf-8") as handle:
            names = parse_names(handle.read())
        for name in names:
            print(name)
        return 0
    if len(argv) == 4 and argv[1] == "check":
        ok, message = count_mismatch(int(argv[2]), int(argv[3]))
        print(message)
        return 0 if ok else 1
    sys.stdout.write(__doc__ or "")
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
