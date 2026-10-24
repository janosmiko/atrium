#!/usr/bin/env python3
"""Split a named batch into valid/invalid BEFORE any of it runs.

A gate run of four scenarios stopped at the second: one scrub-chord
scenario file had no assertions block, the runner rejected it correctly, and the
two scenarios behind it never ran - nothing said so. The fix has two parts:
validate every named scenario up front and report every invalid one together
(not one at a time, not first-fail-wins), then run the valid ones and name
the ones skipped, so the batch's own summary says how many were named, ran,
and skipped.

vm-scenario.sh does the actual validation (it already shells out to parse.py
per scenario, which needs the real JSON files); this module is the pure
bookkeeping on top of that result - which names are valid, which are
skipped, and the summary line - so that bookkeeping is unit-testable without
a VM or a scenario file in sight.

CLI (bash writes the two input files, this does the ordering/reporting):
  batch_validation.py valid <names-file> <errors-file>
      one valid scenario name per line, in input order
  batch_validation.py invalid-report <names-file> <errors-file>
      one "<name>: <error>" line per invalid scenario
  batch_validation.py summary <named> <ran> <skipped>
      the one line the batch's own summary must carry

<names-file>: one scenario name per line, the whole named batch, in order.
<errors-file>: TSV, one row per INVALID name: "<name>\t<error text>". A name
with no row in this file is valid.
"""
import sys


def split(names, errors_by_name):
    """names: the batch, in the order given.
    errors_by_name: {name: error text} for every INVALID name; a name absent
    from this dict is valid.

    Returns (valid, invalid) as ordered lists; invalid holds (name, error)
    pairs, both preserving the input order.
    """
    valid = [name for name in names if name not in errors_by_name]
    invalid = [(name, errors_by_name[name]) for name in names if name in errors_by_name]
    return valid, invalid


def summary_line(named, ran, skipped):
    """The one line a batch's own report must carry."""
    return "named %d, ran %d, skipped %d" % (named, ran, skipped)


def invalid_report(invalid):
    """One line per invalid scenario, name plus its validation error."""
    return ["%s: %s" % (name, error) for name, error in invalid]


def _read_names(path):
    with open(path, encoding="utf-8") as handle:
        return [line.strip() for line in handle if line.strip()]


def _read_errors(path):
    errors = {}
    try:
        with open(path, encoding="utf-8") as handle:
            for line in handle:
                if not line.strip():
                    continue
                name, _, error = line.rstrip("\n").partition("\t")
                errors[name] = error
    except OSError:
        return errors
    return errors


def main(argv):
    if len(argv) >= 2 and argv[1] == "summary" and len(argv) == 5:
        named, ran, skipped = int(argv[2]), int(argv[3]), int(argv[4])
        print(summary_line(named, ran, skipped))
        return 0
    if len(argv) == 4 and argv[1] in ("valid", "invalid-report"):
        names = _read_names(argv[2])
        errors = _read_errors(argv[3])
        valid, invalid = split(names, errors)
        if argv[1] == "valid":
            for name in valid:
                print(name)
        else:
            for line in invalid_report(invalid):
                print(line)
        return 0
    sys.stdout.write(__doc__ or "")
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
