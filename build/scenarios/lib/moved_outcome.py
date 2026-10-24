#!/usr/bin/env python3
"""Grade a per-pixel movement measurement.

Backs the `pixels_changed` assertion. pixcmp's `moved` subcommand does the pixel
math and hands its one output line here; this decides what the numbers are worth
and writes the evidence - the same division of labour as file_value.py and
quit_outcome.py.

WHY THIS ASSERTION EXISTS. The other pixel assertions are built on pixcmp's
`diff`, whose delta is a euclidean distance between the two region MEANS.
Averaging the region before comparing it throws away where inside it anything
happened, and that has now been caught reading near zero for working behaviour
twice, in two different ways:

  CANCELLATION. A region spanning both ends of a movement scores near zero,
  because what leaves one end and arrives at the other cancels in the shared
  average. On sidebar-vertical-band's own stored frames, the region
  [10,120,200,200] - which covers both where the row pill sat and where it went -
  reads a mean delta of 0.03 while 32.34% of its 40,000 pixels genuinely changed.

  DESATURATION. The delta also carries the MAGNITUDE of the colour change, so it
  falls when a thing still moves but is painted paler. sidebar-vertical-band read
  9.88 against a bar of 10.0 for weeks while its gesture worked perfectly - the
  row pill had lost its tint, not its motion. Over that same regression the
  per-pixel share barely moved: 74.4% before the pill lost its colour, 73.4%
  while it was untinted, 74.1% after the fix restored it.

So `pixels_changed` answers "did this region's content move" and leaves "and is
it the right colour" to the mean-based assertion. Neither replaces the other;
running one alone is how both misses happened.

WHAT THIS REFUSES, and why each refusal is here rather than left to the caller:

  A BAR OF ZERO. `min_changed_pct: 0` passes on any two frames ever captured.
  A threshold that cannot fail is the defect this whole task exists to stop, and
  it is invisible in a green run log - so it is refused at the point of grading,
  not left for someone to notice.

  A REGION TOO SMALL TO CARRY A PERCENTAGE. Over a few hundred pixels, one
  antialiased glyph edge is worth whole percentage points. The smallest region
  any scenario in this repo samples is 1,920 pixels; the floor below is far
  under that, so it refuses only regions no scenario has a reason to use.

  BYTE-IDENTICAL FRAMES. Same fidelity rule the rest of the harness applies: a
  duplicated frame is a fake, never a measurement. pixcmp reports it as rc 3.

CLI:
  moved_outcome.py <min_changed_pct> <pixcmp-rc> <pixcmp-output-line>

Exit codes: 0 moved, 2 did not move, 3 byte-identical frames, 1 unusable input
(a vacuous bar, too small a region, or pixcmp not answering). One evidence line
on stdout either way.
"""
import sys

# A 20x20 patch. Chosen against the regions scenarios actually sample - the
# smallest is 120x16 = 1,920 pixels - so this refuses only what no scenario uses.
MIN_PIXELS = 400

# pixcmp `moved` prints one line of `key=value` pairs after a verdict word.
NUMERIC_FIELDS = ("changed_pct", "tol", "changed", "n", "mean_px_dist",
                  "max_px_dist", "mean_delta", "before_var", "after_var")


def parse(line):
    """The key=value pairs out of pixcmp's line, or None if it is not one."""
    fields = {}
    for token in line.split():
        if "=" not in token:
            continue
        key, _, value = token.partition("=")
        if key in NUMERIC_FIELDS:
            try:
                fields[key] = float(value)
            except ValueError:
                return None
    if "changed_pct" not in fields or "n" not in fields:
        return None
    return fields


def check(min_changed_pct, rc, output):
    """(exit code, evidence line). See the module doc for what each code means."""
    try:
        bar = float(min_changed_pct)
    except (TypeError, ValueError):
        return 1, ("min_changed_pct %r is not a number, so nothing was measured"
                   % (min_changed_pct,))
    if bar <= 0:
        return 1, ("min_changed_pct is %g - a bar of zero passes on any two "
                   "frames, so this assertion could never go red" % bar)
    if bar > 100:
        return 1, ("min_changed_pct is %g, and a share of pixels cannot exceed "
                   "100 - this assertion could never go green" % bar)

    # rc arrives from bash as a string and from the tests as an int.
    if str(rc).strip() == "3":
        return 3, ("FAKE(byte-identical frames) %s" % output.strip()
                   if output.strip() else
                   "FAKE - pixcmp reports the two frames are byte-identical")

    fields = parse(output)
    if fields is None:
        return 1, ("pixcmp did not answer with a measurement (rc=%s): %r"
                   % (rc, output.strip()))

    n = int(fields["n"])
    if n < MIN_PIXELS:
        return 1, ("the region holds %d pixels, under the %d-pixel floor - a "
                   "percentage over that few samples moves by whole points on "
                   "one glyph edge" % (n, MIN_PIXELS))

    changed_pct = fields["changed_pct"]
    detail = ("changed_pct=%.2f (bar %.2f) changed=%d/%d tol=%.1f "
              "mean_px_dist=%.1f mean_delta=%.2f before_var=%.1f after_var=%.1f"
              % (changed_pct, bar, int(fields.get("changed", 0)), n,
                 fields.get("tol", 0.0), fields.get("mean_px_dist", 0.0),
                 fields.get("mean_delta", 0.0), fields.get("before_var", 0.0),
                 fields.get("after_var", 0.0)))
    if changed_pct < bar:
        return 2, ("the region's content did NOT move: %s" % detail)
    return 0, ("%.2f%% of the region's pixels moved: %s" % (changed_pct, detail))


def main(argv):
    if len(argv) != 4:
        sys.stdout.write(__doc__)
        return 1
    code, evidence = check(argv[1], argv[2], argv[3])
    print(evidence)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
