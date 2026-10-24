#!/usr/bin/env python3
"""Tests for mid_frame.pick. No VM, no build: `python3 <file>`.

The bursts below are real rapidcap timestamps, read off the frame
filenames of two hotkey space-switch runs and a docked-bar run. Their shape is the whole bug: a 340-400ms hole
where the chord is driven, with the transition on the far side of it.

Which frame each case expects is not a preference either - it is the frame
whose stored pixels pass the scenario's assertions. For the space-switch scenario the
frames that differ from BOTH the resting state and the settled one, by the
scenario's own min_delta of 6, are t=909-1143 in the first burst and
t=1017-1253 in the second (host pixcmp over region 16,250,180,120).
"""
import os
import tempfile
import unittest

import mid_frame

# First hotkey space-switch run. Frames every
# 15-40ms until t=183, nothing until t=577 (the chord), then the glide.
SWITCH_A = (167.3, 182.8, 576.7, 614.5, 654.7, 876.9, 909.4, 940.4, 977.3,
            1019.6, 1060.0, 1104.4, 1142.5, 1181.8, 1223.4, 1265.8, 1306.9,
            1353.7, 1391.1, 1435.0, 1482.1)

# The same scenario 28 minutes later. The hole lands 15ms earlier, which is
# exactly the drift an absolute probe cannot follow.
SWITCH_B = (149.5, 165.0, 562.1, 605.4, 639.9, 785.6, 813.6, 992.7, 1017.4,
            1046.0, 1086.8, 1129.1, 1170.1, 1211.6, 1252.7, 1295.2, 1335.4,
            1377.9, 1421.2, 1460.2)

# Docked-bar run.
DOCKING = (158.7, 173.1, 512.5, 594.6, 638.2, 679.3, 717.6, 759.0, 798.3,
           841.0, 879.1, 922.0)

# Two 400ms stalls, ending at 540 and 1020. Contrived: no recorded burst has
# ever held two of exactly the same length, and the tie has to resolve the
# same way every run or the same frames grade differently on a re-run.
TWIN_STALLS = (100.0, 140.0, 540.0, 580.0, 620.0, 1020.0, 1060.0, 1100.0)


def burst(timestamps, prefix="/r/the-tint-animates/frames/scen-cap-switch"):
    """Frames as the runner hands them over: real paths, real names."""
    return [(f"{prefix}/frame-{i:03d}-t{t:05.1f}.png", t)
            for i, t in enumerate(timestamps)]


def chosen(frames, **kwargs):
    frame, _ = mid_frame.pick(frames, **kwargs)
    return frame[1]


