#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the rail's panes must each render their OWN space.
//
//   make render-test
//
// The bug this exists to catch is a swipe that shows the wrong contents. Every
// pane used to draw the ACTIVE space's rows and tiles retinted, so sliding to a
// neighbour showed the outgoing space's icons until a rebuild landed after the
// animation had finished - the lag reads as the whole surface being slow.
// The rows and the favourites grid are one line each: they will regress the
// same way.
//
// The other half of the same rule is the ACTIVE pane, which deliberately reads
// the TOP-LEVEL fields instead of its per-space copy. Only the active space's
// rows carry live tab ids (SidebarModelJSON hands non-active spaces cold rows,
// and MergeLiveSelection can only mark what has an id) - so an active pane fed
// from the per-space copy renders rows the browser cannot act on at all.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

// sidebar.js builds nodes at call time, not at import time, but the modules it
// pulls in are installed against a document - so the shim goes first.
const dom = installDOM();

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const {renderSidebar, buildRail, buildSpaceDots, markActiveSpaceDot,
       buildUrlRow, buildFavGrid, pinnedFavGrid, markActiveSpaceUrl,
       createFoldState, PINNED_FOLD_ID, installSidebar, setSplitMembers} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/sidebar.js'));
const {contrastRatio, parseRgb, rowBackdrop, RING_MIN_CONTRAST,
       NATIVE_SEL_ALPHA} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/icon-color.js'));

// Every snapshot carries a scope: the browser mints one per profile, and a
// row's URL-keyed icon path is unreachable without it.
const SCOPE = 'testscope';

const render = (snapshot) => {
  const root = document.createElement('div');
  renderSidebar(root, {iconScope: SCOPE, ...snapshot});
  return root;
};

// The flat tab list, in order. `.rows` is the ungrouped section, so this
// deliberately excludes pinned and folder rows.
const rowTitles = (el) =>
    el.querySelectorAll('.rows > .tabrow .tl').map((n) => n.textContent);
const rowIds = (el) =>
    el.querySelectorAll('.rows > .tabrow').map((n) => n.dataset.tabId);
const favIds = (el) => el.querySelectorAll('.fav').map((n) => n.dataset.favId);
// SPACE panes only. An Overview pane sits at rail position 0, so
// an unfiltered query would shift every positional assertion below by one and
// they would silently stop testing the space they name.
const panes = (rail) => rail.querySelectorAll('.railpane')
                            .filter((p) => !p.classList.contains('ovrailpane'));

// A shape comparison that ignores node identity: what a re-render must not
// change is the tree, not the objects.
const shape = (el) => JSON.stringify(el.children.map(function walk(node) {
  return [node.tagName, [...node._attrs.entries()], node.textContent,
          node.children.map(walk)];
}));

// --- renderSidebar ----------------------------------------------------------

// The C++ fall-through path (BuildSidebarTabsJSON without the dylib) emits a
// snapshot holding `tabs` and nothing else, so every other field being absent
// is an ordinary state rather than a malformed push.
test('a snapshot with no fields at all still renders a sidebar', () => {
  const root = render({});
  assert.equal(root.querySelectorAll('.sidebar').length, 1);
  assert.equal(root.querySelector('.wsname').textContent, 'Space',
      'a nameless space still gets a label rather than a blank strip');
  assert.equal(root.querySelectorAll('.newtab').length, 1);
});

test('a snapshot carrying only tabs renders those tabs', () => {
  const root = render({tabs: [{id: '1', title: 'One'}, {id: '2', title: 'Two'}]});
  assert.deepEqual(rowTitles(root), ['One', 'Two']);
});

// The three sit together in the scroll zone, so an odd one out reads as a
// mistake rather than a hierarchy. Sizes drift apart one rule at a time.
test('the workspace row and the new-tab button match the tab row box', () => {
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
  const ruleFor = (selector) => {
    const at = css.indexOf(selector + ' {');
    assert.notEqual(at, -1, `no rule for ${selector}`);
    return css.slice(at, css.indexOf('}', at));
  };
  const boxOf = (selector) => {
    const rule = ruleFor(selector);
    return {
      height: rule.match(/height:\s*(\d+)px/)?.[1],
      padding: rule.match(/padding:\s*([^;]+);/)?.[1].trim(),
    };
  };
  const row = boxOf('.row');
  assert.equal(row.height, '36', 'the tab row is the reference height');
  assert.deepEqual(boxOf('.wsname'), row, 'the workspace row must match a tab row');
  assert.deepEqual(boxOf('.newtab'), row, 'the new-tab button must match a tab row');
});

// Computed from each row's own padding + icon width + gap, not a literal, so
// a future change to any of those three numbers fails here instead of drifting.
test('the workspace row, a tab row, and New Tab all start their text at ' +
         'the same origin',
     () => {
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
  const ruleFor = (selector) => {
    const at = css.indexOf(selector + ' {');
    assert.notEqual(at, -1, `no rule for ${selector}`);
    return css.slice(at, css.indexOf('}', at));
  };
  const numProp = (rule, prop) =>
      Number(rule.match(new RegExp(prop + ':\\s*(\\d+(?:\\.\\d+)?)px'))[1]);
  const paddingLeft = (rule) =>
      Number(rule.match(/padding:\s*\S+\s+\S+\s+\S+\s+(\d+(?:\.\d+)?)px/)[1]);
  const textOrigin = (rowSel, iconSel) => {
    const row = ruleFor(rowSel);
    return paddingLeft(row) + numProp(ruleFor(iconSel), 'width') +
        numProp(row, 'gap');
  };

  const row = textOrigin('.row', '.row .fv');
  const wsname = textOrigin('.wsname', '.wsic');
  const newtab = textOrigin('.newtab', '.newtab .nti');
  assert.equal(wsname, row, `.wsname text starts at ${wsname}, .row at ${row}`);
  assert.equal(newtab, row, `.newtab text starts at ${newtab}, .row at ${row}`);
});

// The pane and the hoisted address row draw at different levels of the tree,
// so nothing but this keeps their side insets equal. Two of them were left
// behind the last time the inset moved.
test('every box that sets a sidebar side inset reads the one token', () => {
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
  const ruleFor = (selector) => {
    const at = css.indexOf(selector + ' {');
    assert.notEqual(at, -1, `no rule for ${selector}`);
    return css.slice(at, css.indexOf('}', at));
  };
  const insets = /var\(--sb-pad-trail\)[^;]*var\(--sb-pad-lead\)/;
  assert.match(ruleFor('.sidebar'), insets,
      'the pane must inset by the tokens');
  const urlrow = ruleFor('#atrium-root.sbroot > .urlrow');
  assert.match(urlrow, /left:\s*var\(--sb-pad-lead\)/,
      'the hoisted address row must lead by the token');
  assert.match(urlrow, /right:\s*var\(--sb-pad-trail\)/,
      'the hoisted address row must trail by the token');
  // The dot row is the exception, and deliberately: --sb-pad-lead and
  // --sb-pad-trail differ by 6px, and centring inside that box put the chips
  // 3px right of the row's own middle.
  const dots = ruleFor('.rail ~ .spacedots');
  assert.doesNotMatch(dots, /var\(--sb-pad-(lead|trail)\)/,
      'the dot row centres its chips, so an asymmetric inset moves them');
});

// PushSidebarInk pushes black ink with --atrium-sel-alpha .6 over a light
// backdrop. An ink-wash hover then darkens the row while the white selection
// lightens it, and the hovered row outweighs the selected one.
test('hover and selection wash the row the same way, hover by less', () => {
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
  const at = css.indexOf('html:not(.atrium-native-chrome) #atrium-root.sbroot {');
  assert.notEqual(at, -1, 'no glass-branch token block');
  const block = css.slice(at, css.indexOf('}', at));
  const tokenFor = (name) => {
    const m = block.match(new RegExp(`--${name}:\\s*([^;]+);`));
    assert.ok(m, `no --${name} in the glass block`);
    return m[1].trim();
  };
  // Resolves the three shapes this block uses into {ink, alpha}.
  const resolve = (value, selAlpha) => {
    const ink = value.includes('var(--atrium-ink)') ? 'ink' : 'white';
    const scaled = value.match(/calc\(\s*var\(--atrium-sel-alpha[^)]*\)\s*\*\s*([\d.]+)\s*\)/);
    const plain = value.match(/var\(--atrium-sel-alpha[^)]*\)/);
    const literal = value.match(/\/\s*([\d.]+)\s*\)/);
    const alpha = scaled ? selAlpha * Number(scaled[1])
        : plain ? selAlpha
        : Number(literal[1]);
    return {ink, alpha};
  };
  // Composite over a backdrop, in the sRGB space the compositor blends in.
  const over = ({ink, alpha}, backdrop, inkLevel) =>
      backdrop * (1 - alpha) + inkLevel * alpha;
  // The two states PushSidebarInk produces: white ink on a dark backdrop, and
  // black ink on a light one, each with the sel alpha it pushes alongside.
  for (const {name, inkLevel, backdrop, selAlpha} of [
    {name: 'white ink, dark backdrop', inkLevel: 255, backdrop: 40, selAlpha: .3},
    {name: 'black ink, light backdrop', inkLevel: 0, backdrop: 200, selAlpha: .6},
  ]) {
    const level = (token) => {
      const parsed = resolve(tokenFor(token), selAlpha);
      return over(parsed, backdrop, parsed.ink === 'ink' ? inkLevel : 255);
    };
    const hover = level('fillhover') - backdrop;
    const sel = level('selfill') - backdrop;
    const base = level('fill') - backdrop;
    assert.ok(Math.sign(hover) === Math.sign(sel),
        `${name}: hover moves ${hover.toFixed(1)} and selection ${sel.toFixed(1)} - ` +
        'opposite directions make the hovered row read as the selected one');
    assert.ok(Math.abs(hover) < Math.abs(sel),
        `${name}: hover steps ${Math.abs(hover).toFixed(1)} but selection only ` +
        `${Math.abs(sel).toFixed(1)} - the hover must be the smaller step`);
    assert.ok(Math.abs(base) < Math.abs(hover),
        `${name}: the resting fill must be quieter than the hover`);
    assert.ok(Math.abs(hover) >= 8,
        `${name}: a ${Math.abs(hover).toFixed(1)} step is not a visible hover`);
  }
  // The other half of the chain the VM scenario cannot reach: it resolves the
  // tokens, this pins the rules that draw them.
  const ruleFor = (selector) => {
    const i = css.indexOf(selector + ' {');
    assert.notEqual(i, -1, `no rule for ${selector}`);
    return css.slice(i, css.indexOf('}', i));
  };
  assert.match(ruleFor('.row.tabrow:not(.sel):is(:hover, .hoverhold)'),
               /--rowfill:\s*var\(--fillhover/);
  assert.match(ruleFor('.row.tabrow.sel'), /background:\s*var\(--selfill/);
});

// A hover fill flush to the selected row's own edge reads as the larger box of
// the two, which is how it was reported against Arc.
test('the hover fill keeps the ring\'s width clear of the row edge', () => {
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
  const ruleFor = (selector) => {
    const at = css.indexOf(selector + ' {');
    assert.notEqual(at, -1, `no rule for ${selector}`);
    return css.slice(at, css.indexOf('}', at));
  };
  // One token, so the ring and the gap the hover leaves inside it cannot drift.
  assert.match(ruleFor('#atrium-root.sbroot'), /--selring:\s*2px/);
  assert.match(ruleFor('.brdr::after'), /border:\s*var\(--selring\)\s+solid/,
      'the ring must read the token the hover is inset by');
  const layer = ruleFor('.row::before,\n.wsname::before,\n.newtab::before');
  assert.match(layer, /inset:\s*var\(--selring\)/);
  assert.match(layer, /border-radius:\s*calc\(8px - var\(--selring\)\)/,
      'an inset box needs the smaller radius, or its corners bulge');
  assert.match(layer, /background:\s*var\(--rowfill/);
  assert.match(layer, /z-index:\s*-1/, 'the fill paints behind the label');
  // Every surface that lights up on hover must host the layer, or its fill
  // would have nowhere to paint and the hover would vanish entirely.
  for (const host of ['.row', '.wsname', '.newtab']) {
    assert.match(ruleFor(host), /isolation:\s*isolate/,
        `${host} must contain its own z-index -1 fill`);
    assert.match(ruleFor(host), /position:\s*relative/);
  }
  // No hover rule may paint the row's own background again: that box is the
  // full one, and it would put the fill back on the edge this test defends.
  for (const sel of ['.row.tabrow:not(.sel):is(:hover, .hoverhold)',
                     '.row.frow:is(:hover, .hoverhold)',
                     '.wsname.frow:is(:hover, .hoverhold)', '.newtab:hover']) {
    assert.doesNotMatch(ruleFor(sel), /^\s*background:/m,
        `${sel} still paints the full box`);
    assert.match(ruleFor(sel), /--rowfill:/);
  }
});

// A tile ring narrower than a row ring makes the two selections read as
// different treatments side by side, which is how it was reported. The lab
// dropped its own 1.5px tile override for the same reason (swipe-lab.html:193).
test('a selected favourite tile draws its ring at the shared width', () => {
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
  for (const rule of css.match(/\.fav[^{}]*\.brdr::after\s*{[^}]*}/g) || []) {
    assert.doesNotMatch(rule, /border(?:-width)?:/,
        `a tile-only ring width puts the two selections back out of step: ${rule}`);
  }
});

// A row or tile that thickens the moment a load starts, then thins when it
// settles, reads as the box resizing rather than as a ring filling.
// The plain sweep paints one colour, the dominant one, and the comet in that
// colour. A palette tile keeps its gradient under the sweep and lets the comet
// brighten it as it passes.
test('a palette tile sweeps over its own colours, not one mixed colour', () => {
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
  const rule = /\.palette > \.brdr::before\s*{([^}]*)}/.exec(css);
  assert.ok(rule, 'no sweep rule for a palette tile');
  assert.match(rule[1], /background:\s*var\(--ringgrad\)/,
      'the sweep must paint the palette gradient');
  assert.doesNotMatch(rule[1], /var\(--ringb\)/,
      'the dominant colour must not reach a palette sweep');
  const mask = /\n\s*mask:\s*([^;]*);/.exec(rule[1]);
  assert.ok(mask, 'no unprefixed mask');
  // The comet is the mask now: --sweep turns the bright arc over the same
  // floor the plain sweep's base ring has, and the two ring-cutting layers
  // stay below it.
  const layers = mask[1].replace(/\s+/g, ' ');
  assert.match(layers, /^conic-gradient\(from var\(--sweep, 0deg\)/);
  const floor = /color-mix\(in srgb, var\(--ringb\) (\d+)%, transparent\)/.exec(css);
  assert.ok(floor, 'the plain sweep has no base ring');
  const alpha = Number(floor[1]) / 100;
  assert.match(layers, new RegExp(
      `rgba\\(0, 0, 0, ${String(alpha).replace('0.', '\\.')}\\) 0deg, #000 79deg, ` +
      `rgba\\(0, 0, 0, ${String(alpha).replace('0.', '\\.')}\\) 162deg`));
  assert.equal((layers.match(/linear-gradient\(#000 0 0\)/g) || []).length, 2);
  assert.match(rule[1], /mask-composite:\s*intersect, exclude/);
});

// The loading ring dipped to under half its colour, both between pulses and
// under the comet, which is what read as a dim border while a page loaded.
test('a loading ring never dims below 70% of its colour', () => {
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
  const pulse = /@keyframes ringpulse\s*{([^}]*}[^}]*}[^}]*)}/.exec(css);
  assert.ok(pulse, 'no ringpulse keyframes');
  const low = /--pulse:\s*(\.\d+|\d(?:\.\d+)?)/.exec(pulse[1]);
  assert.ok(Number(low[1]) >= 0.7, `the pulse dips to ${low[1]}`);
  const floor = /color-mix\(in srgb, var\(--ringb\) (\d+)%, transparent\)/.exec(css);
  assert.ok(Number(floor[1]) >= 70, `the comet's base ring is ${floor[1]}%`);
});

