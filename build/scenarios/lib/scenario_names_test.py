#!/usr/bin/env python3
"""Tests for scenario_names - the truncation the `all` branch had.

Reproduces the exact shape of the bug: `list_scenarios` output is one name
per line, and a consumer that reads it as a single line drops everything
after the first. `parse_names` must not repeat that mistake, and
`count_mismatch` must catch it even if a future rewrite reintroduces it by
some other means.
"""
import os
import tempfile
import unittest

import scenario_names


class ParseNames(unittest.TestCase):
    def test_reads_every_line_not_just_the_first(self):
        # This is the bug, reproduced directly: 57 one-per-line names must
        # come back as 57 names, not 1.
        text = "\n".join("scenario-%02d" % i for i in range(1, 58)) + "\n"
        names = scenario_names.parse_names(text)
        self.assertEqual(len(names), 57)
        self.assertEqual(names[0], "scenario-01")
        self.assertEqual(names[-1], "scenario-57")

    def test_blank_lines_are_dropped(self):
        names = scenario_names.parse_names("a\n\n  b  \n\n")
        self.assertEqual(names, ["a", "b"])

    def test_none_text_is_an_empty_list(self):
        self.assertEqual(scenario_names.parse_names(None), [])

    def test_single_name_stays_a_single_name(self):
        # The degenerate case that made the bug's output look plausible: one
        # name in, one name out is correct here, so the bug's symptom (also
        # one name) could pass an eyeball check.
        self.assertEqual(scenario_names.parse_names("only-one\n"), ["only-one"])


class CountMismatch(unittest.TestCase):
    def test_full_count_matching_disk_is_ok(self):
        ok, message = scenario_names.count_mismatch(57, 57)
        self.assertTrue(ok)
        self.assertIn("57", message)

    def test_collapsed_to_one_is_a_mismatch(self):
        # The actual incident: 57 files on disk, 1 name parsed.
        ok, message = scenario_names.count_mismatch(1, 57)
        self.assertFalse(ok)
        self.assertIn("1", message)
        self.assertIn("57", message)
        self.assertIn("truncated", message)

    def test_parsed_count_may_exceed_disk_count(self):
        # e.g. explicit names repeated on the command line - not this
        # module's job to reject, just not to call it a mismatch.
        ok, _ = scenario_names.count_mismatch(3, 2)
        self.assertTrue(ok)

    def test_zero_and_zero_is_ok(self):
        ok, _ = scenario_names.count_mismatch(0, 0)
        self.assertTrue(ok)


class Cli(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def _write(self, name, content):
        path = os.path.join(self.tmp.name, name)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(content)
        return path

    def test_parse_cli_prints_one_name_per_line(self):
        path = self._write("names", "a\nb\nc\n")
        import contextlib
        import io

        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = scenario_names.main(["prog", "parse", path])
        self.assertEqual(rc, 0)
        self.assertEqual(buf.getvalue().splitlines(), ["a", "b", "c"])

    def test_check_cli_exits_nonzero_on_mismatch(self):
        rc = scenario_names.main(["prog", "check", "1", "57"])
        self.assertEqual(rc, 1)

    def test_check_cli_exits_zero_when_matched(self):
        rc = scenario_names.main(["prog", "check", "57", "57"])
        self.assertEqual(rc, 0)

    def test_wrong_arg_count_is_a_usage_error(self):
        rc = scenario_names.main(["prog", "parse"])
        self.assertEqual(rc, 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
