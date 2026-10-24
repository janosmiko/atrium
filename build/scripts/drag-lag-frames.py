#!/usr/bin/env python3
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
"""Reads a sidebar-drag burst and prints how many frames the page trails the card.

Usage: drag-lag-frames.py <frames-dir> [row] [bar-width]

Each frame row is scanned left to right. The card's left edge is the first
white pixel after the sidebar, the bar's left edge is the first red pixel,
and the card's right edge is the last white or red pixel. The page is settled
when the bar's left edge sits bar-width before the card's right edge. Runs
over the frames the runner pulled, so ffmpeg decodes the PNGs here.
"""
import glob
import os
import subprocess
import sys


def row_pixels(png, row, width=1024):
    raw = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", png, "-vf", f"crop={width}:1:0:{row}",
         "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
        check=True, capture_output=True).stdout
    return [tuple(raw[i:i + 3]) for i in range(0, len(raw), 3)]


def is_white(p):
    return p[0] > 235 and p[1] > 235 and p[2] > 235


def is_red(p):
    return p[0] > 170 and p[1] < 90 and p[2] < 90


def edges(pixels):
    card_left = next((x for x, p in enumerate(pixels) if is_white(p) or is_red(p)), None)
    bar_left = next((x for x, p in enumerate(pixels) if is_red(p)), None)
    card_right = next((x for x in range(len(pixels) - 1, -1, -1)
                       if is_white(pixels[x]) or is_red(pixels[x])), None)
    return card_left, bar_left, card_right


def main():
    frames_dir = sys.argv[1]
    row = int(sys.argv[2]) if len(sys.argv) > 2 else 400
    bar = int(sys.argv[3]) if len(sys.argv) > 3 else 60
    frames = sorted(glob.glob(os.path.join(frames_dir, "frame-*.png")))
    if not frames:
        sys.exit(f"no frames under {frames_dir}")
    prev_left = None
    moving = 0
    trailing = 0
    worst = 0
    run = 0
    for png in frames:
        card_left, bar_left, card_right = edges(row_pixels(png, row))
        settled = (bar_left is not None and card_right is not None
                   and abs((card_right + 1 - bar_left) - bar) <= 1)
        moved = prev_left is not None and card_left != prev_left
        mark = ""
        if moved or run:
            moving += 1
            if not settled:
                trailing += 1
                run += 1
                worst = max(worst, run)
                mark = "  <- page trails"
            else:
                run = 0
        print(f"{os.path.basename(png)} card_left={card_left} bar_left={bar_left} "
              f"card_right={card_right} settled={int(settled)}{mark}")
        prev_left = card_left
    print(f"frames={len(frames)} moving={moving} trailing={trailing} longest_trail={worst}")


if __name__ == "__main__":
    main()
