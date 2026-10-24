#!/usr/bin/env python3
"""Choose the mid-transition frame of a key_during_capture burst.

WHERE THE HOLE IN THE BURST COMES FROM. key_during_capture backgrounds
rapidcap and fires the chord from the SAME ssh session, so the wait before the
key is an in-VM `sleep` and not a host round trip - and the burst still stops
delivering frames for 250-400ms around the chord. The window is that sleep
plus the separate `cgevent` process: its own launch, and for a modified combo
~110ms of held-modifier timing (cgevent.swift withModifier/keypress) before
the key even posts. Recorded bursts show the hole plainly - frames every
15-70ms up to t=165, nothing until t=562, then a steady 40ms cadence once the
chord's effect is on screen. The frame names carry their own timestamps, so
any stored run of the hotkey space-switch scenario shows it.

So a probe measured from the burst START lands inside the hole, and the
nearest frame to it is the LAST ONE BEFORE THE CHORD: that scenario asked for
520ms, got the frame at 562ms, and graded a transition against a picture of
the resting state - delta 0.00 while the frames showed the rail travelling.

`mid_after_chord_ms` measures from the first frame AFTER the hole instead.
The value then says how far into the transition the probe sits, and a longer
or shorter hole carries the probe with it. `mid_at_ms` keeps its old meaning
(measured from the burst start) so the scenarios tuned against it are
untouched - a scenario picks one, never two.

`mid_after_chord_return_ms` measures from the recorded chord return and never
looks for a stall. A warm stream can stall AFTER the transition, and that is
the only stall to anchor on, which walks the probe past what it was aimed at.

Usage:
  mid_frame.py <frames-dir> <key-delay-ms> <mid-at-ms> <after-chord-ms>
               [<chord-return-file> [<after-chord-return-ms>]]

  Pass "" for whichever placement the scenario did not set.
  <chord-return-file> holds the ms the runner measured from backgrounding
  rapidcap to `cgevent` returning; it names WHICH stall the chord sits in when
  a burst has several. Absent or unreadable, the longest stall stands in.

Prints the chosen frame's path on stdout, and how it was chosen on stderr for
the runner to log.

Exit codes:
  0  a frame was chosen
  2  usage
  4  the burst produced no frames
"""
import os
import re
import sys

# A gap this long is not the capture's own cadence: bursts run at 20-25fps
# (40-50ms a frame), so this is three intervals and then some.
MIN_STALL_MS = 120.0

# Matched against the BASENAME only. "-t" also occurs in scenario names
# ("-the-", "-tint-"), so matching the whole path timestamps every frame 0 and
# the tie hands back an arbitrary one (a pre-keypress frame won once).
_FRAME_TS = re.compile(r"^frame-\d+-t(\d+(?:\.\d+)?)\.png$")


def frames_in(dirpath):
    """Every frame in a capture dir as (path, ms-since-the-stream-started)."""
    found = []
    for name in os.listdir(dirpath):
        match = _FRAME_TS.match(name)
        if match:
            found.append((os.path.join(dirpath, name), float(match.group(1))))
    found.sort(key=lambda frame: frame[1])
    return found


def stalls(frames, min_stall_ms=MIN_STALL_MS):
    return [(before, after) for before, after in zip(frames, frames[1:])
            if after[1] - before[1] >= min_stall_ms]


def anchor(frames, chord_return_ms=None, min_stall_ms=MIN_STALL_MS):
    """The first frame after the stall the chord sits in, and why that one."""
    gaps = stalls(frames, min_stall_ms)
    if not gaps:
        return None, ""
    if chord_return_ms:
        before, after = min(gaps, key=lambda gap: abs(gap[1][1] - chord_return_ms))
        why = (f"the {after[1] - before[1]:.0f}ms stall ending at {after[1]:.0f}ms, "
               f"the one nearest the chord's return at {chord_return_ms:.0f}ms")
    else:
        before, after = max(gaps, key=lambda gap: gap[1][1] - gap[0][1])
        why = (f"the {after[1] - before[1]:.0f}ms stall ending at {after[1]:.0f}ms, "
               f"the burst's longest (no chord return recorded)")
    return after, why


