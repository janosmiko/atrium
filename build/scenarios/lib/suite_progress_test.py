#!/usr/bin/env python3
"""Tests for suite_progress. No VM, no build: `python3 <file>`.

a full-suite run was SIGKILL'd mid-deploy on scenario 14 of 55, and
nothing in-process reported the other 41 as unreached - a killed process runs
no cleanup code, so an end-of-loop summary can only describe a run that got
to the end of its loop. The fix is a progress file written as the suite goes,
checked against the full eligible list after the fact. These tests cover the
comparison itself, independent of how the files get written or read.
"""
import os
import tempfile
import unittest

import suite_progress


class Missing(unittest.TestCase):
    def test_nothing_missing_when_every_eligible_name_started(self):
        gap = suite_progress.missing(["a", "b", "c"], ["a", "b", "c"])
        self.assertEqual(gap, [])

    def test_missing_preserves_eligible_order_not_started_order(self):
        gap = suite_progress.missing(["a", "b", "c"], ["c", "a"])
        self.assertEqual(gap, ["b"])

    def test_a_killed_run_leaves_everything_after_the_kill_missing(self):
        # scenario 14 of 55: the first 13 finished, the 14th started but the
        # process died mid-deploy, and 40 more never got a line written.
        eligible = [f"s{i}" for i in range(1, 56)]
        started = [f"s{i}" for i in range(1, 15)]
        gap = suite_progress.missing(eligible, started)
        self.assertEqual(len(gap), 41)
        self.assertEqual(gap[0], "s15")

    def test_empty_started_file_means_everything_is_missing(self):
        gap = suite_progress.missing(["a", "b"], [])
        self.assertEqual(gap, ["a", "b"])


class Verdict(unittest.TestCase):
    def test_a_complete_run_is_ok(self):
        ok, message = suite_progress.verdict(["a", "b"], ["a", "b"])
        self.assertTrue(ok)
        self.assertIn("all 2", message)

    def test_a_partial_run_is_not_ok(self):
        ok, message = suite_progress.verdict(["a", "b", "c"], ["a"])
        self.assertFalse(ok)
        self.assertIn("2 of 3", message)
        self.assertIn("b", message)
        self.assertIn("c", message)

    def test_a_partial_run_message_does_not_read_like_a_pass(self):
        _, message = suite_progress.verdict(["a", "b"], ["a"])
        self.assertIn("killed", message.lower())


class ParseNames(unittest.TestCase):
    def test_blank_lines_and_whitespace_are_dropped(self):
        names = suite_progress.parse_names("a\n\n  b  \n\n")
        self.assertEqual(names, ["a", "b"])

    def test_none_text_is_an_empty_list(self):
        self.assertEqual(suite_progress.parse_names(None), [])


class Cli(unittest.TestCase):
    """The thin file-reading CLI vm-scenario.sh actually shells out to."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def _write(self, name, content):
        path = os.path.join(self.tmp.name, name)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(content)
        return path

    def test_complete_run_exits_zero(self):
        eligible = self._write("eligible", "a\nb\n")
        started = self._write("started", "a\nb\n")
        rc = suite_progress.main(["prog", "verdict", eligible, started])
        self.assertEqual(rc, 0)

    def test_a_killed_run_exits_nonzero(self):
        eligible = self._write("eligible", "a\nb\nc\n")
        started = self._write("started", "a\n")
        rc = suite_progress.main(["prog", "verdict", eligible, started])
        self.assertEqual(rc, 1)

    def test_a_missing_started_file_is_a_run_that_never_wrote_anything(self):
        # The whole point: a process SIGKILL'd before its first scenario
        # started leaves no started-file at all, and that must still fail.
        eligible = self._write("eligible", "a\n")
        missing_started = os.path.join(self.tmp.name, "does-not-exist")
        rc = suite_progress.main(["prog", "verdict", eligible, missing_started])
        self.assertEqual(rc, 1)

    def test_wrong_arg_count_is_a_usage_error(self):
        rc = suite_progress.main(["prog", "verdict", "one-file-only"])
        self.assertEqual(rc, 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