test('a loading row keeps the width its selected ring has', () => {
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
  const at = css.indexOf('.brdr::before {');
  assert.notEqual(at, -1, 'no rule for .brdr::before');
  assert.match(css.slice(at, css.indexOf('}', at)),
      /padding:\s*var\(--selring\)/,
      'the sweep must read the token the ring reads, not repeat the number');
  // Scoped overrides are what split the two apart before, so a narrower rule
  // reintroducing one has to fail here even at a different selector.
  for (const rule of css.match(/\.\w[^{}]*\.brdr::before\s*{[^}]*}/g) || []) {
    assert.doesNotMatch(rule, /padding:/,
        `a scoped sweep width puts the ring and the sweep out of step: ${rule}`);
  }
});

test('an empty favourites grid is still a drop target', () => {
  // The grid is how a tab is promoted. Rendering nothing when there
  // are no favourites would make the FIRST one impossible to create by drag.
  const grid = render({}).querySelector('.favgrid');
  assert.equal(grid.dataset.dropzone, 'favorites');
  assert.equal(grid.classList.contains('empty'), true);
});

// No layout engine in the dom-shim (see the header comment), so this pins
// the CSS text instead of computed style.
test('an empty favourites grid takes no room at rest, ' +
         'and shows its hint centred only once a drag opens it',
     () => {
       const hint = render({}).querySelector('.favhint');
       assert.notEqual(hint, null, 'the hint markup still renders');

       const cssPath =
           path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
       const css = fs.readFileSync(cssPath, 'utf8')
                       .replace(/\/\*[\s\S]*?\*\//g, '');

       const rest = css.match(/\.favgrid \.favhint\s*{[^}]*}/)[0];
       assert.match(rest, /display:\s*none/, 'hint hidden at rest');

       const opened = css.match(/\.favgrid\.dropopen \.favhint\s*{[^}]*}/)[0];
       assert.match(opened, /display:\s*block/, 'hint shown once open');

       const box = css.match(/\.favgrid\.empty\s*{[^}]*}/)[0];
       assert.match(box, /display:\s*none/, 'no room at rest');
       assert.match(box, /align-items:\s*center/);
       assert.match(box, /justify-content:\s*center/);

       const open = css.match(/\.favgrid\.empty\.dropopen\s*{[^}]*}/)[0];
       assert.match(open, /display:\s*flex/);
     });

// No layout engine in the dom-shim, so this pins the rule's shape rather
// than a computed column count - see favourites-grid-caps-at-four.json for
// the rendered column counts.
test('the favourites grid holds 2 columns at the narrow end and caps at 4', () => {
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
  const rule = css.match(/\.favgrid\s*{[^}]*}/)[0];
  const gap = Number(rule.match(/gap:\s*(\d+)px/)?.[1]);
  assert.ok(gap, `no gap in: ${rule}`);
  const calcMatch = rule.match(
      /minmax\(max\((\d+)px,\s*calc\(\(100% - (\d+)px\) \/ 4\)\)/);
  const floor = Number(calcMatch?.[1]);
  const calcSubtrahend = Number(calcMatch?.[2]);
  assert.ok(floor, `no auto-fill floor in: ${rule}`);
  // The subtrahend is three gaps, which is what caps the row at four columns -
  // a gap edited without this would silently break the cap instead of
  // failing here.
  assert.equal(calcSubtrahend, 3 * gap,
      `the calc subtrahend must be 3 * gap (${3 * gap}px), got ${calcSubtrahend}px`);

  // The grid measures the pane's content box, so both side insets decide every
  // width below. Read rather than repeated: an inset edit moves the step, and a
  // stale copy here would pass while the app dropped a column.
  const lead = Number(css.match(/--sb-pad-lead:\s*(\d+)px/)?.[1]);
  const trail = Number(css.match(/--sb-pad-trail:\s*(\d+)px/)?.[1]);
  assert.ok(lead && trail, 'no --sb-pad-lead / --sb-pad-trail tokens');
  const pad = lead + trail;

  // The floor is the whole rule. Both bounds are arithmetic on the real
  // sidebar widths, so a floor, gap, or padding that drops a column at either
  // end fails here rather than in the app.
  const columns = (width) => {
    const content = width - pad;
    const tile = Math.max(floor, (content - 3 * gap) / 4);
    // The epsilon is load-bearing: once the divisor governs, four tiles fill
    // the row exactly, and binary division lands a hair under the integer.
    return Math.floor((content + gap) / (tile + gap) + 1e-9);
  };
  assert.equal(columns(160), 2, 'the minimum width still holds 2 tiles');
  assert.equal(columns(220), 4, 'the default width shows 4');
  assert.equal(columns(380), 4, 'the overview width shows 4');
  assert.equal(columns(500), 4, 'the maximum width never reaches 5');
  // The fourth column has to sit under the default with room for the insets
  // to grow: the step is 4 * floor + 3 * gap + both insets.
  const step = 4 * floor + 3 * gap + pad;
  assert.ok(step <= 220 && step >= 210,
      `the fourth column must arrive just under the 220px default, got ${step}px`);
  assert.equal(columns(step), 4, 'the computed step really shows 4');
  assert.equal(columns(step - 1), 3, 'one pixel below the step still shows 3');

  const favRule = css.match(/\.fav\s*{[^}]*}/)[0];
  assert.match(favRule, /height:\s*var\(--fav-h\)/,
               'the tile height must stay pinned');
  assert.match(css, /--fav-h:\s*47px/, 'the tile height token must stay 47px');
});

test('an empty tab list is still a drop target', () => {
  // The favourites grid always had this and the tab list
  // did not: `if (tabs.length)` skipped buildRows outright, so with no rows
  // there was no [data-dropzone="tabs"] anywhere in the DOM and NOTHING could
  // be dragged into the tab list. A space whose every tab is favourite-backed
  // renders empty, so it is not a corner case - and it was reported three
  // times before the guard, rather than the zone's geometry, was the answer.
  const zone = render({}).querySelector('[data-dropzone="tabs"]');
  assert.ok(zone, 'the zone exists with no rows');
  assert.ok(zone.classList.contains('emptyzone'),
            'and carries its own height, because the hint is pointer-events:none');
});

test('renderSidebar returns the number of favourite tiles it drew', () => {
  // installSidebar reports this to C++ as sidebarFavoriteCount.
  assert.equal(renderSidebar(document.createElement('div'),
                             {favorites: [{id: 'a'}, {id: 'b'}, {id: 'c'}]}), 3);
  assert.equal(renderSidebar(document.createElement('div'), {}), 0);
});

// The strip is absolute in the pane as it is beside the rail, so a copy holds
// no room of its own - only the lane the rows above it give up.
const pins = (n) => Array.from({length: n}, (_, i) => (
    {id: `e${i}`, name: `e${i}`, actionTitle: `e${i}`, pinned: true}));

test('a pane of the pinned profile holds the lane and draws no copy', () => {
  const root = document.createElement('div');
  renderSidebar(root, {iconScope: SCOPE}, {extActions: pins(2)});
  assert.equal(root.querySelector('.exs-row'), null);
  assert.ok(root.querySelector('.sidebar').classList.contains('exs-lane'));
});

// A profile with nothing pinned draws no strip, so a lane held open for it
// would leave a blank band over its tiles.
test('a pane of a profile with no pins holds no lane', () => {
  const root = document.createElement('div');
  renderSidebar(root, {iconScope: SCOPE}, {extActions: [], extStrip: 'tiles'});
  assert.equal(root.querySelector('.exs-row'), null);
  assert.equal(root.querySelector('.sidebar').classList.contains('exs-lane'),
               false);
});

// The other profile's copy is on screen only mid-swipe. Real buttons there put
// a second set of the same chips in the tab order and under the reader, which
// is what buildFavRoom's empty slots avoid for the tiles.
test('another profile’s pane copy is inert and capped', () => {
  const root = document.createElement('div');
  renderSidebar(root, {iconScope: SCOPE},
                {extActions: pins(9), extStrip: 'tiles'});
  const copy = root.querySelector('.exs-row');
  assert.ok(copy, 'the pane has nothing to slide in with');
  assert.equal(copy.getAttribute('aria-hidden'), 'true');
  const chips = copy.querySelectorAll('.exs-chip');
  assert.equal(chips.length, 9);
  assert.ok(chips.every((c) => c.getAttribute('tabindex') === '-1'));
  const sidebar = root.querySelector('.sidebar');
  assert.ok(sidebar.classList.contains('exs-lane'));
  // Nine pins fill three rows, and the pane's own rows have to give up all
  // three. The class alone only says that some lane is open.
  assert.equal(sidebar.style.getPropertyValue('--exs-rows'), '3');
});

test('rendering the same snapshot twice leaves the same tree', () => {
  // The snapshot is re-pushed on every tab title change, so this runs
  // constantly. replaceChildren is what keeps it from stacking up.
  const snapshot = {
    spaceName: 'Work', spaceTint: '#f0a',
    favorites: [{id: 'f'}],
    pinned: [{id: 'p', title: 'Pin'}],
    folders: [{id: 'g', name: 'Group', tabs: [{id: 'x', title: 'In'}]}],
    tabs: [{id: 't', title: 'Tab'}],
    spaces: [{name: 'Work'}], activeSpace: 0,
  };
  const root = document.createElement('div');
  renderSidebar(root, snapshot);
  const first = shape(root);
  renderSidebar(root, snapshot);
  assert.equal(root.children.length, 1, 'no second .sidebar alongside the first');
  assert.equal(shape(root), first);
});

// Closing a favourite's tab leaves the browser with a snapshot that names no
// active tab, and the tile it opened kept its highlight over a page that was
// gone. Nothing on this surface may hold a selection the push does not carry.
test('a push naming no active tab draws no selected tile or row', () => {
  const root = document.createElement('div');
  const lit = {
    favorites: [{id: 'https://fav.test/', title: 'Fav', tabId: '42',
                 selected: true}],
    tabs: [{id: '42', title: 'Tab', url: 'https://fav.test/', selected: true}],
  };
  renderSidebar(root, lit);
  assert.equal(root.querySelectorAll('.fav.sel').length, 1,
               'the control: the tile was lit before the close');

  renderSidebar(root, {
    favorites: [{id: 'https://fav.test/', title: 'Fav', tabId: '', selected: false}],
    tabs: [{id: '', title: 'Tab', url: 'https://fav.test/', selected: false}],
  });
  assert.equal(root.querySelectorAll('.sel').length, 0,
               'neither the tile nor the row keeps a highlight the push dropped');
  assert.equal(root.querySelectorAll('.fav').length, 1,
               'the tile itself stays - a close never removes a favourite');
});

test('a re-render clears what was in the root before it', () => {
  const root = document.createElement('div');
  root.appendChild(document.createElement('p'));
  renderSidebar(root, {tabs: [{id: '1', title: 'One'}]});
  assert.deepEqual(root.children.map((n) => n.tagName), ['DIV'],
      'the stray node is gone, not rendered around');
});

// Titles and URLs come from the page, so they are attacker-controlled all the
// way to the DOM. The default WebUI CSP blocks the injection sinks, but the
// render must not rely on that being switched on.
test('a tab title lands as text, never as markup', () => {
  const hostile = '<img src=x onerror=alert(1)>';
  const title = render({tabs: [{id: '1', title: hostile}]}).querySelector('.rows .tl');
  assert.equal(title.textContent, hostile);
  assert.equal(title.children.length, 0, 'the title parsed into no elements');
});

test('a folder name and a space name land as text', () => {
  const hostile = '<script>alert(1)</script>';
  const root = render({spaceName: hostile,
                       folders: [{id: 'g', name: hostile, tabs: []}]});
  assert.equal(root.querySelector('.frow .tl').textContent, hostile);
  assert.equal(root.querySelector('.frow .tl').children.length, 0);
  // children[1]: the icon glyph is index 0, and falls back to the hostile
  // name's first character too, which would otherwise land in this string.
  assert.equal(root.querySelector('.wsname').children[1].textContent, hostile);
});

test('a row URL is carried as data, never as a navigable attribute', () => {
  // The click handler reads dataset.url and hands it to C++, which refuses
  // anything that is not http/https/about (OpenSidebarColdTab). Putting it in
  // an href here would route around that check entirely.
  const row = render({tabs: [{title: 'Cold', url: 'javascript:alert(1)'}]})
      .querySelector('.tabrow');
  assert.equal(row.dataset.url, 'javascript:alert(1)');
  assert.equal(row.querySelectorAll('a').length, 0);
  assert.equal(row.getAttribute('href'), null);
});

test('a favicon URL cannot escape the favicon path', () => {
  // The id is browser-supplied today, but the src is built by concatenation -
  // so the encode is the only thing between a crafted id and a request to some
  // other host-relative path.
  const img = render({tabs: [{id: '../../evil?x=1', title: 't'}]})
      .querySelector('.fvimg');
  assert.equal(img.src,
      '/favicon/' + SCOPE + '/..%2F..%2Fevil%3Fx%3D1.png');
});

test('a row with no live tab still carries its URL', () => {
  // A COLD row: persisted but with no live counterpart, so there is no id to
  // activate and the URL is the only thing that makes the click do anything
  //.
  const row = render({tabs: [{title: 'Cold', url: 'https://example.test/'}]})
      .querySelector('.tabrow');
  assert.equal(row.dataset.tabId, '');
  assert.equal(row.dataset.url, 'https://example.test/');
});

