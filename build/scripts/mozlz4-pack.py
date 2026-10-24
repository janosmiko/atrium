#!/usr/bin/env python3
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
"""Pack a file as mozLz4 with one literal-only LZ4 block.

Zen and Firefox read this shape, and so does Atrium's decoder. The import
fixtures keep the readable .json twin next to the packed file:
  python3 build/scripts/mozlz4-pack.py <in.json> <out.jsonlz4>
"""
import struct
import sys

with open(sys.argv[1], "rb") as f:
    data = f.read()
out = bytearray(b"mozLz40\0") + struct.pack("<I", len(data))
out.append(min(len(data), 15) << 4)
if len(data) >= 15:
    rest = len(data) - 15
    while rest >= 255:
        out.append(255)
        rest -= 255
    out.append(rest)
out += data
with open(sys.argv[2], "wb") as f:
    f.write(out)