class PickTest(unittest.TestCase):
    def test_a_burst_start_probe_lands_before_the_chord(self):
        # The defect, pinned. 520ms into the burst is inside the hole, so the
        # nearest frame is the last one BEFORE the chord - the resting state,
        # which is what graded delta=0.00 against the resting state.
        self.assertEqual(chosen(burst(SWITCH_A), mid_at_ms=520), 576.7)
        self.assertEqual(chosen(burst(SWITCH_B), mid_at_ms=520), 562.1)

    def test_a_chord_relative_probe_lands_in_the_transition(self):
        # The same 510ms past the hole in both bursts, and both frames differ
        # from the resting state and from the settled one.
        self.assertEqual(
            chosen(burst(SWITCH_A), after_chord_ms=510, key_delay_ms=400), 1104.4)
        self.assertEqual(
            chosen(burst(SWITCH_B), after_chord_ms=510, key_delay_ms=400), 1086.8)

    def test_the_anchor_is_the_frame_after_the_hole(self):
        self.assertEqual(
            chosen(burst(DOCKING), after_chord_ms=0, key_delay_ms=400), 512.5)

    def test_the_recorded_chord_return_names_which_stall(self):
        # SWITCH_A stalls twice: 183->577 around the chord, and 655->877 mid
        # glide. The recorded return picks the first even though the second is
        # long enough to qualify on its own.
        self.assertEqual(
            chosen(burst(SWITCH_A), after_chord_ms=510, key_delay_ms=400,
                   chord_return_ms=560), 1104.4)

    def test_a_chord_return_far_from_every_stall_still_picks_the_nearest(self):
        # The recorded time is host-side shell time and the frame timestamps
        # are rapidcap's own, so the two origins differ by rapidcap's startup.
        # The anchor must survive that skew, not assume the clocks agree.
        self.assertEqual(
            chosen(burst(SWITCH_A), after_chord_ms=510, key_delay_ms=400,
                   chord_return_ms=300), 1104.4)

    def test_without_a_chord_return_the_longest_stall_anchors(self):
        self.assertEqual(
            chosen(burst(SWITCH_A), after_chord_ms=510, key_delay_ms=400,
                   chord_return_ms=None), 1104.4)

    def test_a_burst_with_no_hole_falls_back_to_the_burst_start(self):
        even = burst(tuple(200.0 + 40 * i for i in range(20)))
        frame, reason = mid_frame.pick(even, after_chord_ms=510, key_delay_ms=400)
        self.assertEqual(frame[1], 920.0)  # 400 + 510, nearest frame
        self.assertIn("no stall", reason)

    def test_a_warm_burst_anchors_on_the_recorded_chord_return(self):
        # Some bursts never stalled, and key_delay+after_chord put the
        # probe 150ms before the chord had even landed - on the resting state.
        even = burst(tuple(160.0 + 33 * i for i in range(24)))
        frame, reason = mid_frame.pick(even, after_chord_ms=20, key_delay_ms=300,
                                       chord_return_ms=453)
        self.assertEqual(frame[1], 457.0)
        self.assertIn("chord's return", reason)

    def test_the_chord_return_placement_ignores_a_later_stall(self):
        # The lights burst stalls AFTER the slide, and that stall is the only
        # one, so anchoring on it walks the probe past the travelling lights.
        late_stall = burst((160.0, 200.0, 240.0, 280.0, 320.0, 700.0, 740.0))
        frame, reason = mid_frame.pick(late_stall, after_chord_return_ms=35,
                                       chord_return_ms=250)
        self.assertEqual(frame[1], 280.0)
        self.assertIn("no stall consulted", reason)

    def test_the_chord_return_placement_needs_a_measured_return(self):
        even = burst((160.0, 200.0, 240.0))
        frame, reason = mid_frame.pick(even, after_chord_return_ms=35)
        self.assertIsNone(frame)
        self.assertIn("no origin", reason)

    def test_an_empty_burst_chooses_nothing(self):
        frame, reason = mid_frame.pick([], mid_at_ms=520)
        self.assertIsNone(frame)
        self.assertIn("no frames", reason)

    def test_two_equal_stalls_anchor_on_the_earlier_one(self):
        # max()/min() keep the FIRST of equal keys, and the earlier stall is
        # the chord's: a burst cannot show the chord's effect before the chord.
        frames = burst(TWIN_STALLS)
        self.assertEqual(chosen(frames, after_chord_ms=40, key_delay_ms=100), 580.0)
        # 780 sits 240ms from both stall ends, so the tie is on the recorded
        # time as well, and it has to break the same way.
        self.assertEqual(
            chosen(frames, after_chord_ms=40, key_delay_ms=100,
                   chord_return_ms=780), 580.0)

    def test_the_reason_names_the_anchor_frame(self):
        _, reason = mid_frame.pick(burst(DOCKING), after_chord_ms=286,
                                   key_delay_ms=400, chord_return_ms=560)
        self.assertIn("frame-002-t512.5.png", reason)


class ChordReturnFileTest(unittest.TestCase):
    """The runner writes chord-return.ms in the VM, so it can arrive absent,
    empty (perl missing, the redirect made the file anyway) or truncated (the
    ssh session cut mid-write). None of those may hand the picker a number."""

    def setUp(self):
        holder = tempfile.TemporaryDirectory()
        self.addCleanup(holder.cleanup)
        self.root = holder.name

    def write(self, name, text):
        path = os.path.join(self.root, name)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text)
        return path

    def test_a_readable_file_is_the_measurement(self):
        self.assertEqual(
            mid_frame._chord_return(self.write("good.ms", "612\n")), 612.0)

    def test_a_missing_an_empty_and_a_garbled_file_are_all_no_measurement(self):
        cases = {
            "missing": os.path.join(self.root, "nothing-was-written.ms"),
            "empty": self.write("empty.ms", ""),
            "garbled": self.write("garbled.ms", "6 1 2ms"),
            "zero": self.write("zero.ms", "0"),
        }
        for label, path in cases.items():
            with self.subTest(label):
                self.assertIsNone(mid_frame._chord_return(path))

    def test_no_measurement_falls_back_to_the_longest_stall(self):
        # SWITCH_A's second stall (655->877) would anchor here if a garbled
        # read had been passed through as a number near 880, and the pick
        # would land on the settled state at 1391 instead of mid-glide.
        garbled = mid_frame._chord_return(self.write("garbled.ms", "88 0ms"))
        frame, reason = mid_frame.pick(burst(SWITCH_A), after_chord_ms=510,
                                       key_delay_ms=400, chord_return_ms=garbled)
        self.assertEqual(frame[1], 1104.4)
        self.assertIn("no chord return recorded", reason)


class FramesInTest(unittest.TestCase):
    def test_only_the_basename_carries_the_timestamp(self):
        # "-t" occurs in scenario names too, and a picker that reads
        # the whole path timestamps every frame 0 and returns an arbitrary one.
        with tempfile.TemporaryDirectory() as root:
            capture = os.path.join(root, "the-tint-still-runs")
            os.mkdir(capture)
            for name in ("frame-000-t149.5.png", "frame-001-t562.1.png",
                         "rapidcap.err", "notes-t999.9.txt"):
                open(os.path.join(capture, name), "w").close()
            self.assertEqual([t for _, t in mid_frame.frames_in(capture)],
                             [149.5, 562.1])


if __name__ == "__main__":
    unittest.main(verbosity=2)
