// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

// Bounds `value` to [lo, hi], defaulting to the unit range. A non-finite
// value (load-fill.js sees this before the first progress event) answers
// `lo` rather than propagating NaN through Math.min/max.
export function clamp(value, lo = 0, hi = 1) {
  const n = Number(value);
  if (!Number.isFinite(n)) {
    return lo;
  }
  return Math.min(hi, Math.max(lo, n));
}
