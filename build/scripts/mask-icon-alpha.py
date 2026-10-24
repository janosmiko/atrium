#!/usr/bin/env python3
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
"""Punches real alpha transparency into a qlmanage-rendered icon PNG, and
pads the artwork to macOS's icon-grid margin.

qlmanage -t composites its SVG thumbnails onto an opaque white background,
so every pixel outside the icon's rounded-rect artwork bakes in
(255,255,255,255) instead of being transparent. All AppIconVariant SVGs (20
as of Brand Book v2) share one frame - a rounded rect at x=4,y=4,w=132,h=132,
rx=30 inside a 140x140 viewBox (see swift/AtriumUI/icons-src/*.svg) - so a single
analytic rounded-rect mask, scaled to the render's pixel size, recovers
the transparency without needing a different SVG renderer.

That frame fills 132/140 (~94.3%) of the canvas - effectively full-bleed -
which is why the Dock icon read visibly larger than neighboring apps
(Slack/Teams etc. keep Apple's ~10%-per-side icon-grid margin, matching the
documented ~824/1024 artwork ratio for a full-bleed macOS squircle). Rather
than re-drawing all 20 SVGs, TARGET_FRAME_RATIO below shrinks-and-recenters
the rendered artwork (mask included) onto the same 1024 canvas via inverse-
mapped bilinear resampling, so every shipped variant - and app.icns, built
from this output by build-icns.sh - gets the correct margin.
"""
import math
import struct
import sys
import zlib

FRAME_VIEWBOX = 140.0
FRAME_X, FRAME_Y, FRAME_W, FRAME_H, FRAME_R = 4.0, 4.0, 132.0, 132.0, 30.0

# Apple's stock full-bleed squircle artwork occupies ~824/1024 of the
# canvas. Our SVG frame already occupies FRAME_W/FRAME_VIEWBOX of it, so
# shrink by the ratio of the two to land on that target exactly.
TARGET_FRAME_RATIO = 824.0 / 1024.0
RESAMPLE_SCALE = TARGET_FRAME_RATIO / (FRAME_W / FRAME_VIEWBOX)

