#!/usr/bin/env python3
"""Read one value out of a staged JSON file and grade it.

Backs the `file_contains` assertion. The runner cats the file out of the VM and
hands the bytes here with the host's copy of what was STAGED, so the comparison
happens where both halves are, next to the artifacts a dispute would be settled
from - the same division of labour as pick_renderer.py.

WHY A JSON PATH AND A TYPED VALUE, rather than a substring or a whole-file
match. A persistence assertion has exactly one job: separate a file that was
written from a repaint that only looked like it. All three candidates read the
file, and the other two lie in a different direction each.

  A SUBSTRING passes on bytes that are not the value it names. `"schemaVersion":
  3` appears in a file whose real setting is 30. `true` matches any of the other
  booleans in the same object. And it fails on a file that IS correct but
  formatted differently - the writer here is serde, the fixture is
  hand-written, and `{"a": 1}` vs `{"a":1}` is a difference no persistence
  question is about.

  A WHOLE-FILE match fails for that same formatting reason, plus every key the
  scenario never mentioned. The settings file carries a dozen fields; pinning
  all of them to prove one was written means the assertion goes red the day an
  unrelated default changes, which is how a gate stops being read.

  A JSON PATH names the one value in question and compares it AS JSON, so `true`
  is not `"true"` and 1 is not `"1"`. Those are exactly the confusions a
  round-trip through a serializer produces, so an assertion that cannot see them
  is not checking the round trip.

AND THE BASELINE, which is the part that makes it honest. The value being there
proves nothing if the staged fixture already held it - the assertion would pass
against a build with no writer at all, which is the failure this whole task
exists to stop. So a baseline that already matches is a FAILED assertion, not a
passed one, and the message says the scenario is the thing to fix. Same rule
asserts.sh already applies to a byte-identical before/after frame.

CLI:
  file_value.py <actual-file> <baseline-file> <dotted-path> <expected-json>

Exit codes: 0 match, 3 value differs, 4 path absent, 5 the baseline already
held it, 1 unusable input. One evidence line on stdout either way.
"""
import json
import sys

# A path segment that is all digits indexes a list; anything else is a dict key.
# A key with a literal '.' in it is therefore unreachable - no document this
# harness stages has one, and inventing an escape syntax for a case that does
# not occur costs more than it buys.
MISSING = object()


def select(doc, path):
    """The value at a dotted path, or MISSING. Also returns how far it got."""
    node = doc
    walked = []
    for segment in path.split("."):
        if isinstance(node, list) and segment.isdigit():
            index = int(segment)
            if index >= len(node):
                return MISSING, ".".join(walked)
            node = node[index]
        elif isinstance(node, dict) and segment in node:
            node = node[segment]
        else:
            return MISSING, ".".join(walked)
        walked.append(segment)
    return node, ".".join(walked)


def _render(value):
    return "<absent>" if value is MISSING else json.dumps(value)


def check(actual_text, baseline_text, path, expected_json):
    """(exit code, evidence line). See the module doc for what each code means."""
    try:
        expected = json.loads(expected_json)
    except ValueError as exc:
        return 1, "expected value %r is not JSON: %s" % (expected_json, exc)
    if baseline_text is None:
        return 1, ("no staged copy of this file on the host, so there is no way "
                   "to tell a write from the fixture it started as")
    try:
        baseline = json.loads(baseline_text)
    except ValueError as exc:
        return 1, "the STAGED copy is not JSON: %s" % exc
    try:
        actual = json.loads(actual_text)
    except ValueError as exc:
        return 1, "the file read back from the VM is not JSON: %s" % exc

    staged, _ = select(baseline, path)
    if staged is not MISSING and staged == expected and type(staged) is type(expected):
        return 5, ("the STAGED fixture already held %s at %s, so this assertion "
                   "would pass against a build that never wrote anything - stage "
                   "a different value" % (_render(expected), path))

    found, reached = select(actual, path)
    if found is MISSING:
        return 4, ("no %s in the file (walked as far as %r); staged value was %s"
                   % (path, reached, _render(staged)))
    # Type before value: json.loads gives True == 1, and a settings round trip
    # that turned a bool into a number is precisely a write worth failing on.
    if type(found) is not type(expected) or found != expected:
        return 3, ("%s is %s, expected %s (staged: %s)"
                   % (path, _render(found), _render(expected), _render(staged)))
    return 0, ("%s is %s, and the staged fixture held %s - so this value was "
               "written" % (path, _render(found), _render(staged)))


def _read(path):
    try:
        with open(path, encoding="utf-8") as handle:
            return handle.read()
    except OSError:
        return None


def main(argv):
    if len(argv) != 5:
        sys.stdout.write(__doc__)
        return 1
    actual_path, baseline_path, path, expected_json = argv[1:]
    actual = _read(actual_path)
    if actual is None:
        print("could not read the file pulled from the VM: %s" % actual_path)
        return 1
    code, evidence = check(actual, _read(baseline_path), path, expected_json)
    print(evidence)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
