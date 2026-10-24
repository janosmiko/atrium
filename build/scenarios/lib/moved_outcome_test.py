#!/usr/bin/env python3
"""Tests for moved_outcome.check. No VM, no build: `python3 <file>`.

Every line below is real pixcmp output, measured on stored frames from
build/vm-fixtures/results/sidebar-vertical-band-*/ over the region the scenario
samples, [10,240,200,40]. Inventing numbers would test the grader against
readings the harness has never produced.

The cases that matter are the ones where the MEAN-based assertion this harness
already had would have reached the opposite conclusion - a working band the mean
called still, and a still band that only a per-pixel count can tell from it.
"""
import unittest

import moved_outcome

# 2026-08-10 06:06, after the fix restored the row pill's tint. The band moved
# and the colour is right, so both measures agree.
TINTED_AND_MOVED = (
    "MOVED changed_pct=74.06 minChangedPct=40.00 tol=12.0 changed=5925 n=8000 "
    "mean_px_dist=129.0 max_px_dist=235.6 mean_delta=125.23 before_var=2558.4 "
    "after_var=8.3"
)

# 2026-08-10 02:20, the same gesture on the build whose pill had lost its tint.
# The mean read 9.88 against a bar of 10.0 and called this still for weeks.
UNTINTED_BUT_MOVED = (
    "MOVED changed_pct=73.38 minChangedPct=40.00 tol=12.0 changed=5870 n=8000 "
    "mean_px_dist=17.9 max_px_dist=210.7 mean_delta=9.88 before_var=383.5 "
    "after_var=8.3"
)

# 2026-08-08 15:07, a capture that outlived the gesture: the list had already
# sprung back, so the strip is pixel-identical in both frames.
SPRUNG_BACK = (
    "STILL changed_pct=0.00 minChangedPct=40.00 tol=12.0 changed=0 n=8000 "
    "mean_px_dist=0.0 max_px_dist=0.0 mean_delta=0.00 before_var=708.4 "
    "after_var=708.4"
)

# 2026-08-08 15:21, the marginal one - the highest reading among runs where the
# band did not visibly move. This is what sets the floor under the threshold.
BARELY_ANYTHING = (
    "STILL changed_pct=16.73 minChangedPct=40.00 tol=12.0 changed=1338 n=8000 "
    "mean_px_dist=10.2 max_px_dist=167.9 mean_delta=0.65 before_var=708.4 "
    "after_var=660.4"
)

# The same before/after pair as TINTED_AND_MOVED over [10,120,200,200], a region
# that spans BOTH ends of the movement. What leaves the top arrives at the
# bottom, so the region mean barely shifts - 0.03 against a bar of 10.0.
MEAN_CANCELS_OUT = (
    "MOVED changed_pct=32.34 minChangedPct=30.00 tol=12.0 changed=12937 "
    "n=40000 mean_px_dist=52.6 max_px_dist=441.7 mean_delta=0.03 "
    "before_var=1436.0 after_var=1379.2"
)


class TheContentMoved(unittest.TestCase):
    def test_a_tinted_pill_leaving_the_strip_passes(self):
        code, why = moved_outcome.check("40.0", 0, TINTED_AND_MOVED)
        self.assertEqual(code, 0, why)
        self.assertIn("74.06", why)

    def test_the_evidence_carries_the_mean_as_well(self):
        # Both numbers on one line is what let the 9.88 miss be diagnosed at all.
        _, why = moved_outcome.check("40.0", 0, TINTED_AND_MOVED)
        self.assertIn("mean_delta=125.23", why)
        self.assertIn("before_var=2558.4", why)


class WhatAMeanWouldHaveMissed(unittest.TestCase):
    def test_an_untinted_pill_still_counts_as_movement(self):
        # The regression this assertion exists for: the gesture worked, the
        # colour did not, and the mean-based gate called the whole thing dead.
        code, why = moved_outcome.check("40.0", 0, UNTINTED_BUT_MOVED)
        self.assertEqual(code, 0, why)
        self.assertIn("mean_delta=9.88", why)

    def test_a_region_spanning_both_ends_still_counts_as_movement(self):
        # 32.34% of 40,000 pixels changed while the region mean moved by 0.03.
        code, why = moved_outcome.check("30.0", 0, MEAN_CANCELS_OUT)
        self.assertEqual(code, 0, why)
        self.assertIn("mean_delta=0.03", why)