def read_png(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path} is not a PNG")
    pos = 8
    width = height = bitdepth = colortype = None
    idat = b""
    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        ctype = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        if ctype == b"IHDR":
            width, height, bitdepth, colortype = struct.unpack(">IIBB", chunk[:10])
        elif ctype == b"IDAT":
            idat += chunk
        pos += 8 + length + 4
    if bitdepth != 8 or colortype not in (2, 6):
        raise ValueError(f"{path}: expected 8-bit RGB/RGBA, got bitdepth={bitdepth} colortype={colortype}")
    channels = 4 if colortype == 6 else 3
    raw = zlib.decompress(idat)
    stride = width * channels
    out = bytearray(height * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(height):
        ftype = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        for x in range(stride):
            a = line[x - channels] if x >= channels else 0
            b = prev[x]
            c = prev[x - channels] if x >= channels else 0
            if ftype == 1:
                line[x] = (line[x] + a) & 0xFF
            elif ftype == 2:
                line[x] = (line[x] + b) & 0xFF
            elif ftype == 3:
                line[x] = (line[x] + ((a + b) // 2)) & 0xFF
            elif ftype == 4:
                pp = a + b - c
                pa, pb, pc = abs(pp - a), abs(pp - b), abs(pp - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 0xFF
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return width, height, channels, out


def write_rgba_png(path, width, height, rgba):
    def chunk(tag, payload):
        return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)

    stride = width * 4
    raw = bytearray(height * (stride + 1))
    for y in range(height):
        row_start = y * (stride + 1)
        raw[row_start] = 0
        raw[row_start + 1:row_start + 1 + stride] = rgba[y * stride:(y + 1) * stride]
    idat = zlib.compress(bytes(raw), 9)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", idat))
        f.write(chunk(b"IEND", b""))


def rounded_rect_coverage(px, py, cx, cy, half_w, half_h, radius):
    """Antialiased inside-coverage (0..1) of a pixel center for the rounded rect."""
    qx = abs(px - cx) - half_w + radius
    qy = abs(py - cy) - half_h + radius
    ax, ay = max(qx, 0.0), max(qy, 0.0)
    d = math.hypot(ax, ay) + min(max(qx, qy), 0.0) - radius
    return min(1.0, max(0.0, 0.5 - d))


def sample_bilinear(width, height, channels, buf, sx, sy):
    """Clamped bilinear sample of buf at continuous pixel coords (sx, sy).

    Coordinates outside [0, width) x [0, height) clamp to the nearest edge
    pixel rather than erroring - safe here because RESAMPLE_SCALE < 1 means
    the inverse-mapped source coordinates for outer canvas pixels land well
    outside the original frame, where rounded_rect_coverage is already ~0,
    so the clamped color never actually shows through.
    """
    fx = min(max(sx - 0.5, 0.0), width - 1.0)
    fy = min(max(sy - 0.5, 0.0), height - 1.0)
    x0, y0 = int(fx), int(fy)
    x1, y1 = min(x0 + 1, width - 1), min(y0 + 1, height - 1)
    tx, ty = fx - x0, fy - y0

    def px(x, y):
        i = (y * width + x) * channels
        r, g, b = buf[i], buf[i + 1], buf[i + 2]
        a = buf[i + 3] if channels == 4 else 255
        return r, g, b, a

    p00, p10, p01, p11 = px(x0, y0), px(x1, y0), px(x0, y1), px(x1, y1)
    out = []
    for c in range(4):
        top = p00[c] + (p10[c] - p00[c]) * tx
        bot = p01[c] + (p11[c] - p01[c]) * tx
        out.append(top + (bot - top) * ty)
    return out


def mask_to_rounded_rect(width, height, channels, buf, tile_alpha=1.0):
    scale = width / FRAME_VIEWBOX
    half_w = (FRAME_W * scale) / 2
    half_h = (FRAME_H * scale) / 2
    cx = (FRAME_X * scale) + half_w
    cy = (FRAME_Y * scale) + half_h
    radius = FRAME_R * scale

    canvas_cx, canvas_cy = width / 2.0, height / 2.0

    rgba = bytearray(width * height * 4)
    for y in range(height):
        py = y + 0.5
        row_out = y * width * 4
        for x in range(width):
            px_ = x + 0.5
            # Inverse-map this output pixel back into the ORIGINAL (unshrunk)
            # render's coordinate space, then sample + mask there - a single
            # pass that both shrinks the artwork and recenters it, since the
            # rounded-rect mask constants above are still in that same space.
            local_x = (px_ - canvas_cx) / RESAMPLE_SCALE + canvas_cx
            local_y = (py - canvas_cy) / RESAMPLE_SCALE + canvas_cy
            coverage = rounded_rect_coverage(local_x, local_y, cx, cy, half_w, half_h, radius)
            i_out = row_out + x * 4
            if coverage <= 0.0:
                continue
            r, g, b, a = sample_bilinear(width, height, channels, buf, local_x, local_y)
            rgba[i_out] = round(r)
            rgba[i_out + 1] = round(g)
            rgba[i_out + 2] = round(b)
            # The v2 pinwheel had a backdrop disc, so the mark could stay opaque
            # while the tile faded around it. The v3 flock has no solid region
            # to spare, and a disc-shaped step would show as a ring.
            rgba[i_out + 3] = round(coverage * a * tile_alpha)
    return rgba


def main():
    args = sys.argv[1:]
    tile_alpha = 1.0
    if len(args) == 4 and args[0] == "--tile-alpha":
        tile_alpha = float(args[1])
        args = args[2:]
    if len(args) != 2:
        print(f"usage: {sys.argv[0]} [--tile-alpha 0..1] <in.png> <out.png>", file=sys.stderr)
        return 1
    src, dst = args
    width, height, channels, buf = read_png(src)
    rgba = mask_to_rounded_rect(width, height, channels, buf, tile_alpha)
    write_rgba_png(dst, width, height, rgba)
    return 0


if __name__ == "__main__":
    sys.exit(main())
