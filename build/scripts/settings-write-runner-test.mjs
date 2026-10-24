#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// settings.json has exactly one writer runner, whoever starts a save.
//
//   node --test build/scripts/settings-write-runner-test.mjs

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM = path.join(SRC, 'chrome/browser/ui/atrium');

// Where the one runner lives. Everything else must borrow it.
const OWNER = 'atrium_settings_source.cc';

// Two sequenced runners are two racers: atrium_core_save_settings reads, merges
// and renames per call with no lock, so the older edit can rename last and
// replace the newer, both reporting success.
export function offenders(sources) {
  const bad = [];
  for (const [name, text] of Object.entries(sources)) {
    if (name === OWNER || name.includes('_unittest.')) {
      continue;
    }
    if (!/\bSaveSettingsJSON\s*[(,)]/.test(text)) {
      continue;
    }
    if (/CreateSequencedTaskRunner/.test(text)) {
      bad.push(`${name} builds its own sequenced runner for a settings write`);
    } else if (!/\bSettingsWriteRunner\s*\(\s*\)/.test(text)) {
      bad.push(`${name} posts a settings write off the shared runner`);
    }
  }
  return bad;
}

function atriumSources() {
  const out = {};
  for (const entry of fs.readdirSync(ATRIUM)) {
    if (entry.endsWith('.cc') || entry.endsWith('.mm')) {
      out[entry] = fs.readFileSync(path.join(ATRIUM, entry), 'utf8');
    }
  }
  return out;
}

test('the shared settings write runner exists and is sequenced', () => {
  const header = fs.readFileSync(
      path.join(ATRIUM, 'atrium_settings_source.h'), 'utf8');
  const impl = fs.readFileSync(path.join(ATRIUM, OWNER), 'utf8');
  assert.match(header, /scoped_refptr<base::SequencedTaskRunner>\s+SettingsWriteRunner\(\);/);
  assert.match(impl, /SettingsWriteRunner\(\)\s*{[\s\S]*?CreateSequencedTaskRunner/);
  // BLOCK_SHUTDOWN: the edit most likely to be lost is the last one before quit.
  assert.match(impl, /SettingsWriteRunner\(\)\s*{[\s\S]*?BLOCK_SHUTDOWN/);
});

test('every settings writer shares that one runner', () => {
  assert.deepEqual(offenders(atriumSources()), []);
});

// Both halves of the CRITICAL this guard was written for: the settings window
// and the address bar pin used to own a runner each.
test('the two known writers are both on the shared runner', () => {
  const sources = atriumSources();
  for (const name of ['atrium_settings_window.cc',
                      'atrium_browser_window_surface_routes.cc']) {
    assert.ok(sources[name], `${name} is on disk`);
    assert.match(sources[name], /\bSettingsWriteRunner\s*\(\s*\)/,
                 `${name} posts through the shared runner`);
  }
});

// A guard nobody can watch fail is a guard nobody should trust.
test('the guard fails on the shape it was written to catch', () => {
  const ownRunner = {
    'atrium_thing.cc':
        'base::ThreadPool::CreateSequencedTaskRunner({});\n' +
        'runner->PostTaskAndReplyWithResult(FROM_HERE,\n' +
        '    base::BindOnce(&SaveSettingsJSON, path, json));',
  };
  assert.deepEqual(offenders(ownRunner), [
    'atrium_thing.cc builds its own sequenced runner for a settings write',
  ]);

  const barePost = {
    'atrium_thing.cc':
        'base::ThreadPool::PostTaskAndReplyWithResult(FROM_HERE, {},\n' +
        '    base::BindOnce(&SaveSettingsJSON, path, json));',
  };
  assert.deepEqual(offenders(barePost), [
    'atrium_thing.cc posts a settings write off the shared runner',
  ]);

  const shared = {
    'atrium_thing.cc':
        'SettingsWriteRunner()->PostTaskAndReplyWithResult(FROM_HERE,\n' +
        '    base::BindOnce(&SaveSettingsJSON, path, json));',
  };
  assert.deepEqual(offenders(shared), []);
});