test('the ungrouped tab list declares itself as the tabs section', () => {
  // Two things read this. A drop resolves against it, and the click
  // handler only reopens a COLD row whose parent says `tabs` - a cold PINNED
  // row would come back as an ordinary tab and lose its pinning.
  // Without the dropzone every cold row is inert and every drop is unroutable.
  const root = render({tabs: [{title: 'Cold', url: 'https://example.test/'}],
                       pinned: [{id: 'p', title: 'P'}]});
  assert.equal(root.querySelector('.rows').dataset.dropzone, 'tabs');
  assert.equal(root.querySelector('.foldin').dataset.dropzone, 'pinned',
      'the pinned rows sit outside it, which is what makes the check work');
});

test('every row carries a close control', () => {
  // The click handler routes `.x` to sidebarTabClosed. With no button in the
  // row there is nothing to hit, so a tab could be opened but never closed.
  const root = render({tabs: [{id: '1', title: 'One'}],
                       pinned: [{id: 'p', title: 'P'}]});
  const closers = root.querySelectorAll('.tabrow .x');
  assert.deepEqual(closers.map((n) => n.tagName), ['BUTTON', 'BUTTON'],
      'the pinned row gets one too');
});

test('every row that can be dragged says so, grouped rows included', () => {
  // Reorder, pin and favourite are all drags, so the flag is what makes them
  // reachable at all. A grouped row drags out of its folder to the pinned or
  // the regular list, so it carries the flag too.
  const root = render({
    favorites: [{id: 'f'}],
    tabs: [{id: '1', title: 'One'}],
    pinned: [{id: 'p', title: 'P'}],
    folders: [{id: 'g', name: 'G', tabs: [{id: 'x', title: 'In'}]}],
  });
  const draggableOf = (sel) => !!root.querySelector(sel).draggable;
  assert.equal(draggableOf('.rows > .tabrow'), true);
  assert.equal(
      draggableOf(`.fold[data-folder-body="${PINNED_FOLD_ID}"] .tabrow`), true);
  assert.equal(draggableOf('.fold[data-folder-body="g"] .tabrow'), true,
      'a grouped row leaves its folder by drag');
  assert.equal(draggableOf('.fav'), true, 'the tiles reorder by drag');
});

test('an empty pinned list renders no section at all', () => {
  // The space name is the pin drop target now, so an empty pinned list
  // renders nothing of its own to aim at - see the test below.
  const root = render({tabs: [{id: '1', title: 'One'}]});
  assert.equal(root.querySelector('.pinstrip'), null);
  assert.equal(root.querySelectorAll(`[data-folder-id="${PINNED_FOLD_ID}"]`).length, 0);
});

test('the space name is the pin drop target, empty pinned list or full', () => {
  const empty = render({tabs: [{id: '1', title: 'One'}]});
  assert.equal(empty.querySelector('.wsname').dataset.dropzone, 'pinned');

  const full = render({pinned: [{id: 'p', title: 'P'}]});
  assert.equal(full.querySelector('.wsname').dataset.dropzone, 'pinned');
});

// An incognito window belongs to no space. Both the name and the favourites
// grid are a space's: the grid's tiles live on one and both accept a drop that
// writes to one, so a window with no space must offer neither.
test('an incognito snapshot draws no space name and no favourites grid', () => {
  const root = render({
    incognito: true,
    spaces: [],
    // Deliberately populated: the flag has to drop these, not their emptiness.
    spaceName: 'Work',
    spaceTint: '#4488cc',
    favorites: [{id: 'https://fav.example/', title: 'Fav'}],
    tabs: [{id: '1', title: 'One'}],
  });
  assert.equal(root.querySelector('.wsname'), null);
  assert.equal(root.querySelector('.favgrid'), null);
  assert.equal(root.querySelector('.spacedots'), null);
  assert.equal(root.textContent.includes('Work'), false);
});

// The zone's own drops all resolve to a space index on the browser side, so a
// draggable incognito row is a route into whichever space is active.
test('an incognito row is not draggable and its list is no drop target', () => {
  const root = render({
    incognito: true,
    spaces: [],
    tabs: [{id: '1', title: 'One'}],
  });
  assert.equal(root.querySelector('.rows').dataset.dropzone, undefined);
  assert.equal(root.querySelector('.rows > .tabrow').draggable, undefined);

  const ordinary = render({tabs: [{id: '1', title: 'One'}]});
  assert.equal(ordinary.querySelector('.rows').dataset.dropzone, 'tabs');
  assert.equal(ordinary.querySelector('.rows > .tabrow').draggable, true);
});

test('an incognito snapshot still draws its own tabs', () => {
  const root = render({
    incognito: true,
    spaces: [],
    tabs: [{id: '1', title: 'One'}, {id: '2', title: 'Two'}],
  });
  assert.deepEqual(rowTitles(root), ['One', 'Two']);
  assert.deepEqual(rowIds(root), ['1', '2']);
});

test('a snapshot without the flag keeps its space name and grid', () => {
  const root = render({spaceName: 'Work', tabs: [{id: '1', title: 'One'}]});
  assert.equal(root.querySelector('.wsname').dataset.dropzone, 'pinned');
  assert.equal(root.querySelector('.favgrid').dataset.dropzone, 'favorites');
});

test('neither the tab list nor the pinned section shows placeholder text, ' +
         'empty or full',
     () => {
  const empty = render({});
  assert.equal(empty.querySelector('.foldhint'), null);
  assert.equal(empty.textContent.includes('Pin a tab'), false);

  const full = render({
    tabs: [{id: '1', title: 'One'}],
    pinned: [{id: 'p', title: 'P'}],
  });
  assert.equal(full.querySelector('.foldhint'), null);
  assert.equal(full.textContent.includes('Pin a tab'), false);
});

// a UA "normal" line-height can outgrow the 9px dot by a font-
// dependent amount align-items: center cannot see coming.
test('the space name row pins its icon and label to one centred line', () => {
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
  const rule = css.match(/\.wsname\s*{[^}]*}/)[0];
  assert.match(rule, /align-items:\s*center/);
  assert.match(rule, /line-height:\s*1\b/);
});

test('a pinned list renders as the pinned pseudo-folder', () => {
  const root = render({pinned: [{id: 'p1', title: 'Pinned One'}]});
  assert.equal(root.querySelector('.frow').dataset.folderId, PINNED_FOLD_ID);
  assert.equal(root.querySelector('.fold').dataset.folderBody, PINNED_FOLD_ID);
  assert.deepEqual(
      root.querySelectorAll('.fold .tabrow .tl').map((n) => n.textContent),
      ['Pinned One']);
});

test('the pinned section drops as pinned, not as a tab group', () => {
  // It folds like a folder and has no group id. A dropzone of
  // folder:__pinned__ would resolve to a sidebarTabGrouped for a group that
  // does not exist.
  const inner = render({pinned: [{id: 'p1', title: 'One'}]}).querySelector('.foldin');
  assert.equal(inner.dataset.dropzone, 'pinned');
});

test('the pinned section does not draw itself as a folder', () => {
  // It folds through the folder machinery but is not a group. A folder glyph
  // would read as one, and there is no group behind it to open.
  const pinned = render({pinned: [{id: 'p', title: 'P'}]}).querySelector('.frow');
  assert.equal(pinned.querySelectorAll('.fic-g').length, 0);
  const folder = render({folders: [{id: 'g', name: 'G', tabs: []}]}).querySelector('.frow');
  assert.equal(folder.querySelectorAll('.fic-g').length, 1,
      'a real folder draws one, which is what makes the absence above the tell');
});

test('pinnedCollapsed folds the pinned section', () => {
  const open = render({pinned: [{id: 'p', title: 'P'}]});
  assert.equal(open.querySelector('.fold').classList.contains('closed'), false);
  assert.equal(open.querySelector('.frow').classList.contains('closedH'), false);

  const shut = render({pinned: [{id: 'p', title: 'P'}], pinnedCollapsed: true});
  assert.equal(shut.querySelector('.fold').classList.contains('closed'), true);
  assert.equal(shut.querySelector('.frow').classList.contains('closedH'), true);
});

// The workspace name and the pinned section's header are one control now: the
// name row IS the fold header, present whether or not there is anything under
// it to fold.
test('the workspace row renders with no pinned tabs and carries no fold ' +
         'affordance',
     () => {
  const head = render({}).querySelector('.wsname');
  assert.ok(head, 'the row itself is always drawn');
  assert.equal(head.classList.contains('frow'), false);
  assert.equal(head.dataset.folderId, undefined);
  assert.equal(head.querySelector('.chev'), null);
});

test('the workspace row carries the fold affordance when the space has ' +
         'pinned tabs',
     () => {
  const head = render({pinned: [{id: 'p', title: 'P'}]}).querySelector('.wsname');
  assert.equal(head.classList.contains('frow'), true);
  assert.equal(head.dataset.folderId, PINNED_FOLD_ID);
  assert.ok(head.querySelector('.chev'), 'the disclosure chevron is drawn');
});

test('the icon and the chevron share one slot, not a column of their own', () => {
  const head = render({pinned: [{id: 'p', title: 'P'}]}).querySelector('.wsname');
  const slot = head.querySelector('.wsic');
  assert.ok(slot, 'the icon slot exists');
  assert.deepEqual([...slot.children].map((n) => n.className), ['wsic-i', 'chev']);
});

test('the chevron is hidden at rest, and crossfades with the icon on hover', () => {
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
  const ruleFor = (selector) => {
    const at = css.indexOf(selector + ' {');
    assert.notEqual(at, -1, `no rule for ${selector}`);
    return css.slice(at, css.indexOf('}', at));
  };
  assert.match(ruleFor('.wsic .chev'), /opacity:\s*0\b/);
  // .hoverhold stands in for :hover on a head rebuilt under a still pointer,
  // so every swap rule must fire on either.
  const hovered = ':is(:hover, .hoverhold)';
  assert.match(ruleFor(`.wsname.frow${hovered} .wsic-i`), /opacity:\s*0\b/);
  assert.match(ruleFor(`.wsname.frow${hovered} .wsic .chev`), /opacity:\s*1\b/);
  assert.match(ruleFor(`.frow${hovered} .fic-g`), /opacity:\s*0\b/);
  assert.match(ruleFor(`.frow${hovered} .fic .chev`), /opacity:\s*1\b/);
  assert.equal(css.includes('frow:hover'), false,
               'a bare :hover swap rule would flash on a rebuild');
  // the same for the tab rows, whose fill and close button flickered
  // every time an arriving favicon rebuilt the list. The absence guard needs
  // the presence one beside it, or deleting the rule outright would pass.
  assert.match(ruleFor(`.row.tabrow${hovered} .x`), /opacity:\s*\.6\b/);
  assert.equal(css.includes('tabrow:hover'), false,
               'a bare :hover tab row rule would flash on a rebuild');
});

test('the space dots scroll sideways without ever drawing a scrollbar', () => {
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
  const ruleFor = (selector) => {
    const at = css.indexOf(selector + ' {');
    assert.notEqual(at, -1, `no rule for ${selector}`);
    return css.slice(at, css.indexOf('}', at));
  };
  const dots = ruleFor('.dotscroll');
  assert.match(dots, /overflow-x:\s*auto\b/, 'a long dot list must still scroll');
  assert.match(dots, /scrollbar-width:\s*none\b/);
  assert.match(ruleFor('.dotscroll::-webkit-scrollbar'), /display:\s*none\b/);
});

test('a carried hover snaps - .hoverhold suppresses the swap transitions', () => {
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
  // apply() forces a layout before carryHover's class lands, so without this
  // rule the class change replays the swap as a visible fade.
  const at = css.indexOf('.frow.hoverhold,');
  assert.notEqual(at, -1, 'no snap rule for the held head');
  const rule = css.slice(at, css.indexOf('}', at));
  for (const sel of ['.frow.hoverhold .fic-g', '.frow.hoverhold .fic .chev',
                     '.frow.hoverhold .wsic-i', '.frow.hoverhold .wsic .chev',
                     '.row.tabrow.hoverhold::before',
                     '.row.tabrow.hoverhold .x']) {
    assert.ok(rule.includes(sel), `${sel} missing from the snap rule`);
  }
  assert.match(rule, /transition:\s*none\b/);
});

test('the workspace row keeps the arrow cursor', () => {
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
  const at = css.indexOf('.wsname.frow {');
  assert.notEqual(at, -1);
  assert.doesNotMatch(css.slice(at, css.indexOf('}', at)), /cursor:\s*pointer/);
});

test('the workspace row draws the space icon as a glyph, not a tinted ' +
         'plate',
     () => {
  const tint = '#4488cc';
  const root = render({spaceIcon: '★', spaceTint: tint});
  const glyph = root.querySelector('.wsic-i');
  assert.equal(glyph.textContent, '★');
  assert.ok(!glyph.style.background, 'the plate must be gone');
  const ground = rowBackdrop(parseRgb(tint));
  const ratio = contrastRatio(parseRgb(glyph.style.color), ground);
  assert.ok(ratio >= RING_MIN_CONTRAST,
      `${glyph.style.color} on the row reads ${ratio.toFixed(2)}:1`);
});

test('sidebar.css draws no plate behind the workspace glyph', () => {
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
  const rule = css.match(/\.wsic-i\s*{[^}]*}/)[0];
  assert.equal(/background/.test(rule), false, 'a plate rule is still there');
});

test('the workspace row falls back to the first letter of the space name ' +
         'when the space has no icon',
     () => {
  const glyph = render({spaceName: 'Reading'}).querySelector('.wsic-i');
  assert.equal(glyph.textContent, 'R');
});

test('the workspace row draws a ph: icon as a Phosphor svg, not the literal ' +
         'string',
     () => {
  const glyph = render({spaceIcon: 'ph:star', spaceName: 'Reading'})
      .querySelector('.wsic-i');
  assert.equal(glyph.firstChild.tagName.toLowerCase(), 'svg');
  assert.equal(glyph.firstChild.dataset.icon, 'star');
});

test('the separate Pinned header row is gone', () => {
  const root = render({pinned: [{id: 'p', title: 'P'}]});
  assert.equal(root.querySelectorAll('.sechdrow').length, 0);
  assert.equal(root.querySelectorAll('.frow').length, 1,
      'the space name is the only header the pinned section draws');
});

test('a click on the workspace row folds the pinned section and reports it',
     () => {
  const root = document.createElement('div');
  installSidebar(root);
  renderSidebar(root, {iconScope: SCOPE, pinned: [{id: 'p', title: 'P'}]});
  dom.sent.length = 0;
  root.querySelector('.wsname').dispatchEvent(new Event('click', {bubbles: true}));
  assert.deepEqual(dom.sent, [['sidebarPinnedToggled', [true]]]);
});