class TheContentDidNot(unittest.TestCase):
    def test_a_list_that_sprang_back_before_the_frame_fails(self):
        code, why = moved_outcome.check("40.0", 2, SPRUNG_BACK)
        self.assertEqual(code, 2, why)
        self.assertIn("did NOT move", why)

    def test_the_marginal_run_sits_below_the_bar(self):
        # 16.73 is the highest reading among runs where nothing moved, and the
        # lowest reading among runs where something did is 48.88. The bar of 40
        # is derived from that gap; this test pins the lower half of it.
        code, why = moved_outcome.check("40.0", 2, BARELY_ANYTHING)
        self.assertEqual(code, 2, why)

    def test_the_lowest_reading_from_a_band_that_moved_still_passes(self):
        # 2026-08-08 15:27, delta=13.70, after_var=755 - the capture caught the
        # list mid-springback. It is the upper half of the same gap.
        line = ("MOVED changed_pct=48.88 minChangedPct=40.00 tol=12.0 "
                "changed=3910 n=8000 mean_px_dist=36.7 max_px_dist=220.1 "
                "mean_delta=13.70 before_var=708.4 after_var=755.0")
        code, why = moved_outcome.check("40.0", 0, line)
        self.assertEqual(code, 0, why)


class AThresholdThatCannotFail(unittest.TestCase):
    def test_a_bar_of_zero_is_refused_rather_than_passed(self):
        # This is the defect the task exists for: green forever, measuring
        # nothing, and invisible in a run log.
        code, why = moved_outcome.check("0", 0, SPRUNG_BACK)
        self.assertEqual(code, 1, why)
        self.assertIn("never go red", why)

    def test_a_negative_bar_is_refused(self):
        code, why = moved_outcome.check("-5", 0, TINTED_AND_MOVED)
        self.assertEqual(code, 1, why)

    def test_a_bar_over_a_hundred_is_refused_as_unreachable(self):
        code, why = moved_outcome.check("120", 0, TINTED_AND_MOVED)
        self.assertEqual(code, 1, why)
        self.assertIn("never go green", why)

    def test_a_missing_bar_is_a_scenario_fault_not_a_measurement(self):
        code, why = moved_outcome.check("", 0, TINTED_AND_MOVED)
        self.assertEqual(code, 1, why)


class TheSampleHasToBeWorthAPercentage(unittest.TestCase):
    def test_a_region_under_the_pixel_floor_is_refused(self):
        line = ("MOVED changed_pct=60.00 minChangedPct=40.00 tol=12.0 "
                "changed=180 n=300 mean_px_dist=90.0 max_px_dist=200.0 "
                "mean_delta=40.00 before_var=500.0 after_var=5.0")
        code, why = moved_outcome.check("40.0", 0, line)
        self.assertEqual(code, 1, why)
        self.assertIn("300 pixels", why)

    def test_the_smallest_region_any_scenario_samples_is_above_the_floor(self):
        # header-hover samples 120x16. If the floor ever climbs past
        # that, an existing scenario starts reporting a harness fault.
        self.assertLess(moved_outcome.MIN_PIXELS, 120 * 16)


class TheFramesCannotAnswer(unittest.TestCase):
    def test_byte_identical_frames_are_a_fake_not_a_still_region(self):
        code, why = moved_outcome.check(
            "40.0", 3, "IDENTICAL byte-identical inputs a.png == b.png")
        self.assertEqual(code, 3, why)
        self.assertIn("FAKE", why)

    def test_pixcmp_saying_nothing_is_a_harness_fault_not_a_failure(self):
        # An empty answer read as "did not move" is a claim about the app made
        # from no measurement at all.
        code, why = moved_outcome.check("40.0", 1, "")
        self.assertEqual(code, 1, why)
        self.assertIn("did not answer", why)

    def test_an_error_message_is_not_parsed_as_a_measurement(self):
        code, why = moved_outcome.check(
            "40.0", 1, "frames differ in size (2560x1600 vs 1280x800)")
        self.assertEqual(code, 1, why)


class Parsing(unittest.TestCase):
    def test_a_line_without_changed_pct_is_not_a_measurement(self):
        self.assertIsNone(moved_outcome.parse("MOVED n=8000 tol=12.0"))

    def test_a_non_numeric_field_is_refused_rather_than_guessed(self):
        self.assertIsNone(
            moved_outcome.parse("MOVED changed_pct=nan% n=8000"))

    def test_the_verdict_word_is_ignored(self):
        # The grader decides; pixcmp's own word is context, not the answer.
        fields = moved_outcome.parse(SPRUNG_BACK)
        self.assertEqual(fields["changed_pct"], 0.0)
        self.assertEqual(fields["n"], 8000.0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
