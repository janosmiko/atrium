#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
import {test} from 'node:test';
import assert from 'node:assert/strict';
import {waitFor} from './wait-for.mjs';

test('waitFor resolves once the predicate turns true, even past a fixed ' +
         'margin the old sleep(200)/sleep(500) pattern would have missed',
     async () => {
       let ready = false;
       setTimeout(() => {
         ready = true;
       }, 300);
       await waitFor(() => ready);
       assert.equal(ready, true);
     });

test('waitFor returns immediately once the predicate is already true', async () => {
  const start = Date.now();
  await waitFor(() => true);
  assert.ok(Date.now() - start < 50, 'must not wait out a full interval tick');
});

test('waitFor throws once its own timeout elapses without the predicate ' +
         'turning true',
     async () => {
       await assert.rejects(
           () => waitFor(() => false, {timeout: 50, interval: 10}),
           /condition not met within 50ms/);
     });