test('a click on the workspace row with no pinned tabs does nothing', () => {
  const root = document.createElement('div');
  installSidebar(root);
  renderSidebar(root, {iconScope: SCOPE});
  dom.sent.length = 0;
  root.querySelector('.wsname').dispatchEvent(new Event('click', {bubbles: true}));
  assert.deepEqual(dom.sent, []);
});

// dotAt is the fallback for a click that misses a dot's own box
// mid bloom-shift. This drives it through the real handler, not the helper
// directly, so a break in the sidebar.js wiring still fails a test.
test('a click near a dot switches to it; a click on the bar padding does not',
     () => {
  const root = document.createElement('div');
  installSidebar(root);
  // renderSidebar alone never calls swipe.attach, so spaceCount stays at its
  // construction default and a real jumpTo has nothing to move to.
  dom.window.atriumSetSidebar(
      {iconScope: SCOPE, spaces: [{name: 'A'}, {name: 'B'}], activeSpace: 0});

  const bar = root.querySelector('.spacedots');
  const scroller = bar.querySelector('.dotscroll');
  bar.setRect({left: 0, top: 0, width: 200, height: 26});
  scroller.setRect({left: 0, top: 0, width: 200, height: 26});
  scroller.scrollLeft = 0;
  // Overview, A, B, + at 9, 29, 49, 69: same pitch as buildSpaceDots draws.
  [...bar.querySelectorAll('.sdot')].forEach(
      (dot, i) => dot.setLayoutBox({left: i * 20, top: 0, width: 18, height: 26}));

  const idle = dom.window.__swipeDebug().state;

  // 31px from every centre - the bar's own padding, not a dot.
  bar.dispatchEvent(new Event('click', {bubbles: true, clientX: 100}));
  assert.equal(dom.window.__swipeDebug().state, idle,
      'a press on the padding must not start a switch');

  // Space B's centre (49) plus the bloom's 9px shift - still its dot.
  bar.dispatchEvent(new Event('click', {bubbles: true, clientX: 58}));
  assert.notEqual(dom.window.__swipeDebug().state, idle,
      'a gap press 9px from a dot must still resolve to it');
});

test('a folder body is the drop target that puts a tab in the group', () => {
  // Membership was always persistable, but nothing offered a way to set it, so
  // every folder a user made stayed empty.
  const root = render({folders: [{id: 'ABC-123', name: 'Reading', tabs: []}]});
  assert.equal(root.querySelector('.foldin').dataset.dropzone, 'folder:ABC-123');
});

// No layout in the dom-shim, so this pins the CSS values - real pixels are the
// VM scenario's job. The workspace name, a folder and New Tab used to sit a
// 10px section gap from their neighbours while tab rows sat 4px apart.
test('every child of the scroll column sits a plain-row gap from the next', () => {
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
  const ruleOf = (selector) => css.match(new RegExp(
      selector.replace(/\./g, '\\.') + '\\s*{[^}]*}'))[0];
  const gapOf = (selector) => {
    const match = ruleOf(selector).match(/gap:\s*(-?\d+)px/);
    assert.ok(match, `${selector} declares no gap`);
    return Number(match[1]);
  };
  assert.equal(gapOf('.scrollinner'), gapOf('.rows'));
  for (const selector of ['.fold', '.sechdr', '.newtab']) {
    assert.doesNotMatch(ruleOf(selector), /margin-(top|bottom)/,
        `${selector} must not widen the column gap on its own`);
  }
});

test('the space dots appear only when the snapshot carries spaces, ' +
         'led by the synthetic Overview dot',
     () => {
       assert.equal(
           render({tabs: []}).querySelectorAll('.spacedots').length, 0);
       const dots = render({spaces: [{name: 'A'}, {name: 'B'}], activeSpace: 1})
                        .querySelectorAll('.sdot');
       assert.equal(dots[0].classList.contains('ovdot'), true,
           'Overview is always leftmost');
       // By data-space-index rather than position: the bar is led by the
       // Overview dot and trailed by the "+", and both are
       // .sdot in the lab's markup too - only the rooms carry an index.
       const spaceDots = dots.filter((d) => d.dataset.spaceIndex !== undefined);
       assert.deepEqual(spaceDots.map((d) => d.dataset.spaceIndex),
           ['0', '1']);
       assert.deepEqual(
           spaceDots.map((d) => d.classList.contains('active')),
           [false, true]);
     });

// --- the split row --------------------------------------------

// The membership arrives on its own push, so every case here sets it and puts
// it back - a leaked split would collapse the rows of the tests below.
const withSplit = (ids, body) => {
  setSplitMembers(ids);
  try {
    return body();
  } finally {
    setSplitMembers([]);
  }
};

const SPLIT_TABS = [
  {id: '1', url: 'https://a.test/', title: 'One'},
  {id: '2', url: 'https://b.test/', title: 'Two'},
  {id: '3', url: 'https://c.test/', title: 'Three'},
];

test('the members of a split leave the list and one row takes their place',
     () => {
       const root = withSplit(['2', '3'], () => render({tabs: SPLIT_TABS}));
       assert.deepEqual(
           root.querySelectorAll('.rows > .tabrow')
               .filter((r) => !r.classList.contains('splitrow'))
               .map((r) => r.querySelector('.tl').textContent),
           ['One'], 'only the tab outside the split keeps a row of its own');
       const split = root.querySelectorAll('.rows > .splitrow');
       assert.equal(split.length, 1);
       assert.equal(split[0].dataset.panes, '2');
     });

test('the split row sits at the slot of its first member', () => {
  const root = withSplit(['1', '3'], () => render({tabs: SPLIT_TABS}));
  const rows = root.querySelectorAll('.rows > .tabrow');
  assert.equal(rows.length, 2);
  assert.equal(rows[0].classList.contains('splitrow'), true,
      'the split replaced tab One, which was first in the list');
  assert.equal(rows[1].querySelector('.tl').textContent, 'Two');
});

// The segments are what the two click handlers read, so an empty or missing id
// leaves the pane inert - the cold-row hole, one level down.
test('each pane is a segment carrying its own id, title and close button',
     () => {
       const root = withSplit(['2', '3'], () => render({tabs: SPLIT_TABS}));
       const segs = root.querySelectorAll('.splitrow .sseg');
       assert.deepEqual(segs.map((s) => s.dataset.tabId), ['2', '3']);
       assert.deepEqual(segs.map((s) => s.dataset.url),
           ['https://b.test/', 'https://c.test/']);
       assert.deepEqual(segs.map((s) => s.querySelector('.tl').textContent),
           ['Two', 'Three']);
       for (const seg of segs) {
         assert.ok(seg.querySelector('.fv'), 'a segment must keep its favicon');
         assert.ok(seg.querySelector('button.x'),
             'a segment closes its own pane');
       }
     });

// The pane order is the browser's, not the list's. Reading it off the list
// would name the wrong pane whenever a split was built back to front.
test('the segments follow the pushed pane order, not the list order', () => {
  const root = withSplit(['3', '2'], () => render({tabs: SPLIT_TABS}));
  assert.deepEqual(
      root.querySelectorAll('.splitrow .sseg').map((s) => s.dataset.tabId),
      ['3', '2']);
});

test('a split whose panes have closed down to one renders ordinary rows',
     () => {
       for (const ids of [[], ['2'], ['2', '9']]) {
         const root = withSplit(ids, () => render({tabs: SPLIT_TABS}));
         assert.equal(root.querySelectorAll('.splitrow').length, 0,
             `${JSON.stringify(ids)} is not a split`);
         assert.deepEqual(rowIds(root), ['1', '2', '3']);
       }
     });

test('the split row drags like any row, but not in a private window', () => {
  const root = withSplit(['1', '3'], () => render({tabs: SPLIT_TABS}));
  assert.equal(root.querySelector('.splitrow').draggable, true);
  const hidden = withSplit(['1', '3'], () => render({
    incognito: true, spaces: [], tabs: SPLIT_TABS,
  }));
  assert.equal(hidden.querySelector('.splitrow').draggable, undefined);
});

test('the split row carries the ring element every row needs', () => {
  const root = withSplit(['1', '2'], () => render({tabs: SPLIT_TABS}));
  assert.equal(root.querySelectorAll('.splitrow > .brdr').length, 1);
});

// A click on the row's own padding lands on no segment, and the handlers fall
// back to the row's id - which must name a pane rather than nothing.
test('the split row answers for its first pane', () => {
  const root = withSplit(['3', '1'], () => render({tabs: SPLIT_TABS}));
  const split = root.querySelector('.splitrow');
  assert.equal(split.dataset.tabId, '3');
  assert.equal(split.dataset.url, 'https://c.test/');
});

test('a selected pane marks its own segment and the row', () => {
  const root = withSplit(['1', '2'], () => render({
    tabs: [SPLIT_TABS[0], {...SPLIT_TABS[1], selected: true}, SPLIT_TABS[2]],
  }));
  const split = root.querySelector('.splitrow');
  assert.equal(split.classList.contains('sel'), true);
  assert.deepEqual(
      split.querySelectorAll('.sseg').map((s) => s.classList.contains('segsel')),
      [false, true]);
});

// atriumSplitChanged used to read only primaryTabID/
// secondaryTabID, so a third or fourth pane never reached splitMemberIds at
// all - the push handler itself is the gap, not buildSplitRow.
test('atriumSplitChanged forwards every id in paneTabIDs, not only the ' +
         'first two',
     () => {
       const root = document.createElement('div');
       installSidebar(root);
       window.atriumSplitChanged({
         active: true,
         primaryTabID: 1,
         secondaryTabID: 2,
         paneTabIDs: [1, 2, 3],
       });
       renderSidebar(root, {
         iconScope: SCOPE,
         tabs: [SPLIT_TABS[0], SPLIT_TABS[1], {...SPLIT_TABS[2], selected: true}],
       });
       const split = root.querySelector('.splitrow');
       assert.ok(split, 'three panes must still collapse into one split row');
       assert.deepEqual(
           split.querySelectorAll('.sseg').map((s) => s.dataset.tabId),
           ['1', '2', '3']);
       assert.deepEqual(
           split.querySelectorAll('.sseg').map(
               (s) => s.classList.contains('segsel')),
           [false, false, true], 'the third pane must be markable as selected');
       window.atriumSplitChanged(null);
     });

// The drop is a container query on the ROW, not a measurement in JavaScript:
// the sidebar resizes and each rail pane has its own width, so a window-width
// media query would hide the titles on the wrong panes.
test('the titles drop by container query and the favicons never do', () => {
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
  assert.match(css, /\.row\.tabrow\.splitrow\s*{[^}]*container:\s*splitrow\s*\/\s*inline-size/,
      'the split row must be its own inline-size container');
  // The query reads the content box, which excludes the row's 12px padding.
  for (const [panes, width] of [['2', '132'], ['3', '200'], ['4', '268']]) {
    const at = css.indexOf(`@container splitrow (max-width: ${width}px)`);
    assert.notEqual(at, -1, `no rule for ${panes} panes`);
    const rule = css.slice(at, css.indexOf('\n}', at));
    assert.match(rule, new RegExp(`\\.splitrow\\[data-panes="${panes}"\\] \\.sseg \\.tl`),
        `the ${panes}-pane rule must hide the titles`);
    assert.match(rule, /display:\s*none/);
    assert.doesNotMatch(rule, /\.fv/, 'the favicons must survive every width');
  }
  // In the flow the button held 17px plus a gap on every pane at rest, which
  // is most of a sidebar-wide pane's title room. It stays out of the flow, and
  // the segment is the containing block so it covers its own pane only.
  assert.match(css, /\.splitrow \.sseg\s*{[^}]*position:\s*relative/,
      'the segment must be the close button\'s containing block');
  const seg = css.indexOf('.splitrow .sseg .x');
  assert.notEqual(seg, -1, 'no rule for the segment close button');
  assert.doesNotMatch(css.slice(seg, css.indexOf('\n}', seg)), /position:\s*static/,
      'the segment close button must not take room in the flow');
});

// --- a themed space's own dot ------------------------------------------------

const themeOf = (...colors) => ({
  stops: colors.map((color, i) => ({x: i / 4, y: 0.5, color, alpha: 1})),
});

const dotsOf = (spaces, activeIndex = 0) => {
  const root = document.createElement('div');
  root.appendChild(buildSpaceDots(spaces, activeIndex));
  return root.querySelectorAll('.sdot[data-space-index]');
};

test('a themed space dot draws a gradient of its stops', () => {
  const dots = dotsOf([{name: 'Home', color: '#0f0',
                        theme: themeOf('#FF8800', '#0044FF')},
                       {name: 'Work', color: '#111'}]);
  assert.equal(dots[0].dataset.tint,
               'linear-gradient(135deg, #FF8800, #0044FF)');
  assert.equal(dots[1].dataset.tint, '#111', 'an unthemed dot keeps its tint');
  // The tint now belongs to the hover chip alone.
  assert.ok(!dots[0].querySelector('.d').style.background);
});

test('a one-stop theme keeps the flat tint', () => {
  // A solid colour IS a one-stop theme (the studio opens every unthemed space
  // on one), so a gradient there would repaint every space in the rail.
  const dots = dotsOf([{name: 'Home', color: '#0f0', theme: themeOf('#FF8800')}]);
  assert.equal(dots[0].dataset.tint, '#0f0');
});

test('a stop colour that is not a plain hex falls back to the flat tint', () => {
  // The fill lands in style.background, so an unvalidated stop would be a
  // renderer-side CSS injection from the themes file.
  const dots = dotsOf([
    {name: 'Home', color: '#0f0', theme: themeOf('#FF8800', 'url(evil)')},
  ]);
  assert.equal(dots[0].dataset.tint, '#0f0');
});

test('the active dot draws a ph: icon as a Phosphor svg, not the literal ' +
         'string',
     () => {
  const dots = dotsOf([{name: 'Home', icon: 'ph:star', color: '#0f0'}]);
  const svg = dots[0].querySelector('.d').firstChild;
  assert.equal(svg.tagName.toLowerCase(), 'svg');
  assert.equal(svg.dataset.icon, 'star');
});

test('markActiveSpaceDot draws the newly active dot\'s ph: icon as an svg', () => {
  const root = document.createElement('div');
  root.appendChild(buildSpaceDots(
      [{name: 'Home', color: '#0f0'},
       {name: 'Work', color: '#111', icon: 'ph:star'}],
      0));
  markActiveSpaceDot(root, 1);
  const svg = root.querySelectorAll('.sdot[data-space-index]')[1]
      .querySelector('.d').firstChild;
  assert.equal(svg.tagName.toLowerCase(), 'svg');
  assert.equal(svg.dataset.icon, 'star');
});