def pick(frames, mid_at_ms=None, after_chord_ms=None, key_delay_ms=0.0,
         chord_return_ms=None, min_stall_ms=MIN_STALL_MS,
         after_chord_return_ms=None):
    """Choose one frame. Returns ((path, ts), reason) or (None, reason)."""
    if not frames:
        return None, "the burst produced no frames"
    frames = sorted(frames, key=lambda frame: frame[1])
    if after_chord_return_ms is not None:
        if not chord_return_ms:
            return None, ("nothing measured the chord's return, so a probe "
                          "placed from it has no origin to measure from")
        target = float(chord_return_ms) + float(after_chord_return_ms)
        reason = (f"{after_chord_return_ms:.0f}ms past the chord's return at "
                  f"{chord_return_ms:.0f}ms, no stall consulted")
    elif after_chord_ms is None:
        target = float(mid_at_ms)
        reason = f"{target:.0f}ms after the burst started"
    else:
        found, found_why = anchor(frames, chord_return_ms, min_stall_ms)
        if found is None and chord_return_ms:
            # A warm stream has no hole to anchor on, and key_delay misses the
            # ssh and cgevent time after the sleep - 150ms on some bursts.
            target = float(chord_return_ms) + float(after_chord_ms)
            reason = (f"{after_chord_ms:.0f}ms past the chord's return at "
                      f"{chord_return_ms:.0f}ms - no stall over "
                      f"{min_stall_ms:.0f}ms, the stream stayed awake")
        elif found is None:
            # Nothing measured the chord at all. Say so rather than pick as if
            # it had been located - a silent guess reads like a measurement.
            target = float(key_delay_ms) + float(after_chord_ms)
            reason = (f"{target:.0f}ms after the burst started - no stall over "
                      f"{min_stall_ms:.0f}ms to anchor on, so key_delay+after_chord stands in")
        else:
            target = found[1] + float(after_chord_ms)
            reason = (f"{after_chord_ms:.0f}ms past {os.path.basename(found[0])}, "
                      f"the first frame after {found_why}")
    chosen = min(frames, key=lambda frame: abs(frame[1] - target))
    return chosen, f"target {target:.0f}ms ({reason})"


def _ms(text):
    return None if text.strip() == "" else float(text)


def _chord_return(path):
    try:
        with open(path, encoding="utf-8") as handle:
            value = float(handle.read().strip())
    except (OSError, ValueError):
        return None
    return value if value > 0 else None


def main(argv):
    if len(argv) not in (5, 6, 7):
        sys.stderr.write(__doc__)
        return 2
    dirpath, key_delay, mid_at, after_chord = argv[1:5]
    try:
        mid_at_ms, after_chord_ms = _ms(mid_at), _ms(after_chord)
        key_delay_ms = _ms(key_delay) or 0.0
        after_return_ms = _ms(argv[6]) if len(argv) == 7 else None
    except ValueError as err:
        sys.stderr.write(f"non-numeric probe placement: {err}\n")
        return 2
    placements = [mid_at_ms, after_chord_ms, after_return_ms]
    if sum(value is not None for value in placements) != 1:
        sys.stderr.write("need exactly one of <mid-at-ms> / <after-chord-ms> / "
                         "<after-chord-return-ms>\n")
        return 2
    chord_return_ms = _chord_return(argv[5]) if len(argv) >= 6 else None
    try:
        frames = frames_in(dirpath)
    except OSError as err:
        sys.stderr.write(f"{err}\n")
        return 4
    chosen, reason = pick(frames, mid_at_ms, after_chord_ms, key_delay_ms,
                          chord_return_ms,
                          after_chord_return_ms=after_return_ms)
    sys.stderr.write(reason + "\n")
    if chosen is None:
        return 4
    print(chosen[0])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
