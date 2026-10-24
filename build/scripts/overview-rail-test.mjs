#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Reaches the rail through renderOverview, the entry buildRail uses (see
// overview-wiring-test.mjs), not a test-built host.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import os from 'node:os';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');

const SECTIONS =
    ['downloads', 'spaces', 'archived', 'snoozed', 'shelf', 'notes'];

const dom = installDOM({pathname: '/sidebar'});
const root = dom.document.createElement('div');
root.setAttribute('id', 'atrium-root');
dom.document.body.append(root);

const {renderOverview} = await import(path.join(ATRIUM_DIR, 'overview.js'));
renderOverview(root);

test('each of the six rail buttons switches the pane and marks ' +
         'itself active; the previous section unmounts',
     () => {
       const rail = root.querySelector('.ovrail');
       const pane = root.querySelector('.ovpane');
       assert.equal(rail.querySelectorAll('.ovbtn').length, 6);

       let previousContent = pane.firstElementChild;
       let previousId = rail.querySelector('.ovbtn.on')?.dataset.ovsec;
       for (const id of SECTIONS) {
         const button = rail.querySelector(`[data-ovsec="${id}"]`);
         button.click();
         if (id === previousId) {
           // Rebuilding the section on screen cost frames mid-glide.
           assert.equal(pane.firstElementChild, previousContent,
               `${id} is already shown, so its content stays mounted`);
           continue;
         }
         previousId = id;

         assert.ok(button.classList.contains('on'),
             `${id} button must be marked active after its own click`);
         for (const other of rail.querySelectorAll('.ovbtn')) {
           if (other === button) continue;
           assert.ok(!other.classList.contains('on'),
               `${other.dataset.ovsec} must lose active state when ${
                   id} is selected`);
         }

         const content = pane.firstElementChild;
         assert.ok(content, `${id} must render pane content`);
         if (previousContent) {
           assert.notEqual(content, previousContent,
               'switching sections must unmount the old node, not just ' +
                   'hide it');
         }
         previousContent = content;
       }
     });

test('the active section survives a surface rebuild (does not reset ' +
         'to Downloads)',
     async () => {
       const rail = root.querySelector('.ovrail');
       rail.querySelector('[data-ovsec="shelf"]').click();

       const {renderOverview} =
           await import(path.join(ATRIUM_DIR, 'overview.js'));
       renderOverview(root);

       const rebuiltShelf = root.querySelector('[data-ovsec="shelf"]');
       assert.ok(rebuiltShelf.classList.contains('on'),
           'a rebuilt surface must keep the previously active section');
       const rebuiltDownloads = root.querySelector('[data-ovsec="downloads"]');
       assert.ok(!rebuiltDownloads.classList.contains('on'),
           'a rebuilt surface must not silently fall back to Downloads');
     });