test('markActiveSpaceDot moves the ink to the newly active glyph', () => {
  const root = document.createElement('div');
  root.appendChild(buildSpaceDots(
      [{name: 'Home', color: '#0f0'},
       {name: 'Work', color: '#111', theme: themeOf('#FF8800', '#0044FF')}],
      0));
  markActiveSpaceDot(root, 1);
  const glyphs = root.querySelectorAll('.sdot[data-space-index]')
      .map((dot) => dot.querySelector('.d'));
  assert.equal(glyphs[0].style.color, '', 'the old glyph goes back to a grey dot');
  assert.ok(glyphs[1].style.color, 'the new glyph is inked');
  assert.equal(glyphs[1].style.background, '', 'with no chip behind it');
  assert.equal(glyphs[1].textContent, 'W');
});

test('a dot inks against its own space, not the last pane drawn', () => {
  // buildRail draws every pane before the bar, and each pane sets the shared
  // ring ground, so the bar used to take whichever space came last.
  const inkAfter = (lastTint) => {
    pinnedFavGrid({spaceTint: lastTint, favorites: [], spaces: []});
    return buildSpaceDots([{name: 'Home', color: '#c05070'}], 0)
        .querySelector('.sdot[data-space-index]').dataset.ink;
  };
  assert.equal(inkAfter('#ffffff'), inkAfter('#101010'));
});

test('a switch under the cursor leaves the hover chip painted', () => {
  // The magnifier only repaints when the cursor moves to another dot, so a
  // hotkey switch while a dot is hovered must not strip its chip.
  const root = document.createElement('div');
  root.appendChild(buildSpaceDots(
      [{name: 'Home', color: '#0f0'}, {name: 'Work', color: '#111'},
       {name: 'Play', color: '#222'}], 0));
  const dots = root.querySelectorAll('.sdot[data-space-index]');
  dots[2].classList.add('mag');
  dots[1].classList.add('mag');
  markActiveSpaceDot(root, 1);
  const hovered = dots[2].querySelector('.d');
  assert.equal(hovered.textContent, 'P');
  assert.equal(hovered.style.background, '#222');
  const activeAndHovered = dots[1].querySelector('.d');
  assert.equal(activeAndHovered.style.background, '#111');
  assert.ok(!activeAndHovered.style.color, 'the chip draws its own white initial');
});

// placeDotBloom ran once, at build time, off a bar that was not in
// the document yet - every offsetLeft it read came back 0, so the bloom sat
// at the left end no matter which space was active.
test('the bloom measures the active dot after attach, not before', () => {
  const bloomSpaces = [{name: 'Home', color: '#0f0'}, {name: 'Work', color: '#111'}];
  const root = document.createElement('div');
  dom.body.appendChild(root);
  const bar = buildSpaceDots(bloomSpaces, 1);
  root.appendChild(bar);
  const dots = bar.querySelectorAll('.sdot[data-space-index]');
  dots[0].setRect({left: 0, top: 0, width: 24, height: 24});
  dots[1].setRect({left: 40, top: 0, width: 24, height: 24});
  dom.tick();

  const bloom = bar.querySelector('.dotbloom');
  assert.equal(bloom.style.getPropertyValue('--dbx'), '52px',
      'the bloom centres under a non-first active dot, not at the left end');
  assert.equal(bloom.style.transition, 'none',
      'the correction must not glide in from the 0px guess');
  dom.tick();
  assert.equal(bloom.style.transition, '',
      'the glide is restored once the correction has landed');

  markActiveSpaceDot(root, 0, bloomSpaces);
  assert.equal(bloom.style.getPropertyValue('--dbx'), '12px',
      'switching spaces should move the bloom, not leave it behind');
  assert.equal(bloom.style.transition, '',
      'a real space switch keeps its glide');
});

test('a correction never fires for a bar nothing ever attached', () => {
  const spaces = [{name: 'Home', color: '#0f0'}, {name: 'Work', color: '#111'}];
  const bar = buildSpaceDots(spaces, 1);
  bar.querySelectorAll('.sdot[data-space-index]')[1]
      .setRect({left: 40, top: 0, width: 24, height: 24});
  dom.tick();
  assert.equal(bar.querySelector('.dotbloom').style.getPropertyValue('--dbx'), '0px',
      'a bar nothing attached must not be measured');
});

test('a rebuild cancels an earlier bloom correction pending on a bar it replaces', () => {
  const spaces = [{name: 'Home', color: '#0f0'}, {name: 'Work', color: '#111'}];

  const rootA = document.createElement('div');
  dom.body.appendChild(rootA);
  const barA = buildSpaceDots(spaces, 1);
  rootA.appendChild(barA);
  barA.querySelectorAll('.sdot[data-space-index]')[1]
      .setRect({left: 40, top: 0, width: 24, height: 24});

  // barA stays attached, so only the cancel (not the isConnected guard) can
  // be what stops its correction from firing below.
  const rootB = document.createElement('div');
  dom.body.appendChild(rootB);
  const barB = buildSpaceDots(spaces, 1);
  rootB.appendChild(barB);
  barB.querySelectorAll('.sdot[data-space-index]')[1]
      .setRect({left: 60, top: 0, width: 24, height: 24});

  dom.tick();

  assert.equal(barA.querySelector('.dotbloom').style.getPropertyValue('--dbx'), '0px',
      'the earlier bar\'s pending correction must be cancelled, not fired');
  assert.equal(barB.querySelector('.dotbloom').style.getPropertyValue('--dbx'), '72px',
      'the current bar still gets corrected');
});

// a switch through several spaces carried the active dot past the
// bar's right edge, and the bar never followed it.
const overflowSpaces = Array.from({length: 8}, (_, i) => ({name: `S${i}`, color: '#0f0'}));

// A 100px bar over 8 dots 20px apart: dots 5 to 7 sit past the right edge.
const stageOverflowBar = (activeIndex, {withLeadingOverview = false} = {}) => {
  const root = document.createElement('div');
  dom.body.appendChild(root);
  const bar = buildSpaceDots(overflowSpaces, activeIndex, withLeadingOverview);
  root.appendChild(bar);
  const scroller = bar.querySelector('.dotscroll');
  scroller.clientWidth = 100;
  scroller.scrollLeft = 0;
  scroller.querySelectorAll('.sdot[data-space-index]').forEach((dot, i) =>
      dot.setLayoutBox({left: i * 20, top: 0, width: 18, height: 26}));
  return {root, bar, scroller};
};

test('a switch to a dot past the right edge scrolls the bar to show it', () => {
  const {root, scroller} = stageOverflowBar(0);
  dom.tick();
  markActiveSpaceDot(root, 6, overflowSpaces);
  // Dot 6 spans 120 to 138, so the bar must scroll at least 38px.
  assert.ok(scroller.scrollLeft >= 38, `scrollLeft ${scroller.scrollLeft}`);
  assert.ok(scroller.scrollLeft <= 120, 'the dot must not scroll off the left edge');
  assert.equal(scroller.lastScrollBehavior, 'smooth', 'a real switch glides');
});

test('a switch back to a dot past the left edge scrolls the bar back', () => {
  const {root, scroller} = stageOverflowBar(0);
  dom.tick();
  scroller.scrollLeft = 80;
  markActiveSpaceDot(root, 1, overflowSpaces);
  assert.ok(scroller.scrollLeft <= 20, `scrollLeft ${scroller.scrollLeft}`);
});

test('a switch to a dot already in view leaves the scroll alone', () => {
  const {root, scroller} = stageOverflowBar(0);
  dom.tick();
  scroller.scrollLeft = 10;
  markActiveSpaceDot(root, 2, overflowSpaces);
  assert.equal(scroller.scrollLeft, 10);
});

test('a rebuilt bar lands on its active dot at once', () => {
  // A rebuild is a new element at scrollLeft 0. A glide from there would
  // replay on every snapshot push.
  const {scroller} = stageOverflowBar(7);
  dom.tick();
  assert.ok(scroller.scrollLeft >= 58, `scrollLeft ${scroller.scrollLeft}`);
  assert.equal(scroller.lastScrollBehavior, 'instant');
});

test('the Overview and + controls sit outside the scroller', () => {
  const {bar} = stageOverflowBar(0, {withLeadingOverview: true});
  const scroller = bar.querySelector('.dotscroll');
  assert.equal(bar.querySelector('.ovdot').parentNode, bar,
      'Overview must not be inside the scroller, or a swipe could carry it off');
  assert.equal(bar.querySelector('.addws').parentNode, bar,
      '+ must not be inside the scroller, or a swipe could carry it off');
  assert.ok(scroller.contains(bar.querySelector('.sdot[data-space-index]')),
      'the per-space dots are what actually scrolls');

  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
  assert.doesNotMatch(css, /\.sdot\.ovdot\s*{[^}]*position:\s*sticky/);
  assert.doesNotMatch(css, /\.sdot\.addws\s*{[^}]*position:\s*sticky/);
  const at = css.indexOf('.spacedots {');
  assert.doesNotMatch(css.slice(at, css.indexOf('}', at)), /mask-image/);
});

// the rail's own bar sits beside .rail, not inside .sidebar
//, so .sidebar button's cursor:inherit never reaches its dots.
test('a space dot declares its own default cursor', () => {
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
  const rule = css.match(/\.sdot\s*{[^}]*}/)[0];
  assert.match(rule, /cursor:\s*default/);
  assert.equal(/cursor:\s*pointer/.test(rule), false);
});

const FULL = {
  favorites: [{id: 'f'}],
  pinned: [{id: 'p', title: 'P'}],
  folders: [{id: 'g', name: 'G', tabs: []}],
  tabs: [{id: 't', title: 'T'}],
  spaces: [{name: 'A'}],
};

test('the sidebar stacks its sections in one fixed order', () => {
  // The order IS the layout - there is no CSS ordering behind it - and nothing
  // else here would notice two of these swapping, because every other test
  // reaches its subject by selector.
  // The workspace name is the pinned section's own fold header now, so it
  // draws inside the scroll zone rather than as a sibling of it - see 'only
  // the tab sections live in the scroll zone' below.
  const sidebar = render(FULL).querySelector('.sidebar');
  assert.deepEqual(sidebar.children.map((n) => n.className), [
    // the bar sits under the traffic lights, which is where the
    // lab has it - .sbchrome, then .urlbar, then the panes.
    // The child here is the row that wraps the bar. The RAIL hoists that row
    // out of its panes - this render owns no rail, so it keeps its own.
    'urlrow', 'favgrid',
    'scrollzone',
    'spacedots',
    // no width handle here any more. The native grip owns the seam
    // alone - two handles left a band between them that resized from neither.
  ]);
});

// What scrolls is the tab sections and nothing else: a scrollbar
// spanning the whole pane read as the entire sidebar scrolling, and the
// favourites grid and space dots scrolling away with it was never wanted.
// The sections sit inside .scrollinner rather than directly in the zone: the
// rubber band needs ONE element to translate, and it has to be the content
// rather than the viewport. The contract this pins - what scrolls
// with the tabs, and in what order - is unchanged.
test('only the tab sections live in the scroll zone', () => {
  const inner = render(FULL).querySelector('.scrollzone .scrollinner');
  assert.deepEqual(inner.children.map((n) => n.className), [
    'wsname frow', 'fold',  // pinned: the workspace name as header, then body
    // every folder's head+body pair sits inside one shared zone, so
    // a head can reorder among its siblings.
    'folderlist',
    'sechdr',
    'newtab',
    // no 'newfolder' - the button is gone, and a folder is made
    // from the context menu, which sends the same message.
    'rows',
  ]);
});

// The band has nothing to move without this wrapper, and its absence would
// show up as a rubber band that silently does nothing rather than as an error.
test('the scroll zone holds exactly one content wrapper', () => {
  const zone = render(FULL).querySelector('.scrollzone');
  assert.equal(zone.children.length, 1, 'the zone must have a single child');
  assert.equal(zone.children[0].className, 'scrollinner');
});

test('the favourites grid and the space dots stay outside the scroll zone', () => {
  const sidebar = render(FULL).querySelector('.sidebar');
  const zone = sidebar.querySelector('.scrollzone');
  for (const cls of ['favgrid', 'spacedots']) {
    const el = sidebar.querySelector('.' + cls);
    assert.ok(el, `${cls} missing`);
    assert.equal(zone.contains(el), false, `${cls} must not scroll with the tabs`);
  }
});

test('the workspace row scrolls with the tabs, as the pinned section\'s own ' +
         'header',
     () => {
  const sidebar = render(FULL).querySelector('.sidebar');
  const zone = sidebar.querySelector('.scrollzone');
  assert.equal(zone.contains(sidebar.querySelector('.wsname')), true);
});

// The lab puts New Tab above the regular rows rather than under them, so it
// sits with the list it opens into.
test('New Tab sits above the regular rows', () => {
  const inner = render(FULL).querySelector('.scrollzone .scrollinner');
  const kids = inner.children.map((n) => n.className);
  assert.ok(kids.indexOf('newtab') < kids.indexOf('rows'),
            'New Tab must come before the row list');
});

test('New Tab is still rendered when the space has no regular tabs', () => {
  const zone = render({...FULL, tabs: []}).querySelector('.scrollzone');
  assert.ok(zone.querySelector('.newtab'), 'an empty space still needs New Tab');
  assert.equal(zone.querySelector('.sechdr'), null,
               'and no Tabs header with nothing under it');
});

// The plus used to live inside the label string, which put it in a screen
// reader's announcement ("plus New Tab") and made the button unalignable
// with a row's icon slot.
test('New Tab carries the plus as a decorative icon, not text in its label',
     () => {
  const button = render(FULL).querySelector('.newtab');
  const icon = button.querySelector('.nti');
  assert.equal(icon.textContent, '+');
  assert.equal(icon.getAttribute('aria-hidden'), 'true');
  const label = button.querySelector('.ntl');
  assert.equal(label.textContent, 'New Tab');
});

// --- buildRail --------------------------------------------------------------

// Two spaces with entirely different contents, the fixture the whole per-space
// rule needs. Space 1 is the active one, so its rows are ALSO at the top level,
// with live ids the per-space copy does not have.
const railSnapshot = () => ({
  activeSpace: 1,
  spaceName: 'Work', spaceTint: '#111',
  tabs: [{id: '42', title: 'Live tab', url: 'https://work.test/'}],
  pinned: [{id: '77', title: 'Live pin'}],
  folders: [{id: 'g-work', name: 'Work group', tabs: []}],
  favorites: [{id: 'https://work.test/', title: 'Work fav'}],
  pinnedCollapsed: false,
  spaces: [
    {
      name: 'Home', color: '#0f0',
      // Two DIFFERENT profiles, so a swipe between these panes crosses one.
      profileId: 'home',
      tabs: [{id: '', title: 'Home tab'}],
      pinned: [{id: '', title: 'Home pin'}],
      pinnedCollapsed: true,
      folders: [{id: 'g-home', name: 'Home group', tabs: []}],
      favorites: [{id: 'https://home.test/', title: 'Home fav'}],
    },
    {
      name: 'Work', color: '#111',
      profileId: 'work',
      // Cold copies of the same rows: SidebarModelJSON gives live ids to the
      // ACTIVE space alone, so a tab in one space cannot lend its id to an
      // identical URL in another.
      tabs: [{id: '', title: 'Live tab', url: 'https://work.test/'}],
      pinned: [{id: '', title: 'Live pin'}],
      pinnedCollapsed: false,
      folders: [{id: 'g-work', name: 'Work group', tabs: []}],
      favorites: [{id: '', title: 'Work fav'}],
    },
  ],
});

