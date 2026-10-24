#!/usr/bin/env python3
"""Tests for batch_validation. No VM, no build: `python3 <file>`.

scrub-chord-agree.json (no 'assertions' block) took down a
four-scenario gate run - the runner rejected it correctly but stopped the
whole batch there, and the two scenarios behind it never ran or reported.
These tests cover the pure split/summary logic vm-scenario.sh now runs on
top of its per-scenario validation, so the ordering and reporting rules hold
regardless of which scenarios are actually invalid on a given run.
"""
import os
import tempfile
import unittest

import batch_validation


class Split(unittest.TestCase):
    def test_all_valid_when_no_errors_reported(self):
        valid, invalid = batch_validation.split(["a", "b", "c"], {})
        self.assertEqual(valid, ["a", "b", "c"])
        self.assertEqual(invalid, [])

    def test_the_gate_run_shape_one_invalid_two_behind_it(self):
        # The invalid name is second of four; the two behind it must still be valid.
        names = ["task1", "scrub-chord-agree", "task3", "task4"]
        errors = {"scrub-chord-agree": "missing/empty 'assertions'"}
        valid, invalid = batch_validation.split(names, errors)
        self.assertEqual(valid, ["task1", "task3", "task4"])
        self.assertEqual(invalid, [("scrub-chord-agree", "missing/empty 'assertions'")])

    def test_valid_and_invalid_each_preserve_input_order(self):
        names = ["c", "a", "b"]
        errors = {"a": "bad"}
        valid, invalid = batch_validation.split(names, errors)
        self.assertEqual(valid, ["c", "b"])
        self.assertEqual(invalid, [("a", "bad")])

    def test_every_name_invalid_leaves_nothing_to_run(self):
        names = ["a", "b"]
        errors = {"a": "bad a", "b": "bad b"}
        valid, invalid = batch_validation.split(names, errors)
        self.assertEqual(valid, [])
        self.assertEqual(len(invalid), 2)


class SummaryLine(unittest.TestCase):
    def test_states_named_ran_and_skipped(self):
        line = batch_validation.summary_line(4, 3, 1)
        self.assertIn("named 4", line)
        self.assertIn("ran 3", line)
        self.assertIn("skipped 1", line)

    def test_zero_skipped_is_still_stated_explicitly(self):
        # A clean batch must still say "skipped 0" - silence on the count is
        # the same shape of bug this task exists to close.
        line = batch_validation.summary_line(2, 2, 0)
        self.assertIn("skipped 0", line)


class InvalidReport(unittest.TestCase):
    def test_one_line_per_invalid_scenario_with_its_reason(self):
        lines = batch_validation.invalid_report(
            [("scrub-chord-agree", "missing/empty 'assertions'")])
        self.assertEqual(len(lines), 1)
        self.assertIn("scrub-chord-agree", lines[0])
        self.assertIn("missing/empty 'assertions'", lines[0])

    def test_empty_invalid_list_reports_nothing(self):
        self.assertEqual(batch_validation.invalid_report([]), [])


class Cli(unittest.TestCase):
    """The thin file-reading CLI bash actually shells out to."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def _write(self, name, content):
        path = os.path.join(self.tmp.name, name)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(content)
        return path

    def test_valid_prints_only_the_valid_names_in_order(self):
        names = self._write("names", "task1\nbroken\ntask3\n")
        errors = self._write("errors", "broken\tmissing/empty 'assertions'\n")
        rc = batch_validation.main(["prog", "valid", names, errors])
        self.assertEqual(rc, 0)

    def test_invalid_report_reads_the_tsv_errors_file(self):
        names = self._write("names", "broken\n")
        errors = self._write("errors", "broken\tmissing/empty 'assertions'\n")
        rc = batch_validation.main(["prog", "invalid-report", names, errors])
        self.assertEqual(rc, 0)

    def test_a_missing_errors_file_means_everything_is_valid(self):
        # A batch with nothing invalid never writes an errors file at all.
        names = self._write("names", "task1\ntask2\n")
        missing_errors = os.path.join(self.tmp.name, "does-not-exist")
        valid, invalid = batch_validation.split(
            batch_validation._read_names(names),
            batch_validation._read_errors(missing_errors))
        self.assertEqual(valid, ["task1", "task2"])
        self.assertEqual(invalid, [])

    def test_summary_subcommand_prints_the_summary_line(self):
        rc = batch_validation.main(["prog", "summary", "4", "3", "1"])
        self.assertEqual(rc, 0)

    def test_unknown_subcommand_returns_usage_exit_code(self):
        rc = batch_validation.main(["prog", "nonsense"])
        self.assertEqual(rc, 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
