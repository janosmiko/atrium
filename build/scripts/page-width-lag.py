#!/usr/bin/env python3
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
"""Prints how long the page took to report each width the card was set to.

Usage: page-width-lag.py <results-dir> [run-log] [--budget-ms=N] [--selftest]

The card side comes from the run log's "Atrium: sidebar live width <n>" lines,
whose VLOG stamp is local wall clock. The page side comes from the scenario's
"eval widths ->" line, a JSON array of [Date.now(), innerWidth, how] the
fixture's sampler collected. Both clocks are the VM's, so the lag is a
subtraction.

The two speak different units: the card logs a SIDEBAR width and the page
reports a VIEWPORT width. They are joined by value, not by rank. Widening the
sidebar by n narrows the page by n, so sidebar + viewport is a constant, and
the settled end of the drag is what fixes it. Rank-joining looked simpler and
is wrong: the page coalesces ticks it never painted, so the k-th page report
is not the k-th card tick.
"""
import json
import os
import re
import sys

CARD = re.compile(r"^\[[^]]*?(\d{2})(\d{2})(\d{2})\.(\d{6}):VERBOSE1:[^]]*\] "
                  r"Atrium: sidebar live width (\d+)")
PAGE = re.compile(r"eval widths -> (.*)$")


def card_widths(run_log):
    """(ms-of-day, sidebar width) per live tick. Each line is already a change:
    SetSidebarWidthLive returns before the log when the width did not move."""
    out = []
    with open(run_log, errors="replace") as fh:
        for line in fh:
            m = CARD.match(line)
            if not m:
                continue
            h, mi, s, us, width = m.groups()
            ms = ((int(h) * 60 + int(mi)) * 60 + int(s)) * 1000 + int(us) // 1000
            out.append((ms, int(width)))
    return out


def page_widths(*logs):
    # results.log holds assertions only. An eval's value is printed by the
    # runner as it goes, so it lives in whatever the run's stdout was saved to.
    found = []
    for path in logs:
        if not path or not os.path.exists(path):
            continue
        with open(path, errors="replace") as fh:
            found += [m.group(1) for m in (PAGE.search(line) for line in fh) if m]
    if not found:
        return []
    raw = found[-1].strip()
    if raw.startswith('"'):
        raw = json.loads(raw)
    out = []
    for stamp, width, how in json.loads(raw):
        # Date.now() is epoch ms and the VLOG stamp is ms-of-day local. The VM
        # runs on local time, so the day's midnight is the only offset.
        out.append((stamp % 86400000, int(width), how))
    return out


def lags(card, page):
    """(sidebar width, card ms, page ms) for every card width the page reached.

    The first page entry is the width before the drag, which the sampler records
    as soon as the reset clears it. It is the baseline, not a response."""
    if not card or len(page) < 2:
        return [], None
    total = card[-1][1] + page[-1][1]
    card_at = {}
    for ms, width in card:
        card_at.setdefault(width, ms)
    out = []
    for ms, width, _ in page[1:]:
        sidebar = total - width
        if sidebar in card_at:
            out.append((sidebar, card_at[sidebar], ms))
    return out, total


def report(card, page, budget=None):
    joined, total = lags(card, page)
    print(f"card ticks={len(card)} {card[0][1]} -> {card[-1][1]} "
          f"over {card[-1][0] - card[0][0]}ms")
    print(f"page steps={len(page) - 1} {page[1][1]} -> {page[-1][1]} "
          f"over {page[-1][0] - page[1][0]}ms")
    print(f"sidebar + viewport = {total}")
    if not joined:
        print("\nno page report matched a logged card width - nothing to measure")
        return 1
    for sidebar, c, p in joined:
        print(f"  sidebar={sidebar:4d} card @{c % 100000:6d}  page @{p % 100000:6d}"
              f"  lag={p - c:5d}ms")
    values = sorted(p - c for _, c, p in joined)
    print(f"\nmatched {len(joined)} of {len(card)} card ticks")
    print(f"lag: min={values[0]}ms median={values[len(values) // 2]}ms "
          f"max={values[-1]}ms")
    settle = page[-1][0] - card[-1][0]
    print(f"settle after the last card tick: {settle}ms")
    if budget is None:
        return 0
    # The tail, not the median. A mid-drag lag is the page being honestly busy.
    # A tail is work the gesture already finished still arriving, which is the
    # reflow the user sees land after they let go.
    if settle > budget:
        print(f"FAIL: the page settled {settle}ms after the drag, budget {budget}ms")
        return 1
    print(f"PASS: settled within the {budget}ms budget")
    return 0


def selftest():
    base = 57600000
    # The card steps every 100ms. The page answers 50ms later and skips 262,
    # which is the coalescing that makes a rank join wrong.
    card = [(base + 100, 260), (base + 200, 262), (base + 300, 264)]
    page = [(base + 10, 746, "raf"), (base + 350, 742, "resize")]
    joined, total = lags(card, page)
    assert total == 264 + 742, total
    assert len(joined) == 1, joined
    sidebar, c, p = joined[0]
    assert (sidebar, p - c) == (264, 50), joined
    # A rank join would have paired page[0] with card[0] and called it 150ms.
    print("selftest OK: the value join skips the coalesced tick and reads 50ms")
    return 0


def main():
    if "--selftest" in sys.argv:
        return selftest()
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    results = args[0]
    run_log = args[1] if len(args) > 1 else None
    budget = next((int(a.split("=")[1]) for a in sys.argv[1:]
                   if a.startswith("--budget-ms=")), None)
    card = card_widths(os.path.join(results, "atrium-run.log"))
    page = page_widths(os.path.join(results, "results.log"), run_log)
    if not card:
        sys.exit("no 'sidebar live width' lines - is the build current?")
    if len(page) < 2:
        sys.exit("the page reported no width after the baseline - did the drag land?")
    return report(card, page, budget)


if __name__ == "__main__":
    sys.exit(main())