test('the rail draws one pane per space, each declaring its index', () => {
  // A drag has to tell the section it started in from the one it was dropped
  // on after paging, and the index is the only thing that says which is which
  //.
  const rail = panes(buildRail(railSnapshot()));
  assert.equal(rail.length, 2);
  assert.deepEqual(rail.map((p) => p.dataset.spaceIndex), ['0', '1']);
});

test('Overview is the pane before the first space, and names no space', () => {
  // No spaceIndex on purpose: a drag released in this pane
  // would otherwise resolve to space 0 and move the row into a real space.
  const all = buildRail(railSnapshot()).querySelectorAll('.railpane');
  assert.ok(all[0].classList.contains('ovrailpane'),
            'the Overview pane must come first, or a left swipe reaches a space');
  assert.equal(all[0].dataset.spaceIndex, undefined);
  assert.equal(all.length, 3, 'the Overview pane plus one per space');
});

test('a pane renders its own space\'s rows, not the active space\'s', () => {
  const rail = panes(buildRail(railSnapshot()));
  assert.deepEqual(rowTitles(rail[0]), ['Home tab'],
      'the inactive pane must not show the active space\'s tabs');
  assert.deepEqual(rowTitles(rail[1]), ['Live tab']);
});

// Per-pane grids still hold: a swipe into another PROFILE has
// to carry the outgoing tiles off with their pane.
test('a pane renders its own space\'s favourites, not the active space\'s', () => {
  const rail = panes(buildRail(railSnapshot()));
  // activeSpace is 1, so the pinned grid holds the Work profile and pane 1 is
  // the copy of it - only the foreign pane has tiles of its own to carry.
  assert.deepEqual(favIds(rail[0]), ['https://home.test/']);
  assert.deepEqual(favIds(rail[1]), []);
});

// home, home, work is the shape a single rail-wide flag gets wrong: it calls
// the rail crossed and redraws the pinned tiles in BOTH home panes.
const twoProfileRail = () => buildRail({
  activeSpace: 0,
  favorites: [{id: 'h1'}, {id: 'h2'}, {id: 'h3'}],
  spaces: [
    {name: 'Home', profileId: 'home',
     favorites: [{id: 'h1'}, {id: 'h2'}, {id: 'h3'}]},
    {name: 'Home 2', profileId: 'home',
     favorites: [{id: 'h1'}, {id: 'h2'}, {id: 'h3'}]},
    {name: 'Work', profileId: 'work', favorites: [{id: 'w1'}]},
  ],
});

test('only the pane of a foreign profile draws tiles of its own', () => {
  const rail = panes(twoProfileRail());
  assert.deepEqual(rail.map((p) => p.querySelectorAll('.fav').length),
                   [0, 0, 1], 'both home panes stand in for the pinned grid');
  assert.deepEqual(favIds(rail[2]), ['w1']);
});

// The pinned copy is absolutely positioned, so a pane that dropped the grid
// outright let every row below it slide up underneath.
test('a same-profile pane reserves the room its tiles would take', () => {
  const rail = panes(twoProfileRail());
  assert.deepEqual(rail.map((p) => p.querySelectorAll('.favslot').length),
                   [3, 3, 0], 'one slot per tile the pinned grid shows');
});

// The pinned grid's three must appear once in the tree, not once per pane.
test('the pinned grid\'s tiles are not repeated anywhere in the rail', () => {
  const ids = [...twoProfileRail().querySelectorAll('.fav')]
                  .map((f) => f.dataset.favId);
  assert.deepEqual(ids, ['w1']);
});

test('a slot is exactly as tall as the tile it stands in for', () => {
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
  const heightOf = (selector) =>
      css.match(new RegExp(`\\${selector}\\s*{[^}]*height:\\s*([^;]+);`))?.[1];
  assert.equal(heightOf('.fav'), 'var(--fav-h)');
  assert.equal(heightOf('.favslot'), heightOf('.fav'));
});

// The swipe reads this to tell a move inside one profile from a move across
// profiles, and it is the only place a pane says which profile it is.
test('a pane names its profile', () => {
  const rail = panes(buildRail(railSnapshot()));
  assert.equal(rail[0].dataset.profileId, 'home');
  assert.equal(rail[1].dataset.profileId, 'work');
});

// The top-level scope is the active profile's, not the pane's.
test('a foreign pane asks its own profile\'s icon cache', () => {
  const rail = buildRail({
    iconScope: 'homescope', activeSpace: 0,
    spaces: [
      {name: 'Home', profileId: 'home', iconScope: 'homescope'},
      {name: 'Work', profileId: 'work', iconScope: 'workscope',
       favorites: [{id: 'https://w.test/'}]},
    ],
  });
  assert.equal(rail.querySelector('.fav .fvimg').src,
               '/urlicon/workscope/https%3A%2F%2Fw.test%2F.png');
  assert.equal(pinnedFavGrid({iconScope: 'homescope',
                              favorites: [{id: 'https://h.test/'}]})
                   .querySelector('.fvimg').src,
               '/urlicon/homescope/https%3A%2F%2Fh.test%2F.png',
               'the scope after the rail is the active profile\'s again');
});

// The rail lands before the rebuild does, so between the two the pinned copy
// still holds the profile it was drawn for. Without this stamp the swipe
// cannot tell, and shows the outgoing profile's tiles for a frame.
test('the pinned grid names the profile it was drawn for', () => {
  const snap = railSnapshot();
  assert.equal(pinnedFavGrid(snap).dataset.profileId, 'work',
               'activeSpace 1 is the Work profile');
  assert.equal(pinnedFavGrid({...snap, activeSpace: 0}).dataset.profileId,
               'home');
});

test('a pane renders its own space\'s pinned section', () => {
  const rail = panes(buildRail(railSnapshot()));
  assert.deepEqual(
      rail[0].querySelectorAll('.fold .tabrow .tl').map((n) => n.textContent),
      ['Home pin']);
});

test('a pane renders its own space\'s collapse state', () => {
  // pinnedCollapsed lives on the space, so two panes can legitimately disagree.
  const rail = panes(buildRail(railSnapshot()));
  assert.equal(rail[0].querySelector('.fold').classList.contains('closed'), true);
  assert.equal(rail[1].querySelector('.fold').classList.contains('closed'), false);
});

test('a pane renders its own space\'s folders', () => {
  const rail = panes(buildRail(railSnapshot()));
  const folderIds = (p) =>
      p.querySelectorAll('.frow').map((n) => n.dataset.folderId);
  assert.deepEqual(folderIds(rail[0]), [PINNED_FOLD_ID, 'g-home']);
  assert.deepEqual(folderIds(rail[1]), [PINNED_FOLD_ID, 'g-work']);
});

test('a pane is tinted by its own space', () => {
  const rail = panes(buildRail(railSnapshot()));
  assert.equal(rail[0].querySelector('.sidebar').style['--selbase'], '#0f0');
  assert.equal(rail[0].querySelector('.wsname').children[1].textContent, 'Home');
});

// The other half of the per-space rule, and the one that breaks silently.
test('the active pane takes its rows from the snapshot, keeping the live ids', () => {
  // The per-space copy of the active space holds the same titles with EMPTY
  // ids. A pane built from it looks identical and is completely inert: the
  // click handler has no id to send sidebarTabClicked with.
  const rail = panes(buildRail(railSnapshot()));
  assert.deepEqual(rowIds(rail[1]), ['42']);
  assert.deepEqual(
      rail[1].querySelectorAll('.fold .tabrow').map((n) => n.dataset.tabId),
      ['77']);
});

// The same silent break, for the tiles now that they are drawn beside the rail
// rather than in the active pane.
test('the hoisted grid keeps the live ids the snapshot carries', () => {
  const snap = railSnapshot();
  assert.deepEqual(favIds(buildFavGrid(snap.favorites)), ['https://work.test/']);
});

test('the rail declares which space the live ids belong to', () => {
  // A dragged row's SOURCE is this, not the pane it was dropped on.
  assert.equal(buildRail(railSnapshot()).dataset.activeSpace, '1');
});

test('a pane whose space carries no lists renders empty, not borrowed', () => {
  // Every per-space field is optional. Falling back to the top level here is
  // exactly the bug, so absence has to mean nothing rather than "the active
  // space's".
  const rail = panes(buildRail({
    activeSpace: 0,
    tabs: [{id: '1', title: 'Active tab'}],
    favorites: [{id: 'f'}],
    pinned: [{id: 'p', title: 'Active pin'}],
    spaces: [{name: 'A'}, {name: 'B'}],
  }));
  assert.deepEqual(rowTitles(rail[1]), []);
  assert.deepEqual(favIds(rail[1]), []);
  assert.equal(
      rail[1].querySelectorAll(`[data-folder-id="${PINNED_FOLD_ID}"]`).length,
      0, 'an empty pinned section, not the active space\'s pins');
});

test('a snapshot with no spaces still renders one pane', () => {
  const rail = buildRail({activeSpace: 0, spaceName: 'Solo',
                          tabs: [{id: '1', title: 'Only tab'}]});
  assert.equal(panes(rail).length, 1);
  assert.deepEqual(rowTitles(rail), ['Only tab'],
      'the sole pane is the active one, so it keeps the snapshot\'s rows');
});

// --- the hoisted address row ------------------------------------------------

// Two spaces, each showing a different origin, so a bar left pointing at the
// wrong one is visible rather than coincidentally right.
const originSnapshot = () => ({
  activeSpace: 1,
  spaces: [
    {name: 'Home',
     tabs: [{id: '', title: 'H', url: 'https://home.test/page', selected: true}]},
    {name: 'Work',
     tabs: [{id: '', title: 'W', url: 'https://work.test/page', selected: true}]},
  ],
  tabs: [{id: '42', title: 'W', url: 'https://work.test/page', selected: true}],
});

// The space dots had this exact defect and were hoisted out of the panes for it
//: one copy per pane, and the swipe translates all of them.
test('a rail pane draws no address row of its own', () => {
  const rail = buildRail(originSnapshot());
  assert.equal(rail.querySelectorAll('.urlrow').length, 0,
               'the rail owns no address row - apply() puts one beside it');
  assert.equal(rail.querySelectorAll('.urlbar').length, 0);
});

test('the hoisted row opens on the active space\'s origin', () => {
  // Origin, never the path: the bar is not a location field.
  assert.equal(buildUrlRow(originSnapshot()).querySelector('.urlbar').title,
               'https://work.test');
});

test('the hoisted row follows the space the rail is heading for', () => {
  const snap = originSnapshot();
  const root = document.createElement('div');
  root.append(buildUrlRow(snap), buildRail(snap));
  const title = () => root.querySelector('.urlrow .urlbar').title;

  markActiveSpaceUrl(root, snap, 0);
  assert.equal(title(), 'https://home.test');
  markActiveSpaceUrl(root, snap, 1);
  assert.equal(title(), 'https://work.test');
});

test('Overview leaves the row on the space the rail came from', () => {
  // Overview is a pane, not a space, and shows no page of its own.
  const snap = originSnapshot();
  const root = document.createElement('div');
  root.append(buildUrlRow(snap), buildRail(snap));
  markActiveSpaceUrl(root, snap, 0);
  markActiveSpaceUrl(root, snap, -1);
  assert.equal(root.querySelector('.urlrow .urlbar').title,
               'https://home.test');
});

// --- createFoldState, as it reaches the DOM ---------------------------------

test('the pinned sentinel is the id the pinned section renders under', () => {
  // The click handler routes by this id, so the export and the DOM have to be
  // the same string or the pinned section folds nothing.
  const root = render({pinned: [{id: 'p', title: 'P'}]});
  assert.equal(root.querySelector('.frow').dataset.folderId, PINNED_FOLD_ID);
  assert.equal(root.querySelector('.fold').dataset.folderBody, PINNED_FOLD_ID);
});

test('a fold recorded against the pinned sentinel closes the rendered section', () => {
  const folds = createFoldState();
  const pushed = {pinned: [{id: 'p', title: 'P'}], pinnedCollapsed: false};
  assert.equal(render(folds.applyTo(pushed)).querySelector('.fold')
      .classList.contains('closed'), false);

  folds.record(PINNED_FOLD_ID, true);
  assert.equal(render(folds.applyTo(pushed)).querySelector('.fold')
      .classList.contains('closed'), true,
      'the fold must show before the push catches up, or it flickers');
});

test('an unfold recorded against the pinned sentinel reopens it', () => {
  // The other direction is the one a Set could not express: opening is an
  // OPINION, and a push that still says collapsed must not undo it.
  const folds = createFoldState();
  folds.record(PINNED_FOLD_ID, false);
  const stale = {pinned: [{id: 'p', title: 'P'}], pinnedCollapsed: true};
  assert.equal(render(folds.applyTo(stale)).querySelector('.fold')
      .classList.contains('closed'), false);
});

test('a folder fold round-trips through the render', () => {
  const folds = createFoldState();
  const pushed = {folders: [{id: 'g', name: 'Group', closed: false, tabs: []}]};
  const closedOf = (snap) =>
      render(folds.applyTo(snap)).querySelector('.fold').classList.contains('closed');

  folds.record('g', true);
  assert.equal(closedOf(pushed), true);
  folds.record('g', false);
  assert.equal(closedOf(pushed), false);
});

test('an intent the push has caught up with stops overriding the model', () => {
  // The overlay is OPTIMISTIC, not authoritative: the browser persists the
  // fold, so an intent that agrees with what just arrived has done its job.
  // Held past that it would mask a fold changed in the SwiftUI arm or in
  // another window - this surface would never see it move again.
  const folds = createFoldState();
  const closedOf = (closed) => render(folds.applyTo(
      {folders: [{id: 'g', name: 'G', closed, tabs: []}]}))
      .querySelector('.fold').classList.contains('closed');

  folds.record('g', true);
  assert.equal(closedOf(true), true, 'the push has caught up');
  assert.equal(closedOf(false), false,
      'a fold reopened elsewhere must reach this surface');
});

