#!/usr/bin/env python3
"""Tests for launch_outcome.grade. No VM, no build: `python3 <file>`.

a run that aborts can leave Atrium alive in the VM. The next
scenario's launch then hands off to that live process through Chromium's
ProcessSingleton instead of starting its own - the harness ends up grading
whatever the OLD process shows. Observed 2026-08-10: a profile picker left
over from an aborted run scored a scenario 1 of 4, and only a clean re-run
showed the real result.

Two independent signals, same shape as quit_outcome.py's two-signal design:

  THE HARNESS'S OWN BOOKKEEPING - was an Atrium process already running
  BEFORE this launch was even attempted. Answers "should this scenario have
  started at all".

  THE APP'S OWN ACCOUNT - the run log line Chromium's ProcessSingleton writes
  when a launch notifies an already-running process instead of starting its
  own (observed as "NotifyResult=1"; run-atrium.sh separately documents the
  same handoff printing "Opening in existing browser session"). Answers
  "did THIS launch attempt hand off", which catches the race the pgrep check
  cannot: a prior instance that starts up in the gap between the preflight
  pgrep and the launch itself.

The log fragment below is shaped like a real atrium-run.log tail: startup
VLOG noise surrounding the one line that matters, so the matcher is proven
against noise rather than an isolated string.
"""
import unittest

import launch_outcome

STARTUP_NOISE = "\n".join([
    "=== atrium run 2026-08-10 10:47:12 ===",
    "=== REAL keychain (default) ===",
    ("[6248:41705:0810/104712.001000:VERBOSE1:content/browser/browser_main_loop.cc:"
     "1234] BrowserMainLoop::CreateThreads"),
    ("[6248:41705:0810/104712.045213:VERBOSE1:chrome/browser/chrome_browser_main.cc:"
     "987] PreMainMessageLoopRun"),
])

HANDOFF_LINE = ("[6248:41705:0810/104712.081930:VERBOSE1:chrome/browser/"
                "process_singleton_posix.cc:421] NotifyResult=1")

CLEAN_LOG = STARTUP_NOISE
HANDOFF_LOG = STARTUP_NOISE + "\n" + HANDOFF_LINE


def graded(**over):
    """grade() with the clean-start arguments, minus whatever the case changes."""
    args = {"before_pids_text": "", "log_text": CLEAN_LOG}
    args.update(over)
    return launch_outcome.grade(**args)


class ACleanStart(unittest.TestCase):
    def test_no_prior_process_and_no_handoff_line_passes(self):
        code, why = graded()
        self.assertEqual(code, 0, why)

    def test_startup_noise_is_not_a_handoff(self):
        # Plenty of lines in a real log carry "Notify" in some other context
        # (histograms, IPC). Only the exact ProcessSingleton line should match.
        code, why = graded(log_text=CLEAN_LOG)
        self.assertEqual(code, 0, why)

    def test_an_unreadable_log_does_not_invent_a_handoff(self):
        code, why = graded(log_text="")
        self.assertEqual(code, 0, why)


class AlreadyRunningBeforeLaunch(unittest.TestCase):
    def test_a_leftover_process_refuses_the_scenario(self):
        code, why = graded(before_pids_text="6248")
        self.assertEqual(code, 2, why)
        self.assertIn("6248", why)
        self.assertIn("already running", why)

    def test_several_leftover_pids_are_all_named(self):
        code, why = graded(before_pids_text="6248\n6960\n")
        self.assertEqual(code, 2, why)
        self.assertIn("6248", why)
        self.assertIn("6960", why)

    def test_this_check_fires_even_with_a_clean_log(self):
        # The preflight question is asked BEFORE the launch log exists at all -
        # a prior process is disqualifying on its own.
        code, why = graded(before_pids_text="6248", log_text="")
        self.assertEqual(code, 2, why)


class SingletonHandoffInTheLog(unittest.TestCase):
    def test_a_handoff_with_no_prior_pgrep_hit_still_fails(self):
        # The race this check exists for: pgrep saw nothing before the launch,
        # but the launch itself handed off - some other instance won the race
        # in the gap between the preflight check and the launch attempt.
        code, why = graded(log_text=HANDOFF_LOG)
        self.assertEqual(code, 3, why)

    def test_it_cites_the_handoff_line(self):
        _, why = graded(log_text=HANDOFF_LOG)
        self.assertIn("NotifyResult=1", why)

    def test_it_says_the_wrong_process_would_be_graded(self):
        _, why = graded(log_text=HANDOFF_LOG)
        self.assertIn("OLD process", why)

    def test_the_open_in_existing_session_wording_also_matches(self):
        # run-atrium.sh documents this as the human-visible half of the same
        # handoff; a scenario log carrying it must fail the same way.
        log = STARTUP_NOISE + "\nOpening in existing browser session.\n"
        code, why = graded(log_text=log)
        self.assertEqual(code, 3, why)


class BothSignalsAtOnce(unittest.TestCase):
    def test_the_preflight_signal_wins_when_both_fire(self):
        # A process already running before launch is the earlier, more
        # actionable fact - report that one rather than a race with itself.
        code, why = graded(before_pids_text="6248", log_text=HANDOFF_LOG)
        self.assertEqual(code, 2, why)


class DeadLaunch(unittest.TestCase):
    """a launch that never produced a running process. Both
    instances observed 2026-08-10 - the binary missing, and a deploy wiped
    by a restore SKIP_DEPLOY should have implied skipping - reach this
    through the same empty-or-erroring log, so the check has to say which one
    actually happened rather than leave a reader to guess.
    """

    def test_the_env_no_such_file_line_names_the_missing_binary(self):
        log = ("env: /Users/admin/atrium-probe/Atrium.app/Contents/MacOS/"
               "Atrium: No such file or directory\n")
        evidence = launch_outcome.dead_launch_evidence(log)
        self.assertIn("binary is missing", evidence)
        self.assertIn("No such file or directory", evidence)

    def test_a_completely_empty_log_says_nothing_ever_ran(self):
        evidence = launch_outcome.dead_launch_evidence("")
        self.assertIn("never wrote a single log line", evidence)

    def test_none_is_treated_the_same_as_empty(self):
        evidence = launch_outcome.dead_launch_evidence(None)
        self.assertIn("never wrote a single log line", evidence)

    def test_a_log_with_content_but_no_known_cause_cites_the_tail(self):
        log = STARTUP_NOISE
        evidence = launch_outcome.dead_launch_evidence(log)
        self.assertIn("PreMainMessageLoopRun", evidence)

    def test_cli_dead_mode_always_fails_and_prints_the_evidence(self):
        import contextlib
        import io
        import os
        import tempfile

        fd, path = tempfile.mkstemp()
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as handle:
                handle.write("env: /path/Atrium: No such file or directory\n")
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                code = launch_outcome.main(["launch_outcome.py", "dead", path])
            self.assertEqual(code, 1)
            self.assertIn("binary is missing", out.getvalue())
        finally:
            os.remove(path)


class ReadingPgrep(unittest.TestCase):
    def test_carriage_returns_from_ssh_are_not_pids(self):
        self.assertFalse(launch_outcome.already_running("6248\r\n")[0] is False)

    def test_blank_output_is_no_process(self):
        running, _ = launch_outcome.already_running("  \n\n")
        self.assertFalse(running)
        running, _ = launch_outcome.already_running(None)
        self.assertFalse(running)


if __name__ == "__main__":
    unittest.main(verbosity=2)
