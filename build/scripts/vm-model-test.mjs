#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The probe key must resolve under TART_HOME the way vm.sh does, or ssh
// fails with an identity-file error - the key never existed at ~/tart.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';

// Bypasses vmHost()'s vm.sh ip call, made at import time regardless of
// which export is used.
process.env.ATRIUM_VM_HOST = 'admin@192.168.64.250';

const {probeKeyPath} = await import('./vm-model.mjs');

test('probeKeyPath honors a set TART_HOME', () => {
  const prior = process.env.TART_HOME;
  process.env.TART_HOME = '/tmp/custom-tart';
  try {
    assert.equal(probeKeyPath(), '/tmp/custom-tart/atrium-probe-key');
  } finally {
    if (prior === undefined) delete process.env.TART_HOME;
    else process.env.TART_HOME = prior;
  }
});

test('probeKeyPath defaults to ~/.tart like vm.sh', () => {
  const prior = process.env.TART_HOME;
  delete process.env.TART_HOME;
  try {
    assert.equal(probeKeyPath(), path.join(os.homedir(), '.tart', 'atrium-probe-key'));
  } finally {
    if (prior === undefined) delete process.env.TART_HOME;
    else process.env.TART_HOME = prior;
  }
});
