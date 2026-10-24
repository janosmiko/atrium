#!/usr/bin/env python3
"""Tests for quit_outcome.grade. No VM, no build: `python3 <file>`.

The log fragments below are copied out of real artifacts under
build/vm-fixtures/results/ from 2026-08-10 - the SIGTERM line from
settings-confirm-quit-toggle's first launch, the surrounding shutdown noise
from the same file, and the clean tail from relaunch-file-readback.
Inventing shorter ones would test the matcher against a log this harness never
produces, and the noise is half the point: "Shutdown has begun" and a dozen
histograms with Shutdown in the name sit right next to the line that matters.

The cases that carry the task are the two a `pgrep` check answers wrongly: a
chord that was swallowed and a fallback that covered for it, and a process that
was gone on time but went because something signalled it.
"""
import unittest

import quit_outcome

# The line Chromium prints when the browser exits on a POSIX signal.
SIGTERM_LINE = ("[6248:41807:0810/023312.265587:VERBOSE1:"
                "chrome/browser/shutdown_signal_handlers_posix.cc:131] "
                "Handling shutdown for signal 15.")

# Everything else in that log with "Shutdown" in it. None of it means a signal.
SHUTDOWN_NOISE = "\n".join([
    '   "value": "Shutdown has begun"',
    ("[6248:41705:0810/023312.323738:VERBOSE1:"
     "components/actor/core/aggregated_journal.cc:145] "
     "[ActorTool]: ActorKeyedService::Shutdown: "),
    ("Histogram: Shutdown.Time.BrowserCloseManager.Confirmed recorded 1 "
     "samples, mean = 0.0 (flags = 0x41)"),
    ("Histogram: Profile.KeepAliveLeakAtShutdown recorded 2 samples, "
     "mean = 23.0 (flags = 0x41)"),
    # Present in BOTH a clean quit and a SIGTERM kill, so it can never be the
    # thing that tells them apart. Kept here so a future change that starts
    # reading it fails this file.
    "Histogram: OSX.ConfirmToQuit recorded 1 samples, mean = 0.0 (flags = 0x41)",
])

CLEAN_LOG = SHUTDOWN_NOISE
KILLED_LOG = SHUTDOWN_NOISE + "\n" + SIGTERM_LINE


def graded(**over):
    """grade() with the clean-quit arguments, minus whatever the case changes."""
    args = {"proc": "Atrium", "window_s": "15", "fallback": "none", "before": "6248",
            "after_chord": "", "after_fallback": "", "log_text": CLEAN_LOG}
    args.update(over)
    return quit_outcome.grade(**args)


class TheChordWorked(unittest.TestCase):
    def test_a_process_that_went_on_the_chord_passes(self):
        code, why = graded()
        self.assertEqual(code, 0, why)
        self.assertIn("6248", why, "the evidence must name what it watched")

    def test_shutdown_noise_is_not_a_signal(self):
        # A dozen lines in every log say Shutdown. Only one says a signal
        # handled it, and matching the wrong one would fail every clean quit.
        code, why = graded(log_text=CLEAN_LOG)
        self.assertEqual(code, 0, why)

    def test_an_unreadable_log_does_not_invent_a_signal(self):
        code, why = graded(log_text="")
        self.assertEqual(code, 0, why)


class TheChordWasSwallowed(unittest.TestCase):
    def test_a_quit_that_only_ended_on_the_fallback_fails(self):
        # settings-confirm-quit-toggle, 2026-08-10: 12 passed, 0 failed, and
        # this is the measurement that was missing from the tally.
        code, why = quit_outcome.grade("Atrium", "15", "SIGTERM", "6248",
                                       "6248", "", KILLED_LOG)
        self.assertEqual(code, 3, why)

    def test_it_names_which_fallback_fired(self):
        _, why = quit_outcome.grade("Atrium", "15", "SIGTERM", "6248", "6248",
                                    "", KILLED_LOG)
        self.assertIn("fallback=SIGTERM", why)
        self.assertIn("SWALLOWED", why)

    def test_it_cites_the_app_s_own_line(self):
        _, why = quit_outcome.grade("Atrium", "15", "SIGTERM", "6248", "6248",
                                    "", KILLED_LOG)
        self.assertIn("Handling shutdown for signal 15", why)

    def test_it_still_fails_when_the_log_was_lost(self):
        # The harness watched the process outlive the chord. That alone is the
        # finding; a missing log weakens the evidence, not the verdict.
        code, why = quit_outcome.grade("Atrium", "15", "SIGTERM", "6248",
                                       "6248", "", "")
        self.assertEqual(code, 3, why)
        self.assertIn("fallback=SIGTERM", why)