test('a pinned intent the push has caught up with stops overriding it too', () => {
  // Same rule against the space-level flag, which is a separate branch.
  const folds = createFoldState();
  const pinned = [{id: 'p', title: 'P'}];
  const closedOf = (pinnedCollapsed) =>
      render(folds.applyTo({pinned, pinnedCollapsed}))
          .querySelector('.fold').classList.contains('closed');

  folds.record(PINNED_FOLD_ID, true);
  assert.equal(closedOf(true), true);
  assert.equal(closedOf(false), false);
});

test('the pinned sentinel folds without being a group', () => {
  // It has no group id: it never arrives in snapshot.folders, and prune must
  // not treat it as a folder that has gone away.
  const folds = createFoldState();
  folds.record(PINNED_FOLD_ID, true);
  folds.prune({folders: [{id: 'g', name: 'G', tabs: []}]});
  const snap = folds.applyTo(
      {pinned: [{id: 'p', title: 'P'}], pinnedCollapsed: false,
       folders: [{id: 'g', name: 'G', tabs: []}]});
  assert.equal(snap.folders.some((f) => f.id === PINNED_FOLD_ID), false);
  assert.equal(render(snap).querySelector(
      `.fold[data-folder-body="${PINNED_FOLD_ID}"]`).classList.contains('closed'),
      true, 'pruning the group list must not drop the pinned fold');
});

test('a fold intent for a departed folder stops reaching the render', () => {
  const folds = createFoldState();
  folds.record('gone', true);
  folds.prune({folders: [{id: 'stays', name: 'Stays', tabs: []}]});
  const root = render(folds.applyTo(
      {folders: [{id: 'gone', name: 'Gone', closed: false, tabs: []}]}));
  assert.equal(root.querySelector('.fold').classList.contains('closed'), false,
      'a folder that came back must not inherit a stale fold');
});

test('a fold applied to one rail pane does not leak into the others', () => {
  // applyTo rewrites the TOP-LEVEL folders, which only the active pane reads.
  // Group ids are unique to a space, so a fold must land on exactly one pane.
  const folds = createFoldState();
  folds.record('g-work', true);
  const rail = panes(buildRail(folds.applyTo(railSnapshot())));
  const bodyOf = (pane, id) =>
      pane.querySelector(`.fold[data-folder-body="${id}"]`);
  assert.equal(bodyOf(rail[1], 'g-work').classList.contains('closed'), true);
  assert.equal(bodyOf(rail[0], 'g-home').classList.contains('closed'), false);
});

// --- #sbUrl -------------------------------------------------------

test('the url bar renders the active tab host, and the search placeholder under it', () => {
  // the field shows the host at rest. The placeholder is what an
  // empty space, with no active tab, falls back to.
  const bar = render({tabs: [{id: 't', selected: true,
                              url: 'https://example.com/path'}]})
      .querySelector('#sbUrl');
  assert.equal(bar.tagName, 'INPUT');
  assert.equal(bar.value, 'example.com');
  assert.equal(bar.placeholder, '⌕ Search or enter URL');
});

test('the url bar exposes the active tab origin without printing it', () => {
  const bar = render({tabs: [
    {id: 't1', url: 'https://one.example/x'},
    {id: 't2', selected: true, url: 'https://two.example/y?z=1'},
  ]}).querySelector('#sbUrl');
  assert.equal(bar.title, 'https://two.example');
  assert.match(bar.getAttribute('aria-label'), /two\.example/);
});

test('no selected tab leaves the url bar with no origin to expose', () => {
  const bar = render({tabs: [{id: 't', url: 'https://one.example/'}]})
      .querySelector('#sbUrl');
  assert.equal(bar.title, '');
  assert.doesNotMatch(bar.getAttribute('aria-label'), /example/);
});

test('the url bar follows the same ink var the rest of the sidebar reads', () => {
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8');
  const rule = css.match(/\.urlbar\s*{[^}]*}/)[0];
  // .sidebar sets color: var(--fg, ...); inherit rides that same value rather
  // than hardcoding a second one that could drift from it.
  assert.match(rule, /color:\s*inherit/);
});

// a pinned row closes in two stages, per the lab. The dash
// closes the tab and leaves the pin; the x on an already-cold pin takes the
// pin itself.
test('a loaded pinned row shows the dash, a cold one shows the x', () => {
  const sidebar = render({
    pinned: [
      {id: 'p1', title: 'Loaded pin', url: 'https://a.test/'},
      {id: '', title: 'Cold pin', url: 'https://b.test/'},
    ],
    tabs: [{id: 't1', title: 'Regular', url: 'https://c.test/'}],
  });
  const closeOf = (title) =>
      [...sidebar.querySelectorAll('.tabrow')]
          .find((r) => r.querySelector('.tl').textContent === title)
          .querySelector('.x');

  const loaded = closeOf('Loaded pin');
  assert.equal(loaded.title, 'Close tab (keep pinned)');
  assert.ok(loaded.querySelector('.dg'), 'the loaded pin needs the dash');

  const cold = closeOf('Cold pin');
  assert.equal(cold.title, 'Remove pinned tab');
  assert.ok(cold.querySelector('.xg'), 'a cold pin keeps the x');

  // An unpinned row is untouched by any of this.
  const regular = closeOf('Regular');
  assert.equal(regular.title, 'Close');
  assert.ok(regular.querySelector('.xg'));
});

// an icon that names no colour leaves the ring on the tint, and the
// tint mixed toward white disappeared on a light one. Graded against the row as
// PAINTED - the selected row's white wash over the tint - not the bare tint.
test('the pane carries a ring fallback that clears 3:1 on the painted row', () => {
  for (const tint of ['#12282E', '#F0F4F8']) {
    const sidebar = render({spaceTint: tint, tabs: [{id: 't1', title: 'One'}]});
    const fallback = sidebar.querySelector('.sidebar')
                         .style.getPropertyValue('--ringfb');
    assert.ok(fallback, `no ring fallback on ${tint}`);
    const ground = rowBackdrop(parseRgb(tint));
    const ratio = contrastRatio(parseRgb(fallback), ground);
    assert.ok(ratio >= 3, `${fallback} on ${tint} reads ${ratio.toFixed(2)}:1`);
  }
});

test('a tint that never resolved to a colour leaves the ring to the sheet', () => {
  const sidebar = render({spaceTint: 'oceanBlue', tabs: []});
  assert.equal(
      sidebar.querySelector('.sidebar').style.getPropertyValue('--ringfb'), '');
});

// the ring rules key off .brdr, and rows had none - so the
// selected and loading rings reached favourite tiles only.
test('a row carries the ring element, like a favourite tile does', () => {
  const sidebar = render({tabs: [{id: 't1', title: 'One', selected: true}]});
  const row = sidebar.querySelector('.tabrow');
  assert.ok(row.querySelector('.brdr'), 'no ring element on the row');
  assert.ok(sidebar.querySelector('.fav .brdr') === null ||
                sidebar.querySelector('.fav'),
            'tiles are unaffected either way');
});

test('a selected row does not redefine the tint wash', () => {
  // The lab sets --selfill once on the pane as a NEUTRAL wash and says the tint
  // wash is favourites-only (swipe-lab.html:211, 3206). Redefining it per row as
  // an opaque space-tint mix made every selected row a slab of the space
  // colour, which is the tile treatment rather than the row one.
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  // Comments stripped first: this stylesheet names .row.tabrow in prose, and a
  // naive match lands inside that comment rather than on the rule - which is
  // exactly what the first version of this test did.
  const css = fs.readFileSync(cssPath, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
  const rule = css.match(/\.row\.tabrow\s*{[^}]*}/)[0];
  assert.equal(/--selfill/.test(rule), false,
               '.row.tabrow must not redefine --selfill');
  // It still needs the ring colour, which IS per row.
  assert.match(rule, /--washc/);
  assert.match(rule, /position:\s*relative/);
});

// the JS half shipped and the CSS half did not, so the icon laid out
// at its own bitmap size and painted over the tile.
test('a favourite tile contains its favicon the way a row does', () => {
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8');
  const img = css.match(/\.fav\s+\.fvimg\s*{[^}]*}/);
  assert.ok(img, '.fav .fvimg has no rule, so the icon is unconstrained');
  assert.match(img[0], /position:\s*absolute/);
  // contain, not cover. The tile is large enough that cover's crop shows: it
  // scaled a padded or non-square favicon up past the edges, so only sites
  // shipping a square full-bleed icon looked right.
  assert.match(img[0], /object-fit:\s*contain/);
  // The chip has to clip it, or absolute positioning just moves the overflow.
  const chip = css.match(/\.fav\s+\.fvi\s*{[^}]*}/);
  assert.ok(chip);
  assert.match(chip[0], /overflow:\s*hidden/);
  assert.match(chip[0], /position:\s*relative/);
});

// --- tab row icon slot, matched to the favourite tile's size ---------------

test('a tab row\'s favicon slot matches the favourite tile\'s size and ' +
         'radius',
     () => {
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
  const sizing = css.match(/\.row\s+\.fv\s*{\s*width:[^}]*}/);
  assert.ok(sizing, 'no sizing rule for .row .fv');
  assert.match(sizing[0], /width:\s*16px/);
  assert.match(sizing[0], /height:\s*16px/);
  assert.match(sizing[0], /border-radius:\s*5px/);
});

test('the wider row icon still leaves room for the title and the close ' +
         'button at the narrowest sidebar',
     () => {
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
  const ruleFor = (selector) => {
    const at = css.indexOf(selector + ' {');
    assert.notEqual(at, -1, `no rule for ${selector}`);
    return css.slice(at, css.indexOf('}', at));
  };
  const num = (rule, prop) => Number(rule.match(new RegExp(prop + ':\\s*(\\d+)px'))[1]);
  const row = ruleFor('.row');
  const icon = num(ruleFor('.row .fv'), 'width');
  const close = num(ruleFor('.row .x'), 'width');
  const gap = num(row, 'gap');
  const padLeft = 9, padRight = 8;  // .row's padding: 0 8px 0 9px.
  const fixed = padLeft + padRight + icon + close + gap * 2;
  const MIN_SIDEBAR_WIDTH = 160;
  assert.ok(fixed < MIN_SIDEBAR_WIDTH - 40,
      `only ${MIN_SIDEBAR_WIDTH - fixed}px left for the title at 160px`);
});

// --- the space dots bar does not ride with the panes ---------------

test('no rail pane draws a space dots bar of its own', () => {
  // One per pane meant the swipe translated every copy, so the bar scrolled
  // away with the workspace instead of staying under it.
  const rail = buildRail(railSnapshot());
  for (const pane of panes(rail)) {
    assert.equal(pane.querySelectorAll('.spacedots').length, 0);
  }
  assert.equal(rail.querySelectorAll('.spacedots').length, 0);
});

test('a single-space sidebar still draws its own bar', () => {
  // The rail is only built for two or more spaces, so the direct render must
  // keep the bar it has always had.
  assert.equal(render(FULL).querySelectorAll('.spacedots').length, 1);
});

test('markActiveSpaceDot moves the active chip without a rebuild', () => {
  const root = document.createElement('div');
  root.appendChild(buildSpaceDots(
      [{name: 'Home', color: '#0f0'}, {name: 'Work', color: '#111'}], 0,
      /*withLeadingOverview=*/ true));
  const dots = () => root.querySelectorAll('.sdot[data-space-index]');
  assert.deepEqual(dots().map((d) => d.classList.contains('active')),
                   [true, false]);

  markActiveSpaceDot(root, 1);
  assert.deepEqual(dots().map((d) => d.classList.contains('active')),
                   [false, true]);
  // The initial rides the active chip alone, so a stale one left behind is the
  // failure a class-only check would miss.
  assert.deepEqual(dots().map((d) => d.querySelector('.d').textContent),
                   ['', 'W']);
});

// --- no host-derived hue anywhere behind an icon ---------------

test('an icon slot carries no tint, so nothing shows behind a favicon', () => {
  // The chip was an FNV hash of the host, standing in for a favicon before one
  // was served. /urlicon answers now, so the hue only appeared when a lookup
  // found nothing - a random coloured square - and it flashed on every rebuild.
  const root = render({
    tabs: [{id: '7', title: 'live', color: 'hsl(9,65%,55%)'}],
    favorites: [{id: 'https://a.test/', title: 'a', color: 'hsl(9,65%,55%)'}],
  });
  for (const el of [root.querySelector('.tabrow'), root.querySelector('.fav')]) {
    assert.equal(el.style.getPropertyValue('--favc'), '',
                 'the snapshot colour must not reach the cascade');
  }
});

test('the stylesheet paints no --favc and keeps the slot square', () => {
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
  assert.equal(css.includes('--favc'), false,
               'a rule still reads the hash hue, so a slot can still paint it');
  // The slot keeps its box: an icon that never resolves is an empty square, not
  // a collapsed one that reflows the row around it.
  for (const sel of [/\.fav\s+\.fvi\s*{[^}]*}/, /\.row\s+\.fv\s*{[^}]*}/]) {
    const rule = css.match(sel);
    assert.ok(rule, `no rule matching ${sel}`);
    assert.match(rule[0], /(width|height):/);
    assert.equal(/background:/.test(rule[0]), false,
                 `${sel} still paints a background behind the icon`);
  }
});

test('a selected tile washes with the space tint alone', () => {
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
  // Blending the hash hue into the wash put the same arbitrary colour behind
  // every selected tile. The space tint is the only colour the user chose.
  for (const rule of css.match(/--washc:[^;]*;/g) || []) {
    assert.match(rule, /--selbase/);
    assert.equal(rule.includes('color-mix'), false,
                 `the wash still mixes another colour: ${rule}`);
  }
});

// --- nothing paints until the icon has pixels ------------------

test('an icon is not painted until it loads, and is removed if it never does', () => {
  // The img used to be appended with its src set and only removed once `error`
  // fired. Between those two points the browser painted its own missing-image
  // glyph, which is what a space switch flashed once the hue stopped covering it.
  const root = render({
    tabs: [{id: '7', title: 'live'}],
    favorites: [{id: 'https://a.test/', title: 'a'}],
  });
  for (const slot of [root.querySelector('.tabrow .fv'),
                      root.querySelector('.fav .fvi')]) {
    const img = slot.querySelector('.fvimg');
    assert.ok(img, 'the request is made');
    assert.equal(img.classList.contains('ok'), false,
                 'and nothing is painted while it is in flight');
    img.dispatchEvent(new Event('load'));
    assert.ok(img.classList.contains('ok'), 'a loaded icon is painted');
  }

  const failed = render({tabs: [{id: '8', title: 'gone'}]})
      .querySelector('.tabrow .fv');
  failed.querySelector('.fvimg').dispatchEvent(new Event('error'));
  assert.equal(failed.querySelectorAll('.fvimg').length, 0,
               'an icon that never resolves is taken out of the tree');
});

