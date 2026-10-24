// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Reads the VM's persisted spaces.json.
//
// Every sidebar probe asserts against this rather than the DOM: the DOM shows
// what the surface drew, which is the thing under test, so believing it is
// circular. spaces.json is what the browser actually committed.
//
// The save is DEBOUNCED. Read it straight after an action and the old file
// comes back, which reads as "nothing happened" - a wrong conclusion drawn
// three times in one session before the wait went in.
import {execFileSync} from 'node:child_process';
import os from 'node:os';
import path from 'node:path';

const REPO = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..', '..');

// Mirrors sidebar-regression.sh: only the Tart bridge subnet may be
// targeted - an allowlist, so a form we did not think of (IPv6, a zone id,
// a hostname, an empty host) fails closed instead of passing a denylist.
function assertBridgeAddress(addr) {
  const bare = addr.replace(/^[^@]*@/, '');
  if (!/^192\.168\.64\.[0-9]{1,3}$/.test(bare)) {
    throw new Error(`refusing VM address '${addr}' - not the Tart bridge subnet (192.168.64.x)`);
  }
  return addr;
}

// The address changes every time the VM is re-cloned, so ask tart for it.
// It used to be pinned here, and a moved VM made probes hang on the connect.
export function vmHost() {
  let addr = process.env.ATRIUM_VM_HOST;
  if (!addr) {
    const ip = execFileSync(path.join(REPO, 'build/scripts/vm.sh'), ['ip'], {encoding: 'utf8'}).trim();
    if (!ip) throw new Error('no VM address - start it with make vm-start, or set ATRIUM_VM_HOST');
    addr = `admin@${ip}`;
  }
  return assertBridgeAddress(addr);
}

const VM = vmHost();
// Same default as vm.sh: TART_HOME, falling back to tart's own ~/.tart.
export function probeKeyPath() {
  return path.join(process.env.TART_HOME || path.join(os.homedir(), '.tart'), 'atrium-probe-key');
}
const KEY = probeKeyPath();
const FILE = "~/Library/Application Support/Atrium/spaces.json";

/** How long to wait for the debounced save before reading. */
export const SAVE_SETTLE_MS = 8000;

export const settle = (ms = SAVE_SETTLE_MS) =>
    new Promise((r) => setTimeout(r, ms));

/** The whole persisted document. */
export function readModel() {
  const out = execFileSync('ssh', [
    '-i', KEY, '-o', 'StrictHostKeyChecking=no', '-o', 'UserKnownHostsFile=/dev/null',
    '-o', 'LogLevel=ERROR', VM,
    `python3 -c "import json,os;print(json.dumps(json.load(open(os.path.expanduser('${FILE}')))))"`,
  ], {encoding: 'utf8'});
  return JSON.parse(out);
}

/** `[[name, [tab titles]], ...]` in stored order. */
export function spaceTitles() {
  return readModel().spaces.map((s) => [s.name, s.tabs.map((t) => t.title || '')]);
}