class NothingStoppedIt(unittest.TestCase):
    def test_still_running_with_no_fallback_sent_fails(self):
        code, why = quit_outcome.grade("Atrium", "10", "none", "6960", "6960",
                                       "", CLEAN_LOG)
        self.assertEqual(code, 4, why)
        self.assertIn("6960", why)

    def test_still_running_after_the_fallback_fails(self):
        code, why = quit_outcome.grade("Atrium", "15", "SIGTERM", "6960",
                                       "6960", "6960", CLEAN_LOG)
        self.assertEqual(code, 4, why)
        self.assertIn("SIGTERM", why)


class GoneForTheWrongReason(unittest.TestCase):
    def test_a_signalled_exit_inside_the_window_is_not_a_quit(self):
        # pgrep cannot tell this from a clean quit: the process is gone, on
        # time. The app's own log is the only thing that says a signal did it.
        code, why = quit_outcome.grade("Atrium", "15", "none", "6248", "",
                                       "", KILLED_LOG)
        self.assertEqual(code, 5, why)
        self.assertIn("SIGTERM", why)

    def test_it_names_the_signal_number_it_read(self):
        killed = SIGTERM_LINE.replace("signal 15", "signal 2")
        code, why = quit_outcome.grade("Atrium", "15", "none", "6248", "",
                                       "", killed)
        self.assertEqual(code, 5, why)
        self.assertIn("SIGINT", why)

    def test_a_signal_with_no_name_is_reported_by_number(self):
        killed = SIGTERM_LINE.replace("signal 15", "signal 30")
        _, why = quit_outcome.grade("Atrium", "15", "none", "6248", "", "",
                                    killed)
        self.assertIn("30", why)


class NothingWasMeasured(unittest.TestCase):
    def test_an_app_that_had_already_gone_is_not_a_passed_quit(self):
        code, why = graded(before="")
        self.assertEqual(code, 1, why)
        self.assertIn("before the harness asked", why)

    def test_that_verdict_holds_even_with_a_signal_in_the_log(self):
        code, why = quit_outcome.grade("Atrium", "15", "none", "", "", "",
                                       KILLED_LOG)
        self.assertEqual(code, 1, why)


class ReadingPgrep(unittest.TestCase):
    def test_several_pids_come_back_as_several(self):
        self.assertEqual(quit_outcome.pids("6248\n6960\n"), ["6248", "6960"])

    def test_carriage_returns_from_ssh_are_not_pids(self):
        # vm_ssh output arrives with CRLF; an unstripped "6248\r" would read as
        # a live process forever.
        self.assertEqual(quit_outcome.pids("6248\r\n"), ["6248"])

    def test_blank_output_is_no_process(self):
        self.assertEqual(quit_outcome.pids("  \n\n"), [])
        self.assertEqual(quit_outcome.pids(None), [])


class ReadingTheLog(unittest.TestCase):
    def test_the_signal_number_and_name_are_read_off_the_line(self):
        self.assertEqual(quit_outcome.signals_in(KILLED_LOG),
                         [(15, "SIGTERM", SIGTERM_LINE)])

    def test_the_last_signal_is_the_one_reported(self):
        two = KILLED_LOG + "\n" + SIGTERM_LINE.replace("signal 15", "signal 2")
        self.assertEqual(quit_outcome.signals_in(two)[-1][1], "SIGINT")

    def test_a_clean_log_has_none(self):
        self.assertEqual(quit_outcome.signals_in(CLEAN_LOG), [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