test('an already-decoded icon is painted in the same turn, with no blank frame', () => {
  // `load` fires a turn late even for an image the renderer has already decoded,
  // so waiting for it would leave the slot empty for a frame on every push - the
  // "icon appearing for a quick moment" half of the report. `complete` alone is
  // not the test: it is true for a FAILED load too, hence naturalWidth.
  const real = document.createElement.bind(document);
  document.createElement = (tag) => {
    const el = real(tag);
    if (tag === 'img') {
      Object.defineProperty(el, 'complete', {value: true, configurable: true});
      Object.defineProperty(el, 'naturalWidth', {value: 16, configurable: true});
    }
    return el;
  };
  try {
    const img = render({tabs: [{id: '9', title: 'cached'}]}).querySelector('.fvimg');
    assert.ok(img.classList.contains('ok'),
              'a decoded image must be painted without waiting for the event');
  } finally {
    document.createElement = real;
  }
});

test('a complete but empty image is not painted', () => {
  // `complete` goes true the moment a load FAILS, so keying on it alone paints
  // the broken-image glyph deliberately.
  const real = document.createElement.bind(document);
  document.createElement = (tag) => {
    const el = real(tag);
    if (tag === 'img') {
      Object.defineProperty(el, 'complete', {value: true, configurable: true});
      Object.defineProperty(el, 'naturalWidth', {value: 0, configurable: true});
    }
    return el;
  };
  try {
    const img = render({tabs: [{id: '10', title: 'broken'}]}).querySelector('.fvimg');
    assert.equal(img.classList.contains('ok'), false);
  } finally {
    document.createElement = real;
  }
});

// --- a row with no live tab still asks for an icon ---------------

test('a cold row asks the URL-keyed cache, since it has no tab id to ask with', () => {
  const img = render({tabs: [{id: '', url: 'https://cold.test/a b', title: 'c'}]})
      .querySelector('.tabrow .fvimg');
  assert.ok(img, 'a restored row used to request nothing at all');
  assert.equal(img.src,
      '/urlicon/' + SCOPE + '/https%3A%2F%2Fcold.test%2Fa%20b.png');
});

test('a snapshot naming no scope asks the URL-keyed cache for nothing', () => {
  // The scope is a per-profile token. A row that guessed the path without one
  // would be answered from whichever profile last filled the cache.
  const root = document.createElement('div');
  renderSidebar(root, {tabs: [{id: '', url: 'https://cold.test/', title: 'c'}]});
  assert.equal(root.querySelectorAll('.tabrow .fvimg').length, 0);

  const favRoot = document.createElement('div');
  renderSidebar(favRoot, {favorites: [{id: 'https://fav.test/'}]});
  assert.equal(favRoot.querySelectorAll('.fav .fvimg').length, 0);
});

test('a live row falls back to the URL-keyed icon, once', () => {
  // The tab-keyed cache holds only what a live WebContents pushed, and after a
  // cold launch that is empty for every restored tab - so the 404 is the
  // ordinary case, not an error.
  const slot = render({tabs: [{id: '7', url: 'https://x.test/', title: 't'}]})
      .querySelector('.tabrow .fv');
  const img = slot.querySelector('.fvimg');
  assert.equal(img.src, '/favicon/' + SCOPE + '/7.png');
  img.dispatchEvent(new Event('error'));
  assert.equal(img.src, '/urlicon/' + SCOPE + '/https%3A%2F%2Fx.test%2F.png');
  assert.equal(slot.querySelectorAll('.fvimg').length, 1, 'still in the tree');
  // A second failure is the end of it. Re-arming would loop the two sources
  // against each other for as long as the row is on screen.
  img.dispatchEvent(new Event('error'));
  assert.equal(slot.querySelectorAll('.fvimg').length, 0);
});

test('a row that already 404d asks one source on the next push, not two', () => {
  // A push runs on every navigation and title change, so a row whose tab-keyed
  // icon misses paid for that 404 again on each one.
  const requests = [];
  const real = document.createElement.bind(document);
  document.createElement = (tag) => {
    const el = real(tag);
    if (tag === 'img') {
      let value = '';
      Object.defineProperty(el, 'src', {
        get: () => value,
        set: (v) => { value = v; requests.push(v); },
        configurable: true,
      });
    }
    return el;
  };
  try {
    const tab = {id: '31', url: 'https://two-pushes.test/', title: 't'};
    const first = render({tabs: [tab]}).querySelector('.tabrow .fvimg');
    assert.deepEqual(requests, ['/favicon/' + SCOPE + '/31.png']);
    first.dispatchEvent(new Event('error'));
    assert.equal(requests.length, 2, 'the 404 costs one fallback request');

    requests.length = 0;
    const second = render({tabs: [tab]}).querySelector('.tabrow .fvimg');
    assert.deepEqual(requests,
                     ['/urlicon/' + SCOPE + '/https%3A%2F%2Ftwo-pushes.test%2F.png'],
                     'the second push must not re-run the request it knows 404s');

    // If the URL-keyed source misses too, the live one gets another go and the
    // memo is dropped, so a tab that resolves its own favicon later is never
    // stuck on a URL-keyed answer that does not exist.
    requests.length = 0;
    second.dispatchEvent(new Event('error'));
    assert.deepEqual(requests, ['/favicon/' + SCOPE + '/31.png']);
    requests.length = 0;
    render({tabs: [tab]});
    assert.deepEqual(requests, ['/favicon/' + SCOPE + '/31.png']);
  } finally {
    document.createElement = real;
  }
});

test('a row with no url and no icon is still taken out of the tree', () => {
  const slot = render({tabs: [{id: '8', title: 'gone'}]}).querySelector('.tabrow .fv');
  slot.querySelector('.fvimg').dispatchEvent(new Event('error'));
  assert.equal(slot.querySelectorAll('.fvimg').length, 0);
});

test('the stylesheet paints no icon that is not marked ok', () => {
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '');
  // Anchored at line start: a space satisfies a lookbehind, so `.row .fvimg`
  // matched the bare selector and the assertion read the wrong rule.
  const hidden = css.match(/^\.fvimg\s*{[^}]*}/m);
  assert.ok(hidden, 'no bare .fvimg rule, so an in-flight icon still paints');
  assert.match(hidden[0], /display:\s*none/);
  const shown = css.match(/\.fvimg\.ok\s*{[^}]*}/);
  assert.ok(shown, 'no .fvimg.ok rule, so a loaded icon never appears');
  assert.match(shown[0], /display:\s*(block|initial|revert)/);
});

// --- a favourite with no icon shows its initial --------------------------

// A favourite whose icon never arrives used to be an empty square. The plate
// sits UNDER the img, so a loaded icon covers it and a tile with none keeps it.
test('a favourite with no cached icon shows the host\'s initial on its hue', () => {
  const grid = buildFavGrid([
    {id: 'https://www.example.com/', title: 'Example', color: 'hsl(278,65%,55%)'},
  ]);
  const plate = grid.querySelector('.fav .fvi .fvinit');
  assert.ok(plate, 'the tile has no initial plate');
  assert.equal(plate.textContent, 'E', 'the letter is the host\'s, not the title\'s');
  assert.equal(plate.style.background, 'hsl(278,65%,55%)');
  assert.ok(plate.style.color, 'the ink was not lifted against the plate');
  assert.notEqual(plate.style.color, plate.style.background);
});

test('a favourite whose icon loads stacks the img above the plate', () => {
  const grid = buildFavGrid([{id: 'https://example.com/', color: '#336699'}]);
  const chip = grid.querySelector('.fav .fvi');
  const kids = [...chip.children].map((n) => n.className);
  assert.deepEqual(kids, ['fvinit', 'fvimg'],
                   'the img must come after the plate, so it paints on top');
  const cssPath = path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css');
  const css = fs.readFileSync(cssPath, 'utf8');
  const rule = css.match(/\.fav\s+\.fvinit\s*[,{][^}]*}/);
  assert.ok(rule, '.fav .fvinit has no rule, so the plate does not fill the chip');
  assert.match(rule[0], /position:\s*absolute/);
  assert.match(rule[0], /inset:\s*0/);
  // Hidden, not merely covered: a favicon with a transparent margin would
  // otherwise show the hue around its edges.
  const gone = css.match(/\.fvi:has\(\.fvimg\.ok\)\s+\.fvinit\s*{[^}]*}/);
  assert.ok(gone, 'no rule hides the plate once the icon has loaded');
  assert.match(gone[0], /display:\s*none/);
});

test('two favourites on one host share a hue and a different host differs', () => {
  const grid = buildFavGrid([
    {id: 'https://a.test/x', color: 'hsl(10,65%,55%)'},
    {id: 'https://a.test/y', color: 'hsl(10,65%,55%)'},
    {id: 'https://b.test/', color: 'hsl(200,65%,55%)'},
  ]);
  const hues = grid.querySelectorAll('.fvinit').map((n) => n.style.background);
  assert.equal(hues[0], hues[1]);
  assert.notEqual(hues[0], hues[2]);
  const letters = grid.querySelectorAll('.fvinit').map((n) => n.textContent);
  assert.deepEqual(letters, ['A', 'A', 'B']);
});

// Odd hosts must never throw: a tile with a strange letter beats a grid that
// fails to build.
test('an odd favourite id still yields one letter or none', () => {
  const grid = buildFavGrid([
    {id: ''}, {id: 'http://[::1]:8000/'}, {id: 'http://10.0.0.1/'},
    {id: 'chrome://settings/'}, {id: 'data:text/html,hi'},
  ]);
  const letters = grid.querySelectorAll('.fvinit').map((n) => n.textContent);
  assert.deepEqual(letters, ['', '1', '1', 'C', 'D']);
});

test('a favourite with no colour still gets a letter on the default tint', () => {
  const grid = buildFavGrid([{id: 'https://plain.test/'}]);
  const plate = grid.querySelector('.fvinit');
  assert.equal(plate.textContent, 'P');
  assert.ok(plate.style.background);
});

// The grid beside the rail is a child of the root, not of .sidebar, so the
// pane's tint tokens never reach it and a selected tile there rang in the
// sheet's grey mix.
test('the pinned grid carries the tint tokens its tiles ring against', () => {
  const grid = pinnedFavGrid({
    spaceTint: '#12282E',
    favorites: [{id: 'https://a.test/', title: 'A'}],
    spaces: [{name: 'S', profileId: 'home'}],
    activeSpace: 0,
  });
  assert.equal(grid.style.getPropertyValue('--selbase'), '#12282E');
  const fallback = parseRgb(grid.style.getPropertyValue('--ringfb'));
  assert.ok(fallback, 'the pinned grid has no ring fallback');
  const ratio = contrastRatio(fallback, rowBackdrop(parseRgb('#12282E')));
  assert.ok(ratio >= RING_MIN_CONTRAST, `reads ${ratio.toFixed(2)}:1`);
});

// The native look paints the tile on the surround under an ink wash. Graded
// against the tint, a dark space walked the ring toward white.
function onNativeSurround(fn) {
  const html = document.documentElement;
  html.classList.add('atrium-native-chrome');
  html.style.setProperty('--atrium-surround-color', '#ffffff');
  html.style.setProperty('--atrium-surround-ink', '32 33 36');
  try {
    fn(rowBackdrop([255, 255, 255], NATIVE_SEL_ALPHA, [32, 33, 36]));
  } finally {
    html.classList.remove('atrium-native-chrome');
    html.style.removeProperty('--atrium-surround-color');
    html.style.removeProperty('--atrium-surround-ink');
  }
}

test('on the native surround the ring fallback is graded against the surround',
     () => {
       onNativeSurround((ground) => {
         const snapshot = {
           spaceTint: '#12282E',
           tabs: [{id: 't1', title: 'One'}],
           favorites: [{id: 'https://a.test/', title: 'A'}],
           spaces: [{name: 'S', profileId: 'home'}],
           activeSpace: 0,
         };
         const pane = render(snapshot).querySelector('.sidebar');
         const grid = pinnedFavGrid(snapshot);
         for (const [name, el] of [['pane', pane], ['pinned grid', grid]]) {
           const fallback = parseRgb(el.style.getPropertyValue('--ringfb'));
           assert.ok(fallback, `the ${name} has no ring fallback`);
           const ratio = contrastRatio(fallback, ground);
           assert.ok(ratio >= RING_MIN_CONTRAST,
                     `the ${name} fallback reads ${ratio.toFixed(2)}:1`);
         }
       });
     });

// A cached bitmap paints in the same turn, before the chip has joined its
// tile, so a host looked up from the chip was null.
test('a cached icon colours the ring of the tile and row it was attached for',
     () => {
       const real = document.createElement;
       const red = new Uint8ClampedArray(16 * 16 * 4);
       for (let i = 0; i < red.length; i += 4) {
         red.set([200, 30, 30, 255], i);
       }
       document.createElement = (tag) => {
         const el = real(tag);
         if (tag === 'img') {
           el.complete = true;
           el.naturalWidth = 16;
         } else if (tag === 'canvas') {
           el.getContext = () => ({drawImage() {}, getImageData: () => ({data: red})});
         }
         return el;
       };
       try {
         const sidebar = render({
           spaceTint: '#12282E',
           tabs: [{id: 't1', url: 'https://a.test/', title: 'One'}],
           favorites: [{id: 'https://a.test/', title: 'A'}],
         });
         for (const sel of ['.fav', '.row.tabrow']) {
           const ring = parseRgb(
               sidebar.querySelector(sel).style.getPropertyValue('--ringb'));
           assert.ok(ring, `${sel} carries no ring colour`);
           // Lifted toward white to clear 3:1 on the dark row, so
           // the hue is what survives, not the saturation.
           assert.ok(ring[0] > 150 && ring[0] - ring[1] > 40 &&
                         ring[0] - ring[2] > 40,
                     `${sel} rings ${ring.join(',')}, not red`);
         }
       } finally {
         document.createElement = real;
       }
     });

// The suite builds its own roots, so nothing accumulates on the shared body.
dom.reset();

// Every index-keyed message (rename, snooze, cold close, reorder) names a slot
// in the list the browser keeps, which still holds one entry per tab. The row
// after a collapsed split would otherwise report the slot of the tab above it.
test('a row after a split reports the slot the browser knows it by', () => {
  const root = withSplit(['1', '2'], () => render({tabs: SPLIT_TABS}));
  const rows = root.querySelectorAll('.rows > .tabrow');
  assert.deepEqual(rows.map((r) => r.dataset.slot), ['0', '2']);
  assert.deepEqual(
      root.querySelectorAll('.splitrow .sseg').map((s) => s.dataset.slot),
      ['0', '1'], 'each pane answers for its own slot');
});
