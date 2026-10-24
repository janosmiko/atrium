// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The injectable clocks the surface modules take, so a test can drive time
// without waiting for it.
//
// TWO of them, not one, and that is the point of moving them here. Five files
// each declared a local `REAL_CLOCK` and four of them were identical -
// setTimeout, one shot. The fifth, skylight-day-cycle.js, used setInterval and
// REPEATS, under the same name. Reading any one file gave no hint that the name
// meant two different things elsewhere.
//
// The axis a caller cares about is one-shot versus repeating, so that is what
// the names say. "Real" only separates these from a test's fake.

/** Fires once, `ms` from now. */
export const REAL_TIMEOUT_CLOCK = {
  set: (ms, fn) => setTimeout(fn, ms),
  clear: (handle) => clearTimeout(handle),
};

/** Fires every `ms` until cleared. */
export const REAL_INTERVAL_CLOCK = {
  set: (ms, fn) => setInterval(fn, ms),
  clear: (handle) => clearInterval(handle),
};
