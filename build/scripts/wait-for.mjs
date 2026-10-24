// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// A fixed real-ms sleep races the module's own real setTimeout - under
// system load the wait can elapse before the timer it depends on fires.
// Polling the actual outcome removes the margin instead of widening it.
export async function waitFor(predicate, {timeout = 2000, interval = 10} = {}) {
  const deadline = Date.now() + timeout;
  while (Date.now() < deadline) {
    if (predicate()) {
      return;
    }
    await new Promise((resolve) => setTimeout(resolve, interval));
  }
  if (!predicate()) {
    throw new Error(`waitFor: condition not met within ${timeout}ms`);
  }
}
