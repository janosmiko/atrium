// Copyright 2026 The Atrium Authors
//
// The sidebar route: a prototype, not yet the shipping surface. It renders the
// favourites grid from a snapshot pushed by C++, using the same DOM shape as
// docs/lab/swipe-lab.html's buildFavGrid.
//
// The lab's version reads interaction state from module globals (drag source,
// per-pane selection, loading keys). None of that exists yet on this side, so
// the port takes those as fields on the snapshot instead - the render stays a
// pure function of its input, which is what makes it testable without a
// browser.
//
// No innerHTML: the default WebUI CSP sets require-trusted-types-for 'script'
// with an EMPTY allowlist, so every injection sink is blocked.

import {createSwipeController} from './swipe.js';
import {dropDecision, installRowDrag} from './sidebar-drag.js';
import {captureRowSlots, crossesPinnedLine, playRowFlip} from './sidebar-flip.js';
import {installFavoriteDrag} from './sidebar-fav-drag.js';
import {dotAt, installDotMagnify} from './sidebar-dot-magnify.js';
import {closeMenu, installRowMenu, openSnoozeMenu, place, rowsIn, slotOf,
        startInlineRename} from './sidebar-menu.js';
import {spaceGlyph} from './space-glyph.js';
import {installShelfDropZone} from './shelf-dropzone.js';
import {installOverscroll} from './overscroll-install.js';
import {installStripExtensions, pinnedExtensionActions,
        setActiveExtensionProfile} from './sidebar-ext-install.js';
import {MAX_PINNED_TILES, renderExtStrip, stripRows} from './sidebar-ext-strip.js';
import {buildQuickMenuButton} from './sidebar-ext-shared.js';
import {installSidebarMedia} from './sidebar-media.js';
import {installSidebarNavRow, navRowForRoot, setNavGroupInRow}
    from './sidebar-nav-row.js';
import {installHintTriggers, setBindings} from './hint-triggers.js';
import {REAL_TIMEOUT_CLOCK} from './clock.js';
import {installOverlayResizer} from './sidebar-overlay-resize.js';
import {handleSnoozedTabReturn} from './snoozed-tab-return.js';
import {shouldShowToast, dismissAfterMsForTimeout} from './notification-timeout.js';
import {showConfirmToast, showToast} from './toast-stack.js';
import {cssRgb, NATIVE_SEL_ALPHA, parseRgb, ringColorOn, ringLookFor, rowBackdrop, SEL_WASH_ALPHA} from './icon-color.js';
import {markLoadingKey, reapplyLoadingMarks, setActiveKeys, setLoadingKey, setLoadingPhase} from './loading-mark.js';
import {createLoadFill} from './load-fill.js';
import {startRowRename} from './row-rename.js';
import {flyRowsToOverview} from './archive-fly.js';
import {captureMoment} from './rewind.js';
import {openRewindBar, closeRewindBar, rewindRestored} from './rewind-bar.js';
import {openHistoryScrubber, SCRUBBER_CLASS} from './history-scrubber.js';
import {mirrorChoices, LOGGED_OUT} from './mirror.js';
import {isInsecureOrigin, hostOf} from './insecure-origin.js';
import {renderOverview} from './overview.js';
import {faviconPath, iconKey, isUrlIconPath, setIconScope,
        urlIconPath} from './icon-scope.js';

// Port of the lab's INSECURE_INK (swipe-lab.html:1906).
const INSECURE_INK = '#96201F';

// Rail positions before the first space. Named rather than a bare 1 so the
// conversions between rail and space indices are greppable.
const OVERVIEW_PANES = 1;

// The space tint a snapshot arrives without. A space dot and its bloom read it,
// so an unset one paints a neutral chip rather than nothing at all.
const DEFAULT_TINT = '#9ab';

// The pinned SECTION folds like a folder but is not one - it has no group id,
// and its state lives on the space.
export const PINNED_FOLD_ID = '__pinned__';

// The img is a child of the slot rather than a replacement for it, so an icon
// that never resolves leaves an empty square the row is already sized for.
//
// Nothing is painted until the bitmap is there. An img with a src and no pixels
// yet draws the browser's own missing-image glyph, and every push rebuilds every
// icon - so a space switch flashed that glyph on each slot whose favicon
// resolves nothing. The `ok` class is what the stylesheet paints.
// Rows whose tab-keyed icon answered 404, so the next push asks the URL-keyed
// source directly rather than paying for that 404 again. A push runs on every
// navigation, tab open and title change, so the repeat is not rare.

// Keyed by tab id AND url so a tab that navigates gets a fresh answer. Capped
// because tab ids are minted forever, BoundedIconStore's reason.
const coldTabIcons = new Set();
const MAX_COLD_TAB_ICONS = 512;

// The tab whose AI panel is open, or '' for none. Module-level so
// buildRow reapplies it on every rebuild, and pushed from the browser because
// the panel is its own route and cannot see these rows.
let aiPanelRowTabId = '';

// The tab ids of the panes of the open split, in pane order.
// Module-level for the same reason as aiPanelRowTabId: the split arrives on its
// own push, and buildRows has to reapply it on every rebuild.
let splitMemberIds = [];

export function setSplitMembers(ids) {
  splitMemberIds = (ids || []).map(String).filter(Boolean);
}

// The favourite whose tab has a Peek open, and the tab id the peeked page's
// favicon is cached under. Module-level so buildFav reapplies it on rebuilds.
let peekBadge = null;

function syncPeekBadge(cell) {
  cell.querySelector('.peekbadge')?.remove();
  const path = peekBadge && cell.dataset.favTab === peekBadge.tabId
      ? faviconPath(peekBadge.iconId)
      : '';
  if (!path) {
    return;
  }
  // Blink reuses an image it holds for the same URL, so a changed favicon
  // needs a new one.
  const src = peekBadge.rev ? path + '?r=' + peekBadge.rev : path;
  const badge = document.createElement('span');
  badge.className = 'peekbadge';
  const img = document.createElement('img');
  img.src = src;
  badge.appendChild(img);
  cell.appendChild(badge);
}

// How long a drag may go without a dragover before it counts as gone. Long
// enough that a pointer paused over a valid target is never cut short.
const DRAG_IDLE_MS = 1200;

// The ground the rows being built are PAINTED on, for the ring's contrast.
// Read when the icon is attached, not when it paints: a favicon resolving late
// belongs to the pane it was built for, and each pane draws its own tint.
let ringBackdrop = null;

// Adopted by the rebuild: a fresh img shows its plate until the browser
// answers, and a pruned image cache asks again for the oldest tiles. Kept for
// good only under the same URL-keyed path: a tab's icon can change under one
// path, and another scope is another profile's cache.
let carriedIcons = new Map();
// The path each img was asked for. The src property comes back absolute.
const iconSrcOf = new WeakMap();

function harvestLoadedIcons(root) {
  carriedIcons = new Map();
  if (!root) {
    return;
  }
  for (const img of root.querySelectorAll('img.fvimg.ok')) {
    const key = iconKey(iconSrcOf.get(img));
    if (!key) {
      continue;
    }
    const nodes = carriedIcons.get(key);
    if (nodes) {
      nodes.push(img);
    } else {
      carriedIcons.set(key, [img]);
    }
  }
}

// The row is rebuilt on every snapshot push, so the live fill and the phase
// it was last given both live out here. Without the replay a load that spans
// a push loses its fill for the rest of the run.
let fieldFill = null;
let lastLoadPhase = null;

// The wash alpha the window pushed, which follows the ink it chose. Read off
// the root because the tiles are still detached while they are built.
function selWashAlpha() {
  try {
    const pushed = Number.parseFloat(getComputedStyle(document.documentElement)
                                         .getPropertyValue('--atrium-sel-alpha'));
    return Number.isFinite(pushed) ? pushed : SEL_WASH_ALPHA;
  } catch (e) {
    return SEL_WASH_ALPHA;
  }
}

// The selected row as painted. On the native surround that is the ink wash
// over the surround, not the space tint: graded against a dark tint, a red
// icon walked toward white and vanished on the near-white surround.
function ringGround(tint) {
  const html = document.documentElement;
  if (!html?.classList.contains('atrium-native-chrome')) {
    return rowBackdrop(tint, selWashAlpha());
  }
  const surround = parseRgb(html.style.getPropertyValue('--atrium-surround-color'));
  const ink = parseRgb('rgb(' + html.style.getPropertyValue('--atrium-surround-ink') + ')');
  return rowBackdrop(surround, NATIVE_SEL_ALPHA, ink || [255, 255, 255]);
}

// The grid beside the rail is a child of the root, not of the pane, so the
// pane's tokens never reach its tiles: both carry their own.
function setRingTokens(el, spaceTint) {
  const tint = parseRgb(spaceTint);
  ringBackdrop = ringGround(tint);
  if (!spaceTint) {
    return;
  }
  el.style.setProperty('--selbase', spaceTint);
  // The ring for a row whose icon names no colour. The tint mixed toward
  // white was the old answer and it vanished on a light tint.
  if (tint && ringBackdrop) {
    el.style.setProperty('--ringfb', cssRgb(ringColorOn(tint, ringBackdrop)));
  }
}

function coldIconKey(tab) {
  return tab.id && tab.url ? tab.id + '\n' + tab.url : '';
}

function markColdTabIcon(key) {
  if (coldTabIcons.size >= MAX_COLD_TAB_ICONS) {
    coldTabIcons.delete(coldTabIcons.values().next().value);
  }
  coldTabIcons.add(key);
}

// The ring takes the icon's own colour. A grey mark leaves the tint mix. An
// icon of several colours wears them all as a gradient, each where it sits.
function wearIconColours(host, img, backdrop) {
  if (!host) {
    return;
  }
  const {color, gradient} = ringLookFor(img, {backdrop});
  if (color) {
    host.style.setProperty('--ringb', color);
  }
  host.classList.toggle('palette', !!gradient);
  if (gradient) {
    host.style.setProperty('--ringgrad', gradient);
  } else {
    host.style.removeProperty('--ringgrad');
  }
}

// `fallback` is tried once if `src` resolves nothing. A row whose tab is cold
// has no tab-keyed icon to serve, and the URL-keyed one is the only answer it
// can ever have. `hooks` tells the caller which of the two answered,
// and names the host: the slot is still detached when a cached bitmap paints.
function attachIcon(slot, src, fallback, hooks = {}) {
  const backdrop = ringBackdrop;
  const ringHost = () => hooks.host || slot.closest('.row, .fav');
  const carried = takeCarriedIcon(src) || takeCarriedIcon(fallback);
  if (carried && iconSrcOf.get(carried) === src && isUrlIconPath(src)) {
    slot.appendChild(carried);
    if (hooks.onPrimary) {
      hooks.onPrimary();
    }
    wearIconColours(ringHost(), carried, backdrop);
    return;
  }
  // Holds the slot until the fresh img answers. Only while it is still in this
  // slot: a later rebuild can adopt it into another tree.
  const placeholder = carried || null;
  const heldHere = () => placeholder && placeholder.parentNode === slot;
  if (placeholder) {
    slot.appendChild(placeholder);
    wearIconColours(ringHost(), placeholder, backdrop);
  }
  const img = document.createElement('img');
  img.className = 'fvimg';
  let remaining = fallback && fallback !== src ? fallback : '';
  let fellBack = false;
  const paint = () => {
    img.classList.add('ok');
    if (heldHere()) {
      placeholder.replaceWith(img);
    }
    if (!fellBack && hooks.onPrimary) {
      hooks.onPrimary();
    }
    wearIconColours(ringHost(), img, backdrop);
  };
  img.addEventListener('load', paint);
  img.addEventListener('error', () => {
    if (!remaining) {
      img.remove();
      if (heldHere()) {
        placeholder.remove();
      }
      return;
    }
    const next = remaining;
    remaining = '';
    fellBack = true;
    if (hooks.onFallback) {
      hooks.onFallback();
    }
    iconSrcOf.set(img, next);
    img.src = next;
  });
  iconSrcOf.set(img, src);
  img.src = src;
  // Painted in THIS turn when the renderer has the bitmap already, because
  // `load` fires a turn late even then and the slot would blink empty on every
  // push. naturalWidth as well as `complete`: `complete` goes true the moment a
  // load fails, so it alone would paint the glyph on purpose.
  if (img.complete && img.naturalWidth) {
    paint();
  }
  if (!placeholder) {
    slot.appendChild(img);
  }
}

// A node asked for under this exact path first, so a placeholder from another
// scope never displaces one this slot can keep.
function takeCarriedIcon(path) {
  const nodes = carriedIcons.get(iconKey(path));
  if (!nodes?.length) {
    return null;
  }
  const exact = nodes.findIndex((img) => iconSrcOf.get(img) === path);
  return nodes.splice(Math.max(exact, 0), 1)[0];
}

// From the host, not the title: the title is whatever the page last said.
// Only `www.` is dropped - no public-suffix list ships with the sidebar.
function hostInitial(url) {
  const host = hostOf(String(url || '')).replace(/^www\./i, '');
  const first = /[a-z0-9]/i.exec(host);
  return first ? first[0].toUpperCase() : '';
}

function buildFav(fav) {
  const cell = document.createElement('div');
  // Draggable to reorder the grid.
  cell.draggable = true;
  cell.className = 'fav';
  if (fav.selected) {
    cell.classList.add('sel');
  }
  cell.title = fav.title || '';
  cell.dataset.favId = fav.id || '';
  // A tile whose tab is next door carries that id: the click then activates
  // (moves) it rather than loading a second copy, so no ring either.
  if (fav.elsewhere) {
    cell.classList.add('elsewhere');
  }
  cell.dataset.favTab = fav.tabId || fav.elsewhereTabId || '';

  const dot = document.createElement('span');
  dot.className = 'fvi';
  // Under the img, so a loaded icon covers it and a tile whose icon never
  // arrives keeps its letter instead of an empty square.
  const plate = document.createElement('span');
  plate.className = 'fvinit';
  const tint = fav.color || DEFAULT_TINT;
  plate.style.background = tint;
  plate.style.color = glyphInkForTint(tint);
  plate.textContent = hostInitial(fav.id);
  dot.appendChild(plate);
  // a favourite has no live tab, so its icon comes from the
  // browser's URL-keyed cache rather than the tab-keyed one. Its id IS the
  // page URL - a favourite has no id of its own.
  const favIcon = urlIconPath(fav.id);
  if (favIcon) {
    attachIcon(dot, favIcon, '', {host: cell});
  }

  const border = document.createElement('span');
  border.className = 'brdr';

  cell.append(dot, border);
  syncPeekBadge(cell);
  return cell;
}

// Ported from the lab's syncSbUrlSecurity (swipe-lab.html ~3947-3955). Baked
// into buildUrlBar itself so a snapshot rebuild paints the right state on its
// first frame instead of catching up a moment later.
function applyUrlBarSecurity(bar, origin) {
  const bad = isInsecureOrigin(origin);
  bar.classList.toggle('insecure', bad);
  if (bad) {
    bar.placeholder = 'Not secure - ' + hostOf(origin);
    bar.style.color = INSECURE_INK;
    bar.style.opacity = '1';
    bar.setAttribute(
        'aria-label', 'Not secure, current site: ' + hostOf(origin));
  } else {
    bar.placeholder = '⌕ Search or enter URL';
    bar.style.color = '';
    bar.style.opacity = '';
    bar.setAttribute(
        'aria-label',
        origin ? 'Address and search, current site: ' + origin :
                 'Address and search');
  }
}

// No focus guard on the repaint: a click leaves the caret on the row while the
// palette takes the keyboard, and skipping the push there strands the row on
// the host it showed before the navigation.
export function showUrlBarOrigin(bar, origin) {
  bar.dataset.origin = origin || '';
  bar.title = origin || '';
  applyUrlBarSecurity(bar, origin);
  bar.value = restingUrlText(bar);
}

function restingUrlText(bar) {
  const origin = bar.dataset.origin || '';
  return origin ? hostOf(origin) : '';
}

export function buildUrlBar(origin) {
  const bar = document.createElement('input');
  bar.type = 'text';
  bar.setAttribute('id', 'sbUrl');
  bar.className = 'urlbar';
  bar.autocomplete = 'off';
  bar.spellcheck = false;
  // The palette owns every edit. The element stays an input so the
  // placeholder and the insecure treatment keep working, but nothing can be
  // typed into it and it is out of the tab order.
  bar.readOnly = true;
  bar.tabIndex = -1;
  showUrlBarOrigin(bar, origin);
  return bar;
}

// The row.selected tab across every section a snapshot can carry it in -
// there is no separate "active tab" field on the snapshot itself.
function findActiveTab(snapshot) {
  const sections = [
    ...(snapshot.pinned || [])
        .flatMap(p => p && p.kind === 'folder' ? (p.tabs || []) : [p]),
    ...(snapshot.folders || []).flatMap(f => f.tabs || []),
    ...(snapshot.tabs || []),
  ];
  const row = sections.find(t => t.selected);
  if (row) {
    return row;
  }
  // A claimed favourite tile can be the focused tab's only row, and a tile is
  // keyed by url: its `id` is the page url, the live id rides as `tabId`.
  const tile = (snapshot.favorites || []).find(f => f.selected);
  return tile ? {id: tile.tabId || '', url: tile.id || ''} : null;
}

function findActiveTabUrl(snapshot) {
  return findActiveTab(snapshot)?.url || '';
}

// Both, because a favourite tile is keyed by url and its row by tab id, and the
// two name the same focused page.
function activeRingKeys(snapshot) {
  const tab = findActiveTab(snapshot);
  return tab ? [tab.id, tab.url] : [];
}

// Origin only, e.g. "https://example.com" - never the full path, and never
// thrown for an unparseable/opaque url (new-tab page, chrome://, blank).
function originOf(url) {
  if (!url) {
    return '';
  }
  try {
    const origin = new URL(url).origin;
    return origin === 'null' ? '' : origin;
  } catch {
    return '';
  }
}

// Which snapshot holds a given space's rows. The ACTIVE space reads the
// top-level fields and every other one its own entry - buildRail's rule, and
// the reason a non-active pane's rows carry no live tab ids.
function spaceSlice(snapshot, spaceIndex) {
  return spaceIndex === (snapshot.activeSpace ?? 0)
      ? snapshot
      : (snapshot.spaces || [])[spaceIndex];
}

export function buildUrlRow(snapshot) {
  const row = document.createElement('div');
  row.className = 'urlrow';
  row.appendChild(buildUrlBar(originOf(findActiveTabUrl(snapshot))));
  row.appendChild(
      buildQuickMenuButton((name, args) => chrome.send(name, args)));
  fieldFill = createLoadFill(row);
  // A finished run replays as a second fade, so only a live phase carries
  // over to the new row.
  if (lastLoadPhase && lastLoadPhase.phase !== 'done') {
    fieldFill.setPhase(lastLoadPhase.phase, lastLoadPhase.fraction);
  }
  return row;
}

/** How much of the rail on screen is Overview, 0 to 1, for a rail painted at
    `at` with `leadingPanes` Overview panes in front of the spaces. */
export function overviewShare(at, leadingPanes = OVERVIEW_PANES) {
  return Math.min(Math.max(leadingPanes - at, 0), 1);
}

// They fade with the swipe rather than flipping at the settle, which would
// show the address of the space being left all the way into the Overview
// (swipe-lab.html:3724-3730).
export function applyOverviewFade(root, share) {
  // The extensions strip is a sibling of the root, not a child, so it is found
  // on the parent they share.
  const host = root.parentNode?.style ? root.parentNode : root;
  // On each surface, not on the parent: an inherited value there restyled
  // every row of the sidebar on every frame of the glide.
  for (const el of [...host.children, ...root.children,
                    ...root.querySelectorAll('.railpane')]) {
    if (el.matches('.urlrow, .favgrid, .exs-row, .railpane')) {
      el.style.setProperty('--ov-share', String(share));
    }
  }
  // A scale and an opacity need no width. The slide this replaced was sized in
  // pane widths, and the browser resizes the sidebar across the same gesture,
  // so it came apart at the width change.
  host.classList.toggle('ovcross', share > 0);
  // Past halfway the bar is more gone than there, and a click landing on a
  // ghost is worse than one landing on the pane under it.
  host.classList.toggle('ovpast', share > 0.5);
}

// Repoints the hoisted bar at the space the rail is heading for, without a
// rebuild - markActiveSpaceDot's counterpart. The bar is one element beside the
// rail now, so a swipe cannot slide the right copy into place any more.
export function markActiveSpaceUrl(root, snapshot, spaceIndex) {
  const bar = root.querySelector('.urlrow .urlbar');
  const slice = snapshot && spaceIndex >= 0
      ? spaceSlice(snapshot, spaceIndex)
      : null;
  // Overview is not a space and has no page of its own, so the bar keeps
  // showing whichever space the rail left.
  if (!bar || !slice) {
    return;
  }
  showUrlBarOrigin(bar, originOf(findActiveTabUrl(slice)));
}

// Lifted against the row the same way a favicon's ring is - a tint picked
// for a filled plate is not necessarily readable as bare text.
function glyphInkForTint(tint) {
  const rgb = parseRgb(tint) || parseRgb(DEFAULT_TINT);
  return cssRgb(ringBackdrop ? ringColorOn(rgb, ringBackdrop) : rgb);
}

// The pinned section's own fold header (docs/lab/swipe-lab.html's wshead),
// present whether or not there is anything under it to fold.
function buildSpaceName(snapshot, hasPinned, closed) {
  const name = document.createElement('div');
  name.className = 'wsname';
  // One slot for the icon and, when folding, the chevron - the lab's .wsic
  // pattern (swipe-lab.html:637-642). They share the position rather than the
  // chevron taking a column of its own, so the row never reflows on hover.
  const icon = document.createElement('span');
  icon.className = 'wsic';
  const glyph = document.createElement('span');
  glyph.className = 'wsic-i';
  const tint = snapshot.spaceTint || DEFAULT_TINT;
  glyph.style.color = glyphInkForTint(tint);
  glyph.appendChild(spaceGlyph(snapshot.spaceIcon, snapshot.spaceName || ''));
  icon.appendChild(glyph);
  const label = document.createElement('span');
  label.className = 'wsl';
  label.textContent = snapshot.spaceName || 'Space';
  name.append(icon, label);
  // The pin drop target, whether or not anything is pinned yet - an empty
  // pinned section renders no zone of its own to aim a drop at.
  name.dataset.dropzone = 'pinned';
  // Nothing to fold with no pinned tabs, so the row stays a plain icon - no
  // folder id and no chevron, and the delegated `.frow` click lookup passes
  // over it untouched.
  if (hasPinned) {
    name.classList.add('frow');
    name.dataset.folderId = PINNED_FOLD_ID;
    name.classList.toggle('closedH', !!closed);
    icon.appendChild(chevronGlyph());
  }
  return name;
}

// The copy drawn beside the rail, carrying the profile it was built for so the
// swipe can tell whether it is still the right one to show.
// The profile the pinned copies beside the rail are drawn for.
export function activeProfileOf(snapshot) {
  const spaces = snapshot.spaces || [];
  return spaces[snapshot.activeSpace ?? 0]?.profileId || '';
}

export function pinnedFavGrid(snapshot) {
  // Before the tiles: a cached icon paints as its tile is built, graded
  // against this ground.
  ringBackdrop = ringGround(parseRgb(snapshot.spaceTint));
  const grid = buildFavGrid(snapshot.favorites || []);
  setRingTokens(grid, snapshot.spaceTint);
  grid.dataset.profileId = activeProfileOf(snapshot);
  return grid;
}

export function buildFavGrid(favorites) {
  const grid = document.createElement('div');
  grid.className = 'favgrid';
  // A drop target as well as a display: dragging a tab here promotes it
  //. The hint below has said so since the grid was written.
  grid.dataset.dropzone = 'favorites';
  if (!favorites.length) {
    grid.classList.add('empty');
    const hint = document.createElement('div');
    hint.className = 'favhint';
    hint.textContent =
        'Drag tabs here - favorites live on this profile’s workspaces';
    grid.appendChild(hint);
    return grid;
  }
  for (const fav of favorites) {
    grid.appendChild(buildFav(fav));
  }
  return grid;
}

// Empty slots, not tiles: the pinned copy is absolutely positioned, so a pane
// that dropped the grid let every row below slide up under it, and a hidden
// duplicate tile still answers a .fav count, a selector and a screen reader.
export function buildFavRoom(favorites) {
  const room = document.createElement('div');
  room.className = 'favgrid';
  room.setAttribute('aria-hidden', 'true');
  if (!favorites.length) {
    room.classList.add('empty');
    return room;
  }
  for (const _fav of favorites) {
    const slot = document.createElement('div');
    slot.className = 'favslot';
    room.appendChild(slot);
  }
  return room;
}


// The close glyph. Built with createElementNS because SVG lives in a different
// namespace - createElement('path') would make an unknown HTML element that
// renders nothing.
const SVG_NS = 'http://www.w3.org/2000/svg';

function buildCloseGlyph() {
  const svg = document.createElementNS(SVG_NS, 'svg');
  svg.setAttribute('class', 'xg');
  svg.setAttribute('viewBox', '0 0 10 10');
  svg.setAttribute('width', '9');
  svg.setAttribute('height', '9');
  const path = document.createElementNS(SVG_NS, 'path');
  path.setAttribute('d', 'M1.5 1.5l7 7M8.5 1.5l-7 7');
  path.setAttribute('stroke', 'currentColor');
  path.setAttribute('stroke-width', '1.6');
  path.setAttribute('stroke-linecap', 'round');
  svg.appendChild(path);
  return svg;
}

function buildDashGlyph() {
  const svg = document.createElementNS(SVG_NS, 'svg');
  svg.setAttribute('class', 'dg');
  svg.setAttribute('viewBox', '0 0 10 10');
  svg.setAttribute('width', '9');
  svg.setAttribute('height', '9');
  const path = document.createElementNS(SVG_NS, 'path');
  path.setAttribute('d', 'M1.5 5h7');
  path.setAttribute('stroke', 'currentColor');
  path.setAttribute('stroke-width', '1.6');
  path.setAttribute('stroke-linecap', 'round');
  svg.appendChild(path);
  return svg;
}

// `host` is the row the icon paints on, never the span itself - the ring
// contrast is measured against the row's backdrop.
function buildFavicon(tab, host) {
  const favicon = document.createElement('span');
  favicon.className = 'fv';
  // The tab-keyed cache holds only what a LIVE WebContents pushed, so a
  // restored or pinned row with no tab yet can never be answered from it - the
  // URL-keyed one is what the browser backfills for those.
  const byUrl = urlIconPath(tab.url);
  const coldKey = coldIconKey(tab);
  // Browser-served, from THIS host rather than from
  // chrome://atrium-favicon: a host of its own was never reached, and the load
  // failed with net::ERR_FAILED before the data source ran. See atrium_ui.cc.
  const byTab = faviconPath(tab.id);
  if (byTab && !coldTabIcons.has(coldKey)) {
    attachIcon(favicon, byTab, byUrl, {
      host,
      onFallback: () => markColdTabIcon(coldKey),
      onPrimary: () => coldTabIcons.delete(coldKey),
    });
  } else if (byTab && byUrl) {
    // The memo sent the row straight here. If this misses too, the live source
    // gets another go and the memo is dropped, so a tab that resolves its own
    // favicon later is never stuck on a URL-keyed answer that does not exist.
    attachIcon(favicon, byUrl, byTab,
               {host, onFallback: () => coldTabIcons.delete(coldKey)});
  } else if (byUrl) {
    attachIcon(favicon, byUrl, '', {host});
  }
  return favicon;
}

// The panes of an open split as ONE row. Each pane is a .sseg
// carrying its own id, so the row handlers read the segment the click landed
// in and this row needs no chrome.send name of its own.
export function buildSplitRow(members, slots = [], options = {}) {
  const row = document.createElement('div');
  row.className = 'row tabrow splitrow';
  if (options.draggable) {
    row.draggable = true;
  }
  row.dataset.panes = String(members.length);
  // One row for several tabs, so DOM position no longer counts the slots the
  // browser persists. Every index-keyed message reads this instead.
  if (slots.length) {
    row.dataset.slot = String(slots[0]);
  }
  // The row itself answers for the first pane, so a click on the row's own
  // padding - outside every segment - still names a live tab.
  row.dataset.tabId = members[0].id || '';
  row.dataset.url = members[0].url || '';
  if (members.some((tab) => tab.selected)) {
    row.classList.add('sel');
  }

  members.forEach((tab, i) => {
    const seg = document.createElement('div');
    seg.className = 'sseg';
    seg.dataset.tabId = tab.id || '';
    seg.dataset.url = tab.url || '';
    if (slots.length > i) {
      seg.dataset.slot = String(slots[i]);
    }
    if (tab.selected) {
      seg.classList.add('segsel');
    }

    const title = document.createElement('span');
    title.className = 'tl';
    // textContent, not innerHTML: a page title is attacker-controlled
    //.
    title.textContent = tab.title || (tab.url ? hostOf(tab.url) : '');

    const close = document.createElement('button');
    close.className = 'x';
    close.title = 'Close pane';
    close.appendChild(buildCloseGlyph());

    seg.append(buildFavicon(tab, row), title, close);
    row.appendChild(seg);
  });

  // every row carries the ring element, or the selected ring and
  // the loading sweep reach favourites only.
  const border = document.createElement('span');
  border.className = 'brdr';
  row.appendChild(border);
  return row;
}

function buildRow(tab, options = {}) {
  const row = document.createElement('div');
  row.className = 'row tabrow';
  if (options.draggable) {
    row.draggable = true;
  }
  if (tab.selected) {
    row.classList.add('sel');
  }
  if (tab.discarded) {
    row.classList.add('hib');
  }
  // live, but in another window. Not the same as cold and not the
  // same as hibernated - the click takes the page back rather than loading it.
  if (tab.elsewhere) {
    row.classList.add('elsewhere');
    // The id the click needs. tab.id is empty for this row - it names no tab in
    // THIS window - so the id of the tab in the other window rides separately
    // and the click below sends it instead.
    row.dataset.elsewhereTabId = tab.elsewhereTabId || '';
  }
  row.dataset.tabId = tab.id || '';
  if (aiPanelRowTabId && tab.id === aiPanelRowTabId) {
    row.classList.add('aimark');
  }
  // Carried for every row. A row with no live tab has an empty id, and an
  // empty id is not something the browser can act on - so without this the
  // row is silently inert.
  row.dataset.url = tab.url || '';

  const favicon = buildFavicon(tab, row);

  const title = document.createElement('span');
  title.className = 'tl';
  // textContent, not innerHTML: a page title is attacker-controlled. The host
  // stands in until the page delivers a title.
  title.textContent = tab.title || (tab.url ? hostOf(tab.url) : '');

  const close = document.createElement('button');
  close.className = 'x';
  // Two stages on a pinned row (lab 3514-3518). While a tab is
  // open the dash closes it and leaves the pin behind; once nothing is loaded
  // the x takes the pin itself. A live tab id is what "loaded" means here.
  const pinnedLoaded = options.pinned && !!tab.id;
  if (options.pinned) {
    close.title = pinnedLoaded ? 'Close tab (keep pinned)' : 'Remove pinned tab';
  } else {
    close.title = 'Close';
  }
  close.appendChild(pinnedLoaded ? buildDashGlyph() : buildCloseGlyph());

  row.append(favicon, title);
  // One dot however many highlights the page carries.
  if (tab.marked) {
    const dot = document.createElement('span');
    dot.className = 'mdot';
    dot.title = 'Has highlights';
    row.appendChild(dot);
  }
  row.appendChild(close);
  // rows carry the ring element too, as the lab's do
  // (swipe-lab.html:3226). Without it every .brdr rule - the selected ring, and
  // the loading sweep added on top of it - reaches favourites only.
  const border = document.createElement('span');
  border.className = 'brdr';
  row.appendChild(border);
  return row;
}

export function buildRows(tabs, options = {}) {
  const rows = document.createElement('div');
  rows.className = 'rows';
  if (options.dropzone) {
    rows.dataset.dropzone = options.dropzone;
  }
  // A member the push does not list is skipped, and fewer than two members
  // left means the split is over - both cases fall back to ordinary rows, which
  // is what turns the row back into a plain one as panes close.
  const members = splitMemberIds
      .map((id) => tabs.find((tab) => String(tab.id) === id))
      .filter(Boolean);
  const collapsed = new Set(members.length >= 2 ? members : []);
  let splitDrawn = false;
  tabs.forEach((tab, slot) => {
    if (collapsed.has(tab)) {
      if (!splitDrawn) {
        splitDrawn = true;
        rows.appendChild(
            buildSplitRow(members, members.map((m) => tabs.indexOf(m)),
                          options));
      }
      return;
    }
    const row = buildRow(tab, options);
    row.dataset.slot = String(slot);
    rows.appendChild(row);
  });
  if (!tabs.length && options.dropzone) {
    // A zero-height flex column with no rows was a zero-area target and
    // nothing could be dropped into the tab list at all. The class
    // gives it a height with no visible content - the zone stays invisible.
    rows.classList.add('emptyzone');
  }
  return rows;
}

// A bare divider line. No label: the rows below it are the only unlabelled
// group, so a caption only repeated what the layout already says.
function buildSectionHeader({clearable = false} = {}) {
  const header = document.createElement('div');
  header.className = 'sechdr';
  // Only where there is something to clear - a control that does nothing is
  // worse than an absent one. No visible text: the hover hint (hint-triggers.js
  // ACTION_BY_CLASS) spells the label and the clearTabs chord.
  if (clearable) {
    const clear = document.createElement('button');
    clear.type = 'button';
    clear.className = 'sechdrclear';
    clear.title = 'Clear tabs';
    clear.setAttribute('aria-label', 'Clear tabs');
    clear.dataset.sidebarClear = '';
    clear.appendChild(clearGlyph());
    header.appendChild(clear);
  }
  return header;
}


// The folder and chevron glyphs, ported from the lab's folderGlyph/chev
// template literals. createElementNS, not createElement: SVG is a different
// namespace and an HTML <path> renders nothing.
function buildSvg(cls, viewBox, size, d, extra) {
  const svg = document.createElementNS(SVG_NS, 'svg');
  svg.setAttribute('class', cls);
  svg.setAttribute('viewBox', viewBox);
  svg.setAttribute('width', String(size));
  svg.setAttribute('height', String(size));
  const path = document.createElementNS(SVG_NS, 'path');
  path.setAttribute('d', d);
  path.setAttribute('fill', 'none');
  path.setAttribute('stroke', 'currentColor');
  path.setAttribute('stroke-width', extra.width);
  path.setAttribute('stroke-linejoin', 'round');
  if (extra.cap) {
    path.setAttribute('stroke-linecap', extra.cap);
  }
  svg.appendChild(path);
  return svg;
}

const folderGlyph = () => buildSvg(
    'fic-g', '0 0 14 14', 12,
    'M1.5 3.5a1 1 0 0 1 1-1h3.2l1.3 1.6h4.5a1 1 0 0 1 1 1v4.9a1 1 0 0 1-1 1h-9a1 1 0 0 1-1-1z',
    {width: '1.2'});

const chevronGlyph = () => buildSvg(
    'chev', '0 0 10 10', 10, 'M2 3.5l3 3 3-3', {width: '1.5', cap: 'round'});

// An archive tray: lid, box, handle. What Clear does is archive, not delete.
const clearGlyph = () => buildSvg(
    'clr-g', '0 0 14 14', 12,
    'M2 3h10v2.5H2z M3 5.5v5a1 1 0 0 0 1 1h6a1 1 0 0 0 1-1v-5 M5.5 8h3',
    {width: '1.2', cap: 'round'});

// A folder header. `folder.icon` may be a custom character; the lab allows one
// and it is user-supplied, so it goes in as text, never as markup.
function buildFolderHead(folder) {
  const row = document.createElement('div');
  row.className = folder.header === 'section' ? 'row frow sechdrow' : 'row frow';
  if (folder.closed) {
    row.classList.add('closedH');
  }
  if (folder.draggableHead) {
    row.draggable = true;
  }
  row.dataset.folderId = folder.id || '';

  const icon = document.createElement('span');
  icon.className = 'fic';
  if (folder.header === 'section') {
    // No folder glyph for a section - just the chevron, so it still reads as
    // the uppercase header it has always been.
    icon.appendChild(chevronGlyph());
    const label = document.createElement('span');
    label.className = 'tl';
    label.textContent = folder.name || '';
    row.append(icon, label);
    return row;
  }
  if (folder.icon) {
    const custom = document.createElement('span');
    custom.className = 'fic-g';
    custom.textContent = folder.icon;
    icon.appendChild(custom);
  } else {
    icon.appendChild(folderGlyph());
  }
  icon.appendChild(chevronGlyph());

  const label = document.createElement('span');
  label.className = 'tl';
  label.textContent = folder.name || '';

  row.append(icon, label);
  return row;
}

export function buildFolder(folder) {
  const wrap = document.createDocumentFragment();
  const head = buildFolderHead(folder);
  if (folder.dropzone) {
    // The body below collapses to zero height when closed. The head does
    // not, so it is the target a folded section can still be dropped on
    //.
    head.dataset.dropzone = folder.dropzone;
  }
  wrap.appendChild(head);

  const fold = document.createElement('div');
  fold.className = 'fold';
  if (folder.closed) {
    fold.classList.add('closed');
  }
  fold.dataset.folderBody = folder.id || '';

  const inner = document.createElement('div');
  inner.className = 'foldin';
  if (folder.indent) {
    inner.classList.add('indent');
  }
  if (folder.dropzone) {
    inner.dataset.dropzone = folder.dropzone;
  }
  for (const tab of folder.tabs || []) {
    inner.appendChild(buildRow(tab, {
      draggable: !!folder.draggableRows,
      pinned: folder.id === PINNED_FOLD_ID,
    }));
  }
  fold.appendChild(inner);
  wrap.appendChild(fold);
  return wrap;
}

// buildSpaceName already draws the pinned head, so only the fold body is
// built here. Items mix tabs and folders behind one item index (a folder
// counts as one), which buildFolder's flat tab loop cannot express.
function buildPinnedFold(snapshot, pinned) {
  const fold = document.createElement('div');
  fold.className = 'fold';
  if (snapshot.pinnedCollapsed) {
    fold.classList.add('closed');
  }
  fold.dataset.folderBody = PINNED_FOLD_ID;

  const inner = document.createElement('div');
  inner.className = 'foldin';
  inner.dataset.dropzone = 'pinned';
  for (const item of pinned) {
    inner.appendChild(item && item.kind === 'folder'
        ? buildFolder({
            id: item.id,
            name: item.name,
            closed: item.closed,
            icon: item.icon,
            tabs: item.tabs || [],
            dropzone: 'folder:' + item.id,
            draggableHead: true,
            draggableRows: true,
          })
        : buildRow(item, {draggable: true, pinned: true}));
  }
  fold.appendChild(inner);
  return fold;
}

// The row under the pointer, as the selector that finds it again once the list
// has been rebuilt. A `.hoverhold` node counts as hovered too, so a second push
// before the pointer moves keeps the hold.
export function hoveredRowKey(root) {
  const head = root.querySelector('.frow:hover, .frow.hoverhold');
  if (head) {
    return `.frow[data-folder-id="${CSS.escape(head.dataset.folderId)}"]`;
  }
  // A cold tab row carries no id, and an empty key would hand the
  // hold to whichever row also has none - so it gets no hold at all.
  const tabId =
      root.querySelector('.row.tabrow:hover, .row.tabrow.hoverhold')
          ?.dataset.tabId;
  return tabId ? `.row.tabrow[data-tab-id="${CSS.escape(tabId)}"]` : null;
}

// Chromium gives a node inserted under a still pointer :hover only on its next
// hover update, so a head rebuilt after a fold painted its glyph for a frame,
// and a tab row rebuilt by an arriving favicon lost its fill and close button.
export function carryHover(root, key) {
  if (!key) {
    return false;
  }
  const node = root.querySelector(key);
  node?.classList.add('hoverhold');
  return !!node;
}

export function releaseHover(root) {
  for (const head of root.querySelectorAll('.hoverhold')) {
    head.classList.remove('hoverhold');
  }
}

// The space switcher. The lab already builds this with DOM APIs; what is
// ported here is its shape, minus the drag-to-reorder and context menu, which
// need a data producer to act on.
// The dot stays outside `spaces` and carries no spaceIndex,
// so real space indices - which C++ reads back verbatim in
// SelectSpace - never shift.
// The lab's dwell between rail jumps while a row hovers the dots
// (swipe-lab.html:3607). Long enough that dragging ACROSS the bar does not
// stampede through every space on the way.
export const DOT_HOVER_THROTTLE_MS = 500;

// The bar's own side padding, so a revealed dot does not touch the edge.
const DOT_REVEAL_MARGIN_PX = 8;

// The one colour shape a theme stop is allowed to have. The result goes into
// style.background, so an unchecked stop from themes.json would be a CSS
// injection into the sidebar.
const THEME_STOP_HEX = /^#[0-9a-fA-F]{6}$/;

// What a space chip paints. Only two or more stops become a gradient: the
// studio opens every unthemed space on a one-stop theme, so treating that as a
// gradient would repaint the whole rail the moment anyone opened it.
function spaceDotFill(space) {
  const flat = space?.color || '';
  const stops = space?.theme?.stops;
  if (!Array.isArray(stops) || stops.length < 2) {
    return flat;
  }
  const colors = stops
      .map((stop) => (stop?.color || '').trim())
      .filter((color) => THEME_STOP_HEX.test(color));
  return colors.length === stops.length
      ? `linear-gradient(135deg, ${colors.join(', ')})`
      : flat;
}

// Graded against the dot's own space: a dot is inked only while that space's
// sidebar is under it. ringBackdrop holds whichever pane buildRail drew last.
function dotInkFor(space) {
  const rgb = parseRgb(space.color) || parseRgb(DEFAULT_TINT);
  return cssRgb(ringColorOn(rgb, ringGround(rgb)));
}

// A rebuild's pending correction (see buildSpaceDots below), cancelled if a
// later rebuild replaces the bar it targets before it fires.
let pendingDotBloomFrame = null;

export function buildSpaceDots(spaces, activeIndex, withLeadingOverview = false,
                               {onDotHover, onDotDrop} = {}) {
  let dotHoverLast = 0;
  // Which dot is being dragged, or null when the drag is a ROW from the list.
  // The two gestures land on the same handlers and mean different things.
  let dotDragFrom = null;
  const bar = document.createElement('div');
  bar.className = 'spacedots';

  let overviewDot = null;
  if (withLeadingOverview) {
    overviewDot = document.createElement('button');
    overviewDot.className = 'sdot ovdot';
    overviewDot.title = 'Overview';
    // Read by the magnifier's chip (sidebar-dot-magnify.js), which had
    // nothing to paint back after a hover and left the glyph blank.
    overviewDot.dataset.init = '::';
    const overviewGlyph = document.createElement('span');
    overviewGlyph.className = 'd';
    overviewGlyph.textContent = '::';
    overviewDot.appendChild(overviewGlyph);
  }

  // Only the per-space dots scroll: the Overview and + controls
  // below sit outside this element, so a swipe can never carry them off.
  const scroller = document.createElement('div');
  scroller.className = 'dotscroll';

  spaces.forEach((space, i) => {
    const dot = document.createElement('button');
    dot.className = 'sdot';
    if (i === activeIndex) {
      dot.classList.add('active');
    }
    if (space.overview) {
      dot.classList.add('ovdot');
    }
    dot.title = space.name || '';
    dot.dataset.spaceIndex = String(i);
    // Carried on the element so the bar can restyle itself mid-swipe without
    // the snapshot, the same two fields the lab keeps on its dots.
    dot.dataset.init = space.overview ? '::' : '';
    // Read back by the hover chip (sidebar-dot-magnify.js) and by
    // markActiveSpaceDot, both of which repaint this dot without the space
    // object in hand.
    if (!space.overview) {
      dot.dataset.spaceIcon = space.icon || '';
      dot.dataset.spaceName = space.name || '';
    }
    // The fill is the hover chip's (sidebar-dot-magnify.js). A dot at rest is
    // grey, and the active one is its bare glyph in the ink.
    dot.dataset.tint = space.overview ? '' : spaceDotFill(space);
    dot.dataset.ink = space.overview ? '' : dotInkFor(space);

    const d = document.createElement('span');
    d.className = 'd';
    if (space.overview) {
      d.textContent = '::';
    } else if (i === activeIndex) {
      d.appendChild(spaceGlyph(space.icon, space.name));
    }
    if (i === activeIndex) {
      d.style.color = dot.dataset.ink;
    }
    // drag a dot to reorder the spaces. The Overview dot carries
    // no data-space-index and is skipped by the drop resolution below, so it
    // stays pinned first the way the lab's does.
    dot.draggable = true;
    dot.addEventListener('dragstart', (e) => {
      dotDragFrom = i;
      e.dataTransfer?.setData?.('text/plain', String(i));
    });
    dot.addEventListener('dragover', (e) => {
      e.preventDefault();
      // a dragged ROW hovering a dot slides the rail there, so the
      // destination is on screen before the drop. Throttled at the lab's 500ms
      // (swipe-lab.html:3607) - without it every dragover frame would restart
      // the animation and the rail would never arrive.
      if (dotDragFrom !== null || !onDotHover) {
        return;
      }
      const now = Date.now();
      if (now - dotHoverLast < DOT_HOVER_THROTTLE_MS) {
        return;
      }
      dotHoverLast = now;
      onDotHover(i);
    });
    dot.addEventListener('drop', (e) => {
      e.preventDefault();
      const from = dotDragFrom;
      dotDragFrom = null;
      if (from === null) {
        // A ROW dropped on a dot moves that tab to the space.
        onDotDrop?.(i);
        return;
      }
      if (!Number.isInteger(from) || from === i) {
        return;
      }
      chrome.send('sidebarSpaceReordered', [from, i]);
    });
    dot.addEventListener('dragend', () => {
      dotDragFrom = null;
    });

    dot.appendChild(d);
    scroller.appendChild(dot);
  });
  // the "+" at the right edge, after every dot as the lab has it
  // (swipe-lab.html:3622-3627).
  const add = document.createElement('button');
  add.type = 'button';
  add.className = 'sdot addws';
  add.title = 'New workspace';
  add.dataset.sidebarNewSpace = '';
  // Same as the Overview dot above: the magnifier's chip reads
  // this back after a hover.
  add.dataset.init = '+';
  const plus = document.createElement('span');
  plus.className = 'd';
  plus.textContent = '+';
  add.appendChild(plus);

  if (overviewDot) {
    bar.appendChild(overviewDot);
  }
  bar.appendChild(scroller);
  bar.appendChild(add);

  placeDotBloom(bar, activeIndex, spaces);
  const refreshMagnify = installDotMagnify(bar);
  // The bar is still detached here, so offsetLeft read 0 - correct
  // it once the caller has attached it. Cancel a still-pending correction from
  // a bar a rebuild is replacing.
  if (pendingDotBloomFrame !== null) {
    cancelAnimationFrame(pendingDotBloomFrame);
  }
  pendingDotBloomFrame = requestAnimationFrame(() => {
    pendingDotBloomFrame = null;
    if (!bar.isConnected) {
      return;
    }
    // instant: true - this corrects the 0px guess written above before
    // layout existed, and that jump must not glide like a real switch does.
    placeDotBloom(bar, activeIndex, spaces, {instant: true});
    // A rebuilt bar starts at scrollLeft 0, and a glide from there would
    // replay on every snapshot push.
    revealActiveDot(bar, 'instant');
    // Same frame, same reason: the chip is derived from where the cursor is,
    // which needs the bar laid out.
    refreshMagnify();
  });
  return bar;
}

// Scrolls the bar alone. scrollIntoView would also scroll the rail and the
// sidebar around it.
function revealActiveDot(bar, behavior) {
  // Only the per-space dots scroll; Overview and + are always on
  // screen already, so the active dot found here is never one of them.
  const scroller = bar.querySelector('.dotscroll') || bar;
  const dot = scroller.querySelector('.sdot.active');
  const view = scroller.clientWidth;
  if (!dot || !view) {
    return;
  }
  const left = dot.offsetLeft - DOT_REVEAL_MARGIN_PX;
  const right = dot.offsetLeft + dot.offsetWidth + DOT_REVEAL_MARGIN_PX;
  let to;
  if (left < scroller.scrollLeft) {
    to = left;
  } else if (right > scroller.scrollLeft + view) {
    to = right - view;
  } else {
    return;
  }
  scroller.scrollTo({left: Math.max(0, to), behavior});
}

// The glow under the active dot, ported from the lab's placeDotBloom
// (swipe-lab.html:3640-3651). One element reused rather than rebuilt, so it
// glides between dots instead of jumping - which is the whole effect.
export function placeDotBloom(bar, activeIndex, spaces, {instant = false} = {}) {
  // Scoped to the scroller, so the bloom shares the per-space
  // dots' own coordinate space and glides with them instead of the fixed row.
  const scroller = bar.querySelector('.dotscroll') || bar;
  // A plain scan rather than an attribute selector: this runs against the DOM
  // shim in tests, whose selector parser handles classes and nothing more.
  const active = [...scroller.querySelectorAll('.sdot')].find(
      (dot) => Number(dot.dataset.spaceIndex) === activeIndex);
  const space = spaces[activeIndex];
  let bloom = scroller.querySelector('.dotbloom');
  if (!active || !space || space.overview) {
    if (bloom) {
      bloom.style.opacity = '0';
    }
    return null;
  }
  if (!bloom) {
    bloom = document.createElement('div');
    bloom.className = 'dotbloom';
    scroller.appendChild(bloom);
  }
  bloom.style.opacity = '';
  bloom.style.background =
      `radial-gradient(closest-side, ${space.color || DEFAULT_TINT}CC, transparent 75%)`;
  // instant skips the CSS glide for a correction the user never saw move -
  // only a real switch should animate.
  if (instant) {
    bloom.style.transition = 'none';
  }
  // Positioned off the dot's own offset rather than a measured rect: the shim
  // has no layout, and offsetLeft is what the surface can answer for either.
  bloom.style.setProperty(
      '--dbx', `${(active.offsetLeft ?? 0) + (active.offsetWidth ?? 0) / 2}px`);
  if (instant) {
    requestAnimationFrame(() => {
      bloom.style.transition = '';
    });
  }
  return bloom;
}

// Moves the active marker without a rebuild. The bar no longer rides with the
// panes, so a swipe cannot slide the right dot into place any more - the
// controller says which space it is heading for and this follows.
export function markActiveSpaceDot(root, activeIndex, spaces) {
  const bar = root.querySelector('.spacedots');
  if (!bar) {
    return;
  }
  for (const dot of bar.querySelectorAll('.sdot[data-space-index]')) {
    const active = Number(dot.dataset.spaceIndex) === activeIndex;
    dot.classList.toggle('active', active);
    const glyph = dot.querySelector('.d');
    if (!glyph) {
      continue;
    }
    const overview = dot.classList.contains('ovdot');
    // The initial only shows on the active chip, and an overview dot keeps its
    // glyph either way - buildSpaceDots draws the same two rules.
    // A hovered dot keeps the magnifier's chip. The magnifier repaints only
    // when the cursor moves to another dot.
    const mag = dot.classList.contains('mag');
    if (active || overview || mag) {
      glyph.replaceChildren(dot.dataset.spaceIcon === undefined
          ? document.createTextNode(dot.dataset.init || '')
          : spaceGlyph(dot.dataset.spaceIcon, dot.dataset.spaceName || ''));
    } else {
      glyph.replaceChildren();
    }
    glyph.style.background = mag ? (dot.dataset.tint || '') : '';
    glyph.style.color = active && !mag ? (dot.dataset.ink || '') : '';
  }
  // The standalone Overview dot has no data-space-index, so the loop above
  // never reaches it. A negative index means the rail is on the Overview pane
  //.
  const ovdot = bar.querySelector('.ovdot');
  ovdot?.classList.toggle('active', activeIndex < 0);
  const reduceMotion =
      globalThis.matchMedia?.('(prefers-reduced-motion: reduce)').matches;
  revealActiveDot(bar, reduceMotion ? 'instant' : 'smooth');
  // The bar is already attached by now, so this is the glide's real
  // measurement - buildSpaceDots only ever gets the pre-attach one.
  if (spaces) {
    placeDotBloom(bar, activeIndex, spaces);
  }
}

// `withSpaceDots` is false for a rail pane: the bar is one element beside the
// rail there, or every pane carries a copy and the swipe drags them all.

// `withUrlRow` is false for the same reason. The address row had
// the defect the dots were fixed for - one copy per pane, all of them scrubbing
// with the swipe - and apply() hoists it beside the rail.
export function renderSidebar(root, snapshot,
                              {withSpaceDots = true, withUrlRow = true,
                               favGrid = 'tiles', extActions = [],
                               extStrip = 'room'} = {}) {
  setIconScope(snapshot.iconScope);
  root.replaceChildren();
  const sidebar = document.createElement('div');
  sidebar.className = 'sidebar';
  setRingTokens(sidebar, snapshot.spaceTint);
  // The strip is absolute in the root AND in a pane, so a copy needs no room
  // held for it - only the lane above the rows, which this profile's own pin
  // count decides. A profile with none gives the space back to the tiles.
  const pinnedExts = (extActions || []).filter((a) => a.pinned);
  sidebar.classList.toggle('exs-lane', pinnedExts.length > 0);
  sidebar.style.setProperty('--exs-rows',
                            String(stripRows(pinnedExts.length)));
  // Tiles only where they differ from the pinned copy painted over them, the
  // same split buildFavRoom draws for the favourites.
  if (extStrip === 'tiles' && pinnedExts.length) {
    const copy = document.createElement('div');
    copy.className = 'exs-row';
    renderExtStrip(copy, {actions: pinnedExts, max: MAX_PINNED_TILES,
                          inert: true});
    sidebar.appendChild(copy);
  }

  // the ADDRESS BAR comes first, then the favourites. The lab's
  // sidebar is .sbchrome, then .urlbar, then the panes - so the bar sits
  // directly under the traffic lights (docs/lab/swipe-lab.html:1129), and the
  // favourites grid lives INSIDE a pane, above that pane's own .wshead.

  // Only when this render owns the bar. A rail pane does not - apply() puts one
  // beside the rail, and the grid deliberately stays behind.
  if (withUrlRow) {
    sidebar.appendChild(buildUrlRow(snapshot));
  }
  // Not incognito: it belongs to no space, so it has neither tiles nor a drop
  // target. A rail pane inside one profile gets the empty room instead - the
  // tiles are the pinned copy's, and this only holds their place.
  if (!snapshot.incognito) {
    sidebar.appendChild(favGrid === 'room'
                            ? buildFavRoom(snapshot.favorites || [])
                            : buildFavGrid(snapshot.favorites || []));
  }

  // Everything that scrolls lives in here - the favourites grid and the space
  // dots stay put around it. Ported from the lab's .scrollzone: before it,
  // `.rows` alone carried overflow-y with no flex:1/min-height:0, so a flex
  // item that cannot shrink below its content pushed the layout out instead of
  // scrolling, and its scrollbar spanning the pane read as the whole sidebar
  // scrolling.
  const scroll = document.createElement('div');
  scroll.className = 'scrollzone';
  // Everything inside the zone lives in ONE wrapper so the rubber band has a
  // single thing to move. The band must translate the CONTENT, not
  // the zone: the zone is the viewport, and sliding a viewport slides its clip
  // box with it, which slides the whole list over the space dots instead of
  // revealing space above it.
  const inner = document.createElement('div');
  inner.className = 'scrollinner';

  const pinned = snapshot.pinned || [];
  if (!snapshot.incognito) {
    // The workspace name is the pinned section's own fold header now (the
    // lab's wshead), drawn whether or not there is anything under it to fold -
    // an empty pinned list still needs the drop target buildSpaceName sets up.
    inner.appendChild(
        buildSpaceName(snapshot, !!pinned.length, !!snapshot.pinnedCollapsed));
    if (pinned.length) {
      inner.appendChild(buildPinnedFold(snapshot, pinned));
    }
  }
  if ((snapshot.folders || []).length) {
    // One zone around every head+body pair, so a folder head reorders among
    // its siblings the way sidebar-drag.js's other sections do -
    // each folder keeps its OWN dropzone too, for a tab joining the group.
    const folderList = document.createElement('div');
    folderList.className = 'folderlist';
    folderList.dataset.dropzone = 'folders';
    for (const folder of snapshot.folders) {
      // The folder body accepts drops: that is how a tab joins a group
      //. Membership was always persistable - SpaceTab.groupID
      // round-trips - but nothing offered a way to set it, so every folder a
      // user created stayed empty in both arms.
      folderList.appendChild(buildFolder({
        ...folder,
        dropzone: 'folder:' + folder.id,
        draggableHead: true,
        draggableRows: true,
      }));
    }
    inner.appendChild(folderList);
  }

  const tabs = snapshot.tabs || [];
  if (tabs.length) {
    inner.appendChild(buildSectionHeader({clearable: true}));
  }

  // "+ New Tab", above the regular rows and inside the scroll
  // zone - it scrolls away with the list rather than sitting under it.
  const newTab = document.createElement('button');
  newTab.className = 'newtab';
  const newTabIcon = document.createElement('span');
  newTabIcon.className = 'nti';
  newTabIcon.textContent = '+';
  // Decorative: the plus lived inside the label string before, which made the
  // button unalignable with a row's icon slot and had a screen reader read
  // "plus New Tab".
  newTabIcon.setAttribute('aria-hidden', 'true');
  const newTabLabel = document.createElement('span');
  newTabLabel.className = 'ntl';
  newTabLabel.textContent = 'New Tab';
  newTab.append(newTabIcon, newTabLabel);
  inner.appendChild(newTab);

  // ALWAYS, even with no rows. Guarding this on tabs.length meant the tab
  // list's dropzone did not exist at all when the list was empty, so nothing
  // could be dragged into it - and a space whose every tab is favourite-backed
  // renders empty, which is not rare. buildRows draws the
  // rest-state hint and the aimable height for that case.

  // Every drop here ends in a reorder, a pin or a move, all three naming a
  // space by index - so an incognito drag would file a private tab into the
  // space the document has active.
  inner.appendChild(snapshot.incognito
                        ? buildRows(tabs)
                        : buildRows(tabs, {draggable: true, dropzone: 'tabs'}));

  scroll.appendChild(inner);
  sidebar.appendChild(scroll);

  const spaces = snapshot.incognito ? [] : (snapshot.spaces || []);
  if (withSpaceDots && spaces.length) {
    sidebar.appendChild(
        buildSpaceDots(spaces, snapshot.activeSpace ?? 0, /*withLeadingOverview=*/ true));
  }

  root.appendChild(sidebar);
  return sidebar.querySelectorAll('.fav').length;
}

// the swipe rail. One pane per space laid side by side; swiping
// translates the rail. This is the fluidity test - the question the whole
// migration turns on - so swipe.js also records per-frame timings, because
// "feels smooth" is exactly what produced days of wasted SwiftUI work.
export function buildRail(snapshot) {
  const rail = document.createElement('div');
  rail.className = 'rail';
  // The model's active space, for diagnostics and tests. NOT a drag's source:
  // panes carry their own rows, so a dragged row's source and
  // its destination are both the pane it is drawn in (see sidebar-drag.js's
  // sourceSpace). Before that every pane drew the ACTIVE space's rows retinted,
  // and this was the only thing that could name where a row came from.
  rail.dataset.activeSpace = String(snapshot.activeSpace ?? 0);
  // Overview is the pane left of the first space, the way the
  // lab makes it SPACES[0] (swipe-lab.html:1235). It carries no spaceIndex, so
  // a drag started in it resolves to no space rather than to space 0.
  const overviewPane = document.createElement('div');
  overviewPane.className = 'railpane ovrailpane';
  renderOverview(overviewPane);
  rail.appendChild(overviewPane);
  const spaces = snapshot.spaces && snapshot.spaces.length
      ? snapshot.spaces
      : [{name: snapshot.spaceName || 'Space', color: snapshot.spaceTint,
          icon: snapshot.spaceIcon}];
  // The profile the pinned grid beside the rail holds. Compared per pane, as
  // swipe.js's markProfileSpan does: one flag for the whole rail calls home,
  // home, work crossed and redraws the pinned tiles in BOTH home panes.
  const pinnedProfile = spaces[snapshot.activeSpace ?? 0]?.profileId || '';
  spaces.forEach((space, i) => {
    const pane = document.createElement('div');
    pane.className = 'railpane';
    // Which space this pane is, so a drag can tell the section it started in
    // from the one it was dropped on after paging.
    pane.dataset.spaceIndex = String(i);
    // Which profile this pane belongs to, so the swipe can tell a move inside
    // one profile - where the tiles are identical and must not move - from a
    // move across profiles, where they differ and travel with their pane.
    pane.dataset.profileId = space.profileId || '';
    // Each pane renders ITS OWN space now. Every pane used to draw
    // the active space's rows retinted, so a swipe showed the outgoing space's
    // icons until a rebuild landed - the incoming space appeared only after the
    // animation had finished.
    //
    // The ACTIVE pane deliberately keeps reading the top-level fields: the fold
    // overlay and the live selection are applied there, and routing it through
    // the per-space copy would silently drop both.
    // `?? 0` to match buildSpaceDots and rail.dataset.activeSpace above, which
    // both default it. Comparing raw meant an omitted activeSpace matched NO
    // pane, so the top-level tabs, folders and favourites were dropped from
    // every one of them and the sidebar rendered empty rather than defaulting
    // to the first space. The Swift model always sends the field, so
    // this never bit a user - but the same field being defaulted in two places
    // and not the third is the kind of inconsistency that costs an afternoon
    // the first time something hand-builds a snapshot. Measured: omitting it
    // gave fold false / frows 0 / rows 0 against fold true / frows 1 with it.
    const own = i === (snapshot.activeSpace ?? 0) ? snapshot : space;
    renderSidebar(pane, {
      ...snapshot,
      // Its own profile's icon cache, which the landing then reuses as is.
      iconScope: space.iconScope || snapshot.iconScope,
      spaceName: space.name,
      spaceTint: space.color || snapshot.spaceTint,
      spaceIcon: space.icon,
      activeSpace: i,
      tabs: own.tabs || [],
      pinned: own.pinned || [],
      folders: own.folders || [],
      pinnedCollapsed: !!own.pinnedCollapsed,
      // The tiles too, so a swipe into another PROFILE carries the
      // outgoing grid off with its pane.
      favorites: own.favorites || [],
    }, {withSpaceDots: false, withUrlRow: false,
        favGrid: (space.profileId || '') === pinnedProfile ? 'room' : 'tiles',
        // The pins too, for the same reason and off the same comparison: a
        // swipe across profiles carries the outgoing strip off with its pane.
        extActions: pinnedExtensionActions(space.profileId || ''),
        extStrip: (space.profileId || '') === pinnedProfile ? 'room' : 'tiles'});
    rail.appendChild(pane);
  });
  // Each pane set its own. What builds after the rail draws the active profile.
  setIconScope(snapshot.iconScope);
  return rail;
}

// Frame timings across a transition. rAF fires once per composited frame, so
// the gaps ARE the frame intervals. Read by build/swipe-bench.mjs.
function recordFrameStats() {
  const marks = [];
  let last = performance.now();
  const tick = (now) => {
    marks.push(now - last);
    last = now;
    if (marks.length < 60) {
      requestAnimationFrame(tick);
      return;
    }
    const sorted = [...marks].sort((a, b) => a - b);
    window.__frameStats = {
      frames: marks.length,
      median: sorted[Math.floor(sorted.length / 2)],
      p95: sorted[Math.floor(sorted.length * 0.95)],
      worst: sorted[sorted.length - 1],
    };
  };
  window.__frameStats = null;
  requestAnimationFrame(tick);
}

// Folder open/closed state, held by the surface.
//
// The model owns it now: the snapshot carries `folder.closed` and a fold is
// persisted. This
// map is the optimistic overlay only - the snapshot is re-pushed on every tab
// title change, so without it a fold would spring back open before the
// agreeing push lands. It defers to the model once that push arrives.
export function createFoldState() {
  // id -> the state the USER put it in. A Map, not a Set: absence has to mean
  // "no opinion", and set-membership cannot tell that apart from "explicitly
  // opened". With a Set, opening a folder deleted its entry, and the next push
  // that had not yet caught up still said closed - so it snapped shut again
  //.
  const intent = new Map();
  return {
    // Overlays what the user did onto a freshly arrived snapshot.
    //
    // Optimistic only: the browser persists the fold now, so an intent that
    // AGREES with what just arrived has done its job and is dropped. Held
    // forever it would mask a fold changed in the SwiftUI arm or in another
    // window - this surface would never see it.
    applyTo(snapshot) {
      const next = {...snapshot};
      // Same optimistic rule as a folder, against the space-level flag.
      if (intent.get(PINNED_FOLD_ID) === !!snapshot.pinnedCollapsed) {
        intent.delete(PINNED_FOLD_ID);
      }
      if (intent.has(PINNED_FOLD_ID)) {
        next.pinnedCollapsed = intent.get(PINNED_FOLD_ID);
      }
      const folders = next.folders || [];
      if (!folders.length) {
        return next;
      }
      for (const folder of folders) {
        if (intent.get(folder.id) === !!folder.closed) {
          intent.delete(folder.id);
        }
      }
      return {
        ...next,
        folders: folders.map((folder) => ({
          ...folder,
          closed: intent.has(folder.id) ? intent.get(folder.id) : !!folder.closed,
        })),
      };
    },
    // `closed` is what the surface just moved TO. The caller reads it off the
    // live DOM rather than from here, because the rendered state is what the
    // user is reacting to - and for a folder they have never touched, that
    // comes from the model, which this map deliberately does not hold.
    record(id, closed) {
      intent.set(id, closed);
    },
    isClosed(id) {
      return intent.get(id) === true;
    },
    // A folder that no longer exists must not keep an intent alive forever -
    // the map would grow for the life of the window otherwise.
    prune(snapshot) {
      const live = new Set((snapshot.folders || []).map((f) => f.id));
      // The pinned section is not in `folders` and must not be pruned as if it
      // were a departed group.
      live.add(PINNED_FOLD_ID);
      for (const id of [...intent.keys()]) {
        if (!live.has(id)) {
          intent.delete(id);
        }
      }
    },
  };
}

// the write side has no message yet, so every entry here is
// disabled - the submenu shows which profile the space is on, not lets you
// change it.
function buildProfileSubmenuItems(spaces, activeIndex) {
  const current = spaces[activeIndex]?.profileId || '';
  const seen = new Set();
  const items = [];
  for (const space of spaces) {
    const id = space.profileId || '';
    if (!id || seen.has(id)) {
      continue;
    }
    seen.add(id);
    const name = space.profileName || id;
    items.push({label: (id === current ? '✓ ' : '  ') + name, disabled: true});
  }
  return items;
}

function overviewSectionItem(label, section, emit) {
  return {label,
          action: () => emit('sidebarOverviewSectionOpened', [section])};
}

// The empty-space menu, in the lab's full ordering.
export function buildEmptySpaceMenuItems(
    snapshot, {emit, openRewind, renameSpace, pickIcon} = {}) {
  const spaces = snapshot?.spaces || [];
  const activeIndex = snapshot?.activeSpace ?? 0;
  return [
    {label: 'Change Space Icon', action: () => pickIcon(activeIndex)},
    {label: 'Change Space Name',
     action: () => renameSpace(
         activeIndex,
         (name) => emit('sidebarSpaceRenamed', [activeIndex, name]))},
    {label: 'Change Theme',
     action: () => emit('sidebarSpaceThemeRequested', [activeIndex])},
    {label: 'Set Profile…', items: buildProfileSubmenuItems(spaces, activeIndex)},
    {},
    {label: 'New Tab', action: () => emit('sidebarNewTab', [])},
    {label: 'New Folder', action: () => emit('sidebarFolderCreated', [])},
    {label: 'New Workspace', action: () => emit('sidebarSpaceCreated', [])},
    {},
    overviewSectionItem('Downloads', 'downloads', emit),
    overviewSectionItem('Archived Tabs', 'archived', emit),
    overviewSectionItem('Snoozed Tabs', 'snoozed', emit),
    overviewSectionItem('Shelf', 'shelf', emit),
    overviewSectionItem('Notes', 'notes', emit),
    {label: 'Courtyard', action: () => emit('sidebarCourtyardOpened', [])},
    {},
    {label: 'Rewind', action: () => openRewind()},
    {label: 'Hide Sidebar', action: () => emit('sidebarToggleRequested', [])},
    {label: 'Settings', action: () => emit('openSettingsPane', ['spaces'])},
  ];
}

// A right-click on a space dot, which the lab opens the Theme Studio from
// (swipe-lab.html:4522). Addressed by the dot's own index, not the active
// space: any dot in the rail can be themed without switching to it first.
// The rect is the dot in this document's coordinates; the browser turns it
// into a screen anchor for the studio window.
export function buildSpaceDotMenuItems(spaceIndex, {emit, rect} = {}) {
  const anchor = rect ? {
    x: Math.round(rect.x), y: Math.round(rect.y),
    width: Math.round(rect.width), height: Math.round(rect.height),
  } : null;
  return [
    {label: 'Change Theme',
     action: () => emit('sidebarSpaceThemeRequested',
                        anchor ? [spaceIndex, anchor] : [spaceIndex])},
  ];
}

// Mirror View's submenu, ordered "as "+profile then Logged out
// (swipe-lab.html:4426-4428). Disabled with no submenu when mirrorChoices
// has nothing to offer.
export function buildMirrorMenuItem(snapshot, url, emit) {
  const choices = mirrorChoices(snapshot);
  if (!choices.length) {
    return {label: 'Mirror View…', disabled: true};
  }
  const items = [];
  let sawProfile = false;
  for (const choice of choices) {
    if (choice.profileId === LOGGED_OUT) {
      if (sawProfile) {
        items.push({});
      }
      items.push({label: 'Logged out',
        action: () => emit('sidebarTabMirrored', [url, choice.profileId])});
    } else {
      sawProfile = true;
      items.push({label: 'as ' + choice.label,
        action: () => emit('sidebarTabMirrored', [url, choice.profileId])});
    }
  }
  return {label: 'Mirror View…', items};
}

// The favourite menu, in the lab's full ordering. Add to Dock stays disabled - it
// resolves by tab id, which a favourite has none of.
export function buildFavoriteMenuItems(favCell, emit, snapshot) {
  const url = favCell.dataset.favId || '';
  return [
    {label: 'Copy Link', action: () => {
      navigator.clipboard?.writeText(url);
    }},
    {label: 'Share…', disabled: true},
    {label: 'Summarize Page', disabled: true},
    {},
    {label: 'Change Icon', disabled: true},
    {},
    {label: 'Add Split View', disabled: true},
    buildMirrorMenuItem(snapshot, url, emit),
    {},
    {label: 'Put on Shelf', action: () => {
      // No live tab to pull a title from - fall back to the
      // url rather than shelve a blank name.
      const url = favCell.dataset.favId || '';
      emit('sidebarFavoriteShelved', [url, favCell.title || url]);
    }},
    {label: 'Add to Dock', disabled: true},
    {},
    {label: 'Remove from Favorites', action: () => {
      const url = favCell.dataset.favId || '';
      if (url) {
        emit('sidebarFavoriteRemoved', [url]);
      }
    }},
  ];
}

export function installSidebar(root) {
  // Marks the root as this route's viewport, which is what clips the rail.
  root.classList.add('sbroot');

  // The PAGE must not scroll - only the tab list does. There are no html/body
  // rules anywhere in this tree, so both keep their default auto height and
  // body its 8px margin, while .sbroot is 100vh: the document ends up taller
  // than the viewport and the whole surface scrolls as one. The space name,
  // the favourites, the tabs and the dots all move together, which is what the
  // user saw and what no amount of fixing the INNER containers could address
  //.
  //
  // Set here rather than in sidebar.css because atrium.html loads that
  // stylesheet on every route, and height:100% on body would stop the quick
  // menu hugging its content - it is auto-resized from the document's own size.
  for (const el of [document.documentElement, document.body]) {
    el.style.height = '100%';
    el.style.margin = '0';
    el.style.padding = '0';
    el.style.overflow = 'hidden';
    el.style.overscrollBehavior = 'none';
  }
  installRowMenu(root, {
    emptyMenuItems: (emit) => buildEmptySpaceMenuItems(currentSnapshot, {
      emit,
      openRewind: () => openRewindBar(root, rewindMoments, () => currentSnapshot),
      // Every space's pane draws its own name row.
      renameSpace: (index, commit) => startInlineRename(
          root.querySelector(
              `.railpane[data-space-index="${index}"] .wsname .wsl`),
          commit),
      pickIcon: (index) => {
        const dot = root.querySelector(`.sdot[data-space-index="${index}"]`);
        if (!dot) {
          return;
        }
        const rect = dot.getBoundingClientRect();
        emit('sidebarSpaceIconPickerOpened', [
          index, currentSnapshot?.spaces?.[index]?.icon || '',
          Math.round(rect.left), Math.round(rect.top),
          Math.round(rect.width), Math.round(rect.height),
        ]);
      },
    }),
    favoriteMenuItems: (favCell, emit) =>
        buildFavoriteMenuItems(favCell, emit, currentSnapshot),
    spaceDotMenuItems: (dot, emit) =>
        buildSpaceDotMenuItems(Number(dot.dataset.spaceIndex),
                               {emit, rect: dot.getBoundingClientRect()}),
    mirrorMenuItem: (url, emit) => buildMirrorMenuItem(currentSnapshot, url, emit),
    spaces: () => currentSnapshot?.spaces || [],
    incognito: () => !!currentSnapshot?.incognito,
  });
  const shelfZone = installShelfDropZone(root, {
    send: (tabId) => chrome.send('sidebarTabShelved', [tabId]),
  });
  installStripExtensions(root);
  installSidebarMedia(root);
  // routes the six actions below through the post-action hint,
  // so installSidebarNavRow's own send goes through the wrapper too.
  const {send: hintSend} = installHintTriggers(document.body, {
    send: chrome.send.bind(chrome),
    clock: REAL_TIMEOUT_CLOCK,
  });
  window.atriumSetSidebarBindings = setBindings;
  installSidebarNavRow(
      root, {send: (name, args) => hintSend(name, args, {fromClick: true})});
  installOverlayResizer(root);
  installFavoriteDrag(root, {
    onReorder: (from, to) => chrome.send('sidebarFavoriteReordered', [from, to]),
  });
  const folds = createFoldState();

  // Every push is a candidate moment - captureMoment's own structural dedupe
  // decides which ones stick.
  let rewindMoments = [];
  // The live snapshot restorePayload checks a chosen scope's feasibility
  // against - not a moment's projection, which is already the past.
  let currentSnapshot = null;
  // The snapshot as the browser sent it, before the fold state was merged in.
  // A push that carries no tabs of its own - a split change - redraws through
  // this, so folds.applyTo runs once per render rather than on its own output.
  let lastRaw = null;
  // Where a second press on the Overview dot returns to.
  let lastSpacePane = OVERVIEW_PANES;
  let parkedOnOverview = false;
  // True between a rest on the Overview and the first frame that leaves it.
  // A drag decides nothing until the lift, so a narrow keyed to the decision
  // ran after the whole crossing, as the last thing on screen.
  let restedOnOverview = false;
  let overviewWidth = false;
  const setOverviewWidth = (wide) => {
    if (wide === overviewWidth) {
      return;
    }
    overviewWidth = wide;
    chrome.send('sidebarOverviewPaneActive', [wide]);
  };
  // Set by window.atriumSetNotificationTimeout. 'fiveSeconds' matches
  // settings.rs's default for a surface built before the first push.
  let notificationTimeoutStep = 'fiveSeconds';

  // The snapshot is re-pushed on every tab title change. Rebuilding the rail
  // mid-swipe would destroy the nodes the wheel events are being delivered to
  // and reset the tab list's scroll position, so a push that lands during a
  // gesture waits for it to finish (the lab's "render, NOT rebuild" lesson).
  let pending = null;
  // A fold now round-trips through the browser, and the push comes
  // back fast enough to land mid-collapse - which rebuilds the rail, so the
  // .26s animation never runs and the rebuilt DOM starts at its final
  // grid-template-rows. Same hazard the swipe already defers for, so it gets
  // the same treatment: hold the rebuild until the fold has settled.
  const FOLD_SETTLE_MS = 320;
  let foldSettlingUntil = 0;
  const foldSettling = () => Date.now() < foldSettlingUntil;
  let foldTimer = null;

  // A push that lands during the scrubber's drag is the same rebuild-under-a-
  // live-gesture hazard the fold and the swipe already guard - replaceChildren
  // would tear out the overlay's own listeners mid-drag, not just its DOM.
  const scrubbing = () => !!root.querySelector('.' + SCRUBBER_CLASS);

  // Shared by the header click and a drop landing in a folded section.
  // Mutates the live nodes - a rebuilt subtree starts at its final
  // grid-template-rows, so the .26s collapse never runs.
  function setFolded(id, nowClosed) {
    folds.record(id, nowClosed);
    foldSettlingUntil = Date.now() + FOLD_SETTLE_MS;
    for (const head of root.querySelectorAll(
        `.frow[data-folder-id="${CSS.escape(id)}"]`)) {
      head.classList.toggle('closedH', nowClosed);
    }
    for (const body of root.querySelectorAll(
        `.fold[data-folder-body="${CSS.escape(id)}"]`)) {
      body.classList.toggle('closed', nowClosed);
    }
    if (id === PINNED_FOLD_ID) {
      chrome.send('sidebarPinnedToggled', [nowClosed]);
    } else {
      chrome.send('sidebarFolderToggled', [id, nowClosed]);
    }
  }

  // A drop into a folded section opens it. Only when it is
  // actually closed - an open section must not send a spurious toggle.
  function unfoldIfClosed(id) {
    const body = root.querySelector(
        `.fold[data-folder-body="${CSS.escape(id)}"]`);
    if (body && body.classList.contains('closed')) {
      setFolded(id, false);
    }
  }

  // A push that lands mid-DRAG is fatal, not merely ugly. apply() rebuilds the
  // rail with replaceChildren, which destroys the very node the drag started
  // from - and a drag whose source element leaves the DOM is cancelled by the
  // browser. NOT what broke dragging before - that was .dragactive
  // reflowing the sidebar under the pointer, and this was ruled out as
  // the cause there. It is the same failure waiting on a slower
  // push, which is why it landed alongside the real fix.
  //
  // The swipe and the fold already defer pushes for their own reasons; a drag
  // needs the same treatment, and needs it more.
  let dragging = false;
  let dragIdleTimer = null;
  const endDrag = () => {
    if (dragIdleTimer !== null) {
      clearTimeout(dragIdleTimer);
      dragIdleTimer = null;
    }
    if (!dragging) {
      return;
    }
    dragging = false;
    if (pending) {
      const queued = pending;
      pending = null;
      apply(queued);
    }
  };
  // A drag released over another window delivers no dragend here, and the
  // latch then holds every push for the rest of the session. A live drag fires
  // dragover continuously, so this much silence means it has gone.
  const watchDrag = () => {
    if (dragIdleTimer !== null) {
      clearTimeout(dragIdleTimer);
    }
    dragIdleTimer = setTimeout(endDrag, DRAG_IDLE_MS);
  };
  root.addEventListener('dragstart', () => {
    dragging = true;
    watchDrag();
  }, true);
  root.addEventListener('dragover', () => {
    if (dragging) {
      watchDrag();
    }
  }, true);
  root.addEventListener('dragend', endDrag, true);
  root.addEventListener('drop', endDrag, true);

  // By the first move the browser's own :hover has caught up with the pointer.
  // The flag keeps the query off every move, only the first after a rebuild.
  let hoverHeld = false;
  const release = () => {
    if (hoverHeld) {
      releaseHover(root);
      hoverHeld = false;
    }
  };
  root.addEventListener('pointermove', release);
  root.addEventListener('pointerleave', release);
  const swipe = createSwipeController(root, {
    onIdle: () => {
      // At rest, not at the decision: a gesture the spring pulled back leaves
      // the last frame's fraction on the bar, and snapping it when the target
      // was decided dropped the scale a frame before the rail moved.
      applyOverviewFade(root, parkedOnOverview ? 1 : 0);
      // A drag or the scrubber still holds the snapshot, and drains it on end.
      if (pending && !dragging && !scrubbing()) {
        const snap = pending;
        pending = null;
        apply(snap);
      }
    },
    // Dots count spaces, the controller counts rail positions. Overview lands
    // at -1, which marks no space dot and lights the Overview one instead.
    onActiveIndex: (index) => {
      if (index >= OVERVIEW_PANES) {
        lastSpacePane = index;
      }
      // Overview is not a space, so the browser's activeSpace never moves for
      // it and the next snapshot push would pull the rail back to the space
      // behind it. This is what attach() reads instead.
      const parked = index < OVERVIEW_PANES;
      // When the gesture decides, both ways: first movement widens on a
      // wobble, idle narrows long after the content landed.
      setOverviewWidth(parked);
      parkedOnOverview = parked;
      markActiveSpaceDot(root, index - OVERVIEW_PANES, currentSnapshot?.spaces);
      markActiveSpaceUrl(root, currentSnapshot, index - OVERVIEW_PANES);
    },
    onRailPaint: (at) => {
      const share = overviewShare(at);
      if (restedOnOverview && share < 1) {
        restedOnOverview = false;
        setOverviewWidth(false);
      }
      applyOverviewFade(root, share);
    },
    leadingPanes: OVERVIEW_PANES,
    // Reported from the SETTLE, not from buildRail. Sending it at build time
    // marked a pane that exists rather than one the rail reached, which reads
    // green on a swipe that landed on a space.
    onLeadingPane: () => {
      // A drag that turned back re-widens here, after narrowing on the way out.
      restedOnOverview = true;
      setOverviewWidth(true);
      chrome.send('sidebarOverviewPaneRendered', []);
    },
  });

  const rowDrag = installRowDrag(root, {
    // a folder head reorders among folders the same way a tab row
    // does among tabs, so it joins the set of rows this module hit-tests.
    rowSelector: '.tabrow, .frow',
    // A third pane has no tab-id cache or focus subscription, so the browser
    // refuses an edge drop while split.
    splitActive: () => !!splitState?.active,
    // A url for a title shows as the host, as on a row with no title.
    rowLook: (tile) => buildRow({
      url: tile.dataset.favId,
      title: tile.title === tile.dataset.favId ? '' : tile.title,
    }),
    tileLook: (row) => buildFav({
      id: row.dataset.url,
      title: row.querySelector('.tl')?.textContent,
    }),
    onDrop: (drop) => {
      // One message per kind rather than one message with a mode flag: an
      // unregistered chrome.send name is a FATAL browser crash, so each name
      // is worth being able to grep for on its own.
      switch (drop.kind) {
        case 'reorder':
          chrome.send('sidebarTabReordered', [drop.from, drop.to]);
          break;
        case 'reorderPinned':
          chrome.send('sidebarPinnedReordered', [drop.from, drop.to]);
          break;
        case 'reorderFolder':
          chrome.send('sidebarFolderReordered', [drop.from, drop.to]);
          break;
        case 'pin':
          unfoldIfClosed(PINNED_FOLD_ID);
          chrome.send('sidebarTabPinned', [drop.from, drop.to]);
          break;
        case 'unpin':
          chrome.send('sidebarTabUnpinned', [drop.from, drop.to]);
          break;
        case 'favorite':
          // The space argument keeps its place and the slot follows it. A
          // favourite belongs to the profile, so the space it landed on says
          // nothing about where in the grid it goes. -1 appends.
          chrome.send('sidebarRowFavorited',
                      [drop.from, drop.fromSection, -1,
                       Number.isInteger(drop.to) ? drop.to : -1]);
          break;
        case 'favoriteToFolder':
          unfoldIfClosed(drop.groupID);
          chrome.send('sidebarFavoriteGrouped',
                      [drop.url, drop.groupID, drop.memberSlot]);
          break;
        case 'unfavorite':
          if (drop.toSection === 'pinned') {
            unfoldIfClosed(PINNED_FOLD_ID);
          }
          chrome.send('sidebarFavoriteMovedOut',
                      [drop.url, drop.toSection, drop.to]);
          break;
        case 'group':
          unfoldIfClosed(drop.groupID);
          chrome.send('sidebarTabGrouped',
                      [drop.from, drop.groupID, drop.section,
                       drop.memberSlot !== undefined ? drop.memberSlot : -1]);
          break;
        case 'ungroup':
          if (drop.toSection === 'pinned') {
            unfoldIfClosed(PINNED_FOLD_ID);
          }
          chrome.send('sidebarTabUngrouped',
                      [drop.groupID, drop.member, drop.toSection, drop.to]);
          break;
        case 'split':
          chrome.send('sidebarTabSplit',
                      [drop.draggedTabId, drop.targetTabId, drop.side]);
          break;
        case 'reorderMember':
          chrome.send('sidebarFolderMemberReordered',
                      [drop.groupID, drop.from, drop.to]);
          break;
        case 'pinFolder':
          unfoldIfClosed(PINNED_FOLD_ID);
          chrome.send('sidebarFolderPinned', [drop.groupID, drop.to]);
          break;
        case 'moveToSpace':
          // Both spaces travel with it. Paging has already switched the
          // browser's active space by the time this lands, so a source index
          // alone would be resolved against the wrong space.
          chrome.send('sidebarTabMovedToSpace',
                      [drop.fromSpace, drop.from, drop.fromSection, drop.toSpace,
                       drop.to]);
          break;
        case 'moveRun':
          chrome.send('sidebarRunMoved',
                      [drop.fromSpace, drop.slots, drop.toSpace, drop.to]);
          break;
      }
    },
    // Holding a dragged row at either edge pages to the neighbouring space,
    // through the same controller a swipe drives - so the rail moves the one
    // way it knows how, rather than gaining a second animation path.
    onEdgePage: (direction) => {
      // Off the destination, not the origin: a dwell pages repeatedly, and
      // spaceIndex does not move until the rail arrives, so reading it walked
      // back and forth over one boundary instead of on through the rail.
      const next = swipe.targetIndex() + (direction === 'next' ? 1 : -1);
      if (next >= 0 && next < swipe.spaceCount()) {
        swipe.jumpTo(next);
      }
    },
    // a release outside the sidebar that never resolved to a drop
    // kind. Named for what happened (a release), not what it means - the native side
    // still has to classify the screen point against every other Atrium window
    // before anything is torn off, moved, or split.
    onRelease: (tabId, screenX, screenY) => {
      chrome.send('sidebarTabDragReleased', [tabId, screenX, screenY]);
    },
    // the live edge highlight in whichever other window the
    // pointer is over. The end of the gesture sends the tab id alone, which
    // the browser reads as "clear", so a cancelled drag leaves nothing up.
    onDragMove: (tabId, screenX, screenY) => {
      chrome.send('sidebarTabDragMoved',
                  screenX === null ? [''] : [tabId, screenX, screenY]);
    },
  });
  // the address bar grip's drag, which the browser brackets.
  window.atriumBeginExternalTabDrag = (tabId) => rowDrag.beginExternal(tabId);
  window.atriumEndExternalTabDrag = () => rowDrag.endExternal();
  // The space icon picker widget calls this on close, so the dot that opened
  // it gets focus back without the scroll-triggered dismiss a plain .focus()
  // would cause on the sidebar's own scroll listener.
  window.atriumFocusSpaceDot = (index) => {
    root.querySelector(`.sdot[data-space-index="${index}"]`)
        ?.focus({preventScroll: true});
  };

  function apply(raw) {
    lastRaw = raw;
    // A run log cannot tell a push that was PARKED in `pending` from one that
    // redrew, and the two look identical on screen when the stale frame is
    // still plausible. This is the only signal that separates them.
    window.__atriumApplyCount = (window.__atriumApplyCount || 0) + 1;
    // A push while the scrubber is open must not leave it running against a
    // ring/index that has since moved.
    closeRewindBar(root);
    folds.prune(raw);
    const snap = folds.applyTo(raw);
    // Before pinnedFavGrid, which reads it: a rail push builds that grid
    // ahead of buildRail's own renderSidebar calls, the only other setter.
    const scopeChanged = setIconScope(snap.iconScope);
    // A regular profile's loaded node is already in this document, so reusing
    // it leaks nothing. An off-the-record profile's must never cross.
    const carryIcons = !scopeChanged ||
        (!currentSnapshot?.incognito && !snap.incognito);
    // Where the rows are NOW, while the outgoing DOM is still laid out. Every
    // pin and unpin arrives here as a snapshot, so one glide here covers both
    // the chord and the drag drop.
    const slots = crossesPinnedLine(currentSnapshot, snap)
        ? captureRowSlots(root)
        : null;
    const heldHover = hoveredRowKey(root);
    currentSnapshot = snap;
    rewindMoments = captureMoment(rewindMoments, snap, Date.now());
    // Every scroll zone in the outgoing DOM loses its band before that DOM
    // goes: a rebuilt sidebar would otherwise leave a listener holding a
    // detached element, and a rail has one zone per pane.
    dropBands();
    // More than one space means the rail; one space renders directly, so the
    // common case costs nothing extra.
    // >= 1, not > 1: the Overview pane makes a second pane on its own, so even
    // a single space now has somewhere to swipe.
    // The flag as well as the length, so "an incognito surface never rails"
    // holds whatever a producer puts in `spaces` - rather than only while the
    // browser side remembers to send none.
    harvestLoadedIcons(carryIcons ? root : null);
    if (!snap.incognito && snap.spaces && snap.spaces.length >= 1) {
      // Before the rail is built, which reads the strip per pane, and in step
      // with the grid pinnedFavGrid stamps below - the two ride as one block.
      setActiveExtensionProfile(activeProfileOf(snap));
      // The bar sits beside the rail, not in it, so the swipe cannot carry it
      // off. It is absolutely positioned, so the panes reserve room
      // for it in CSS rather than in the flow.
      root.replaceChildren(
          // The bar before the rail, the order the lab's sidebar has
          // (swipe-lab.html:1129). It is absolutely positioned too, so the
          // panes reserve room for it in CSS rather than in the flow.
          buildUrlRow(snap),
          // Beside the rail, so a swipe cannot carry the profile's tiles off.
          // Stamped with the profile it was drawn FOR: until a rebuild lands
          // it still holds the outgoing one's, which must not be shown.
          pinnedFavGrid(snap),
          buildRail(snap),
          buildSpaceDots(snap.spaces, snap.activeSpace ?? 0,
                         /*withLeadingOverview=*/ true, {
                           onDotHover: (index) =>
                               swipe.jumpTo(index + OVERVIEW_PANES),
                           onDotDrop: (index) => {
                             const src = rowDrag.dragSource();
                             if (!src || src.space === index) {
                               return;
                             }
                             // dropDecision's own cross-space source check, so
                             // a favourite or folder never moves a real tab.
                             const decision = dropDecision(
                                 {from: src, to: {section: 'tabs', slot: -1,
                                                   space: index}});
                             if (decision?.kind === 'moveRun') {
                               chrome.send('sidebarRunMoved',
                                           [decision.fromSpace, decision.slots,
                                            decision.toSpace, decision.to]);
                               return;
                             }
                             if (!decision || decision.kind !== 'moveToSpace') {
                               return;
                             }
                             // Appended, not inserted: a dot names a space, not
                             // a slot, so -1 lets the browser side place it.
                             chrome.send(
                                 'sidebarTabMovedToSpace',
                                 [decision.fromSpace, decision.from,
                                  decision.fromSection, decision.toSpace,
                                  decision.to]);
                           },
                         }));
      // +1 for the Overview pane at rail position 0.
      swipe.attach(
          snap.spaces.length + OVERVIEW_PANES,
          parkedOnOverview ? 0 : (snap.activeSpace ?? 0) + OVERVIEW_PANES);
      // The bar above was just replaced, so it comes back at full opacity even
      // while the rail is parked on the Overview. Not mid-glide:
      // a 0 here dropped the scale a frame before the grid's compensation did.
      if (!swipe.isGliding()) {
        applyOverviewFade(root, parkedOnOverview ? 1 : 0);
      }
    } else {
      swipe.detach();
      // No rail, so no other profile to compare against: the lane follows
      // whatever the strip itself is showing.
      renderSidebar(root, snap, {extActions: pinnedExtensionActions()});
    }
    // Nodes not adopted belong to the tree that just went.
    carriedIcons = new Map();
    hoverHeld = carryHover(root, heldHover);
    playRowFlip(root, slots);
    addBands();
    // The grid a chord's slot resolves against, not every tile in the tree: a
    // rail that crosses profiles legitimately draws another profile's tiles in
    // its own pane, and those are not slots the active space can open.
    const shownGrid =
        [...root.children].find((el) => el.classList.contains('favgrid')) ||
        root;
    chrome.send('sidebarFavoriteCount',
                [shownGrid.querySelectorAll('.fav').length]);
    // A tab activation reaches this surface as a snapshot naming a different
    // selected tab, and that is the focus change the ring follows.
    // Before reapplyLoadingMarks, which restores the rings this leaves standing.
    setActiveKeys(activeRingKeys(snap), root);
    // a run log cannot tell a drawn sidebar from an empty one, and
    // a capture that waited on a settle caught the empty one five runs in six.
    reapplyLoadingMarks(root);
    // the whole sidebar fades in on its first render, as one piece.
    root.classList.add('drawn');
    chrome.send('sidebarRowsDrawn', [root.querySelectorAll('.row').length]);
  }

  // The vertical rubber band, one per scroll zone. Installed after each render
  // and torn down before the next, because the zones are rebuilt rather than
  // reused.
  let bands = [];
  function dropBands() {
    for (const off of bands) {
      off();
    }
    bands = [];
  }
  function addBands() {
    for (const zone of root.querySelectorAll('.scrollzone')) {
      bands.push(installOverscroll(zone));
    }
  }

  // the last snapshot this surface actually rendered, so an
  // identical one can be dropped instead of rebuilding the tree.
  //
  // The host pushes on many independent signals - a tab-strip change, a title
  // arriving, a per-tab favicon, a resolved favourite icon - and several of them
  // carry a model that has not moved. Measured on a three-favourite scenario:
  // FIVE pushes in one launch, every one of them the same 715 bytes. Each was
  // replacing every row and tile.
  //
  // Comparing the serialised model is sound here because selection and the
  // discarded flags are merged into it on the C++ side before it is sent, so two
  // identical models mean an identical surface. A cheaper key - a length, a
  // count - would collide on exactly the edits that matter, like a row's title
  // changing without its url.

  window.atriumSetSidebar = (snapshot) => {
    const snap = snapshot || {};
    // the redundant-render skip that used to live here is gone. It
    // returned before swipe.attach(), which is the ONLY thing that clears the
    // swipe machine's awaitingAck latch - the latch that holds the rail on the
    // space a gesture just chose until a snapshot agrees. Skip the render and
    // the acknowledgement never arrives, so a swipe could be undone by the next
    // push that did render. A push costing a rebuild is cheaper than a gesture
    // that does not stick.
    // A glide the browser asked for has no finger on the old nodes, and the
    // routed tab's row must not wait the ~0.9s it takes to settle.
    const swiping = swipe.isBusy() && !swipe.isGliding();
    if (dragging || scrubbing() || swiping || foldSettling()) {
      pending = snap;
      // The swipe's own idle callback drains `pending`, and a fold has no loop
      // so it schedules its own drain. The scrubber drains itself, on close.
      if (!dragging && !scrubbing() && !swiping && foldTimer === null) {
        foldTimer = setTimeout(() => {
          foldTimer = null;
          // Whatever still holds the snapshot drains it when it lets go.
          const held = dragging || scrubbing() ||
              (swipe.isBusy() && !swipe.isGliding());
          if (pending && !held) {
            const queued = pending;
            pending = null;
            apply(queued);
          }
        }, Math.max(0, foldSettlingUntil - Date.now()) + 20);
      }
      return;
    }
    apply(snap);
  };

  // C++'s reply to a Back hold. onClose mirrors endDrag's own
  // drain, since scrubbing() is what held `pending` back in the first place.
  window.atriumSetVisitTimeline = (timeline) => {
    openHistoryScrubber(root, timeline, {
      onClose: () => {
        if (pending) {
          const queued = pending;
          pending = null;
          apply(queued);
        }
      },
    });
  };

  // Dev instrumentation only - read-only, see createSwipeController's debug().
  window.__swipeDebug = () => swipe.debug();

  // the argument is a SPACE, because the only production caller is
  // GlideSidebarToSpace and the browser has no concept of a leading pane. The
  // rail counts panes, so the offset is added here rather than at the call - a
  // space passed straight through glides one space BACK, undoing the swipe that
  // asked for it.
  window.atriumSwipeTo = (space) => {
    recordFrameStats();
    return 'swiping to ' + swipe.jumpTo(space + OVERVIEW_PANES);
  };

  // Overview is a pane, not a space, so atriumSwipeTo's space-to-
  // pane offset cannot name it. The hotkeys and the leading dot come in here.
  window.atriumSwipeToOverview = () => {
    recordFrameStats();
    return 'swiping to ' + swipe.jumpTo(0);
  };

  // dispatched by ShelveSidebarTab once the row is really on the
  // shelf, so a failed shelve shows nothing rather than a toast that lies.
  // dispatched by RestoreSidebarSlice only after the restore
  // really happened, so a refused one shows nothing.
  window.atriumRewindRestored = () => {
    rewindRestored({host: root});
  };

  window.atriumShelfToast = (label) => {
    if (!shouldShowToast(notificationTimeoutStep)) {
      return;
    }
    showToast(
        // The lab's shelf accent (swipe-lab.html:1942), carried here rather
        // than read from a token: the shelf is the only surface that uses it.
        {color: '#B58ED0', title: 'Added to Shelf', sub: label,
         onClick: () => chrome.send('sidebarShelfToastClicked', [])},
        {host: root, dismissAfterMs: dismissAfterMsForTimeout(notificationTimeoutStep)});
  };

  // The brief confirmations - a copy, a screenshot, their failure. A native
  // status bubble drew these, so they read every colour from C++ constants and
  // followed neither the tint nor native chrome mode.
  window.atriumConfirmToast = (title, sub, ok) => {
    showConfirmToast({title, sub, ok: ok !== false}, {host: root});
  };

  window.atriumSetNotificationTimeout = (step) => {
    notificationTimeoutStep = step;
  };

  // settings.rs's own field names, mapped to swipe.js's DEFAULT_TUNING
  // spelling. No entry for blendFactor/holdSilenceGap - settings.rs carries
  // neither.
  const SWIPE_TUNING_FROM_SETTINGS = {
    swipeCommitThreshold: 'commitThreshold',
    swipeSlowCommit: 'slowCommit',
    swipeOvershootCap: 'overshootCap',
    swipeSettleDur: 'settleDur',
    swipeSpringDamp: 'springDamp',
    swipeFollowTau: 'followTau',
  };
  window.atriumSetSwipeTuning = (fromSettings) => {
    const tuning = {};
    for (const [settingsKey, tuningKey] of
             Object.entries(SWIPE_TUNING_FROM_SETTINGS)) {
      if (typeof fromSettings[settingsKey] === 'number') {
        tuning[tuningKey] = fromSettings[settingsKey];
      }
    }
    swipe.setTuning(tuning);
    if (typeof fromSettings.swipeHaptics === 'string') {
      swipe.setHaptics(fromSettings.swipeHaptics);
    }
  };

  // the wake event itself is not built yet - this is
  // only the entrypoint it will call through.
  window.atriumTabSnoozeReturned = (tab, opts) => {
    handleSnoozedTabReturn(tab, {notificationTimeoutStep, ...opts, host: root});
  };

  // the sweep's own caller is out of scope here - this is only
  // the entrypoint it will call through, same seam as atriumTabSnoozeReturned.
  window.atriumTabsArchived = (ids, opts) => {
    const rows = (ids || [])
        .map((id) => root.querySelector(`[data-tab-id="${id}"]`))
        .filter(Boolean);
    flyRowsToOverview(rows, {...opts, host: root});
  };

  // stored so a consumer (and this file's own tests) can read the
  // current state without re-deriving it.
  let splitState = null;
  window.atriumSplitChanged = (state) => {
    splitState = state || null;
    // paneTabIDs carries every live pane; the primary/secondary
    // pair only ever names the first two.
    const members = splitState?.active
        ? splitState.paneTabIDs ||
            [splitState.primaryTabID, splitState.secondaryTabID]
        : [];
    setSplitMembers(members);
    // Through atriumSetSidebar, not apply: the drag, swipe and fold deferrals
    // that path holds apply to this redraw too.
    if (lastRaw) {
      window.atriumSetSidebar(lastRaw);
    }
  };
  window.__splitState = () => splitState;

  // which edge the shelf zone hugs - the same one the sidebar
  // itself docks on. The root class is the same fact for CSS;
  // the floating overlay sets it too, and only while it floats.
  window.atriumSetSidebarOnRight = (onRight) => {
    document.documentElement.classList.toggle(
        'atrium-sidebar-on-right', !!onRight);
    shelfZone.setOnRight(onRight);
  };

  // pushed on every committed navigation of the active tab, so the
  // field's title/aria-label tracks redirects rather than only the load a
  // snapshot happened to arrive with.
  // The browser's own loading state for one tab, from DidStartLoading to
  // DidStopLoading. Keyed on the tab id, so the row and the tile both run.
  window.atriumTabLoading = (tabId, loading) => {
    setLoadingKey(String(tabId), root, !!loading);
  };

  // The two phase fill, pushed by AtriumBrowserWindow::PushLoadPhase. A stale
  // tab's waiting/receiving is dropped, but `done` always goes through, or
  // that tab's fill stays lit.
  window.atriumLoadPhase = (tabId, phase, fraction) => {
    const activeId = currentSnapshot ? findActiveTab(currentSnapshot)?.id : null;
    if (phase !== 'done' && String(tabId) !== String(activeId ?? '')) {
      return;
    }
    lastLoadPhase = {phase, fraction};
    fieldFill?.setPhase(phase, fraction);
    setLoadingPhase(String(tabId), root, phase, fraction);
  };

  window.atriumSetSidebarUrl = (origin) => {
    const bar = root.querySelector('#sbUrl');
    if (!bar) {
      return;
    }
    showUrlBarOrigin(bar, origin);
  };

  // '' when no panel is open. The live rows are patched here and
  // the id is kept for buildRow, so a rebuild between two pushes does not
  // drop the mark.
  window.atriumSetAiPanelRow = (tabId) => {
    aiPanelRowTabId = tabId ? String(tabId) : '';
    for (const row of root.querySelectorAll('.tabrow')) {
      row.classList.toggle(
          'aimark', !!aiPanelRowTabId && row.dataset.tabId === aiPanelRowTabId);
    }
  };

  // '' when no peek is open, or the peek did not start from a favourite.
  window.atriumSetPeekBadge = (tabId, iconId, rev) => {
    peekBadge = tabId && iconId
        ? {tabId: String(tabId), iconId: String(iconId), rev: rev || 0}
        : null;
    for (const cell of root.querySelectorAll('.fav')) {
      syncPeekBadge(cell);
    }
  };

  // CommandBarWidget::Close() calls this on every close path, so
  // the row a click hid comes back even if the palette never animated.
  window.atriumSetAddressRowHidden = (hidden) => {
    root.querySelector('#sbUrl')?.classList.toggle('addr-hidden', !!hidden);
  };

  // A Cmd+T open has no tab yet, so the selection sits on the New Tab
  // button until the bar closes. On the root, which a snapshot push keeps.
  window.atriumSetCommandBarNewTabPending = (pending) => {
    root.classList.toggle('cb-newtab-pending', !!pending);
  };

  // the two address bars are never both visible. `ownShown` (dev
  // mode / localhost) alone hides #sbUrl - `shown` also covers a
  // split-forced bar, which must not, and defaults to `ownShown`.
  window.atriumSetDevBarShown = (ownShown, shown = ownShown) => {
    root.classList.toggle('devbar-shown', !!ownShown);
    // The pinned-extension strip is the root's sibling, out of reach of a
    // root-scoped rule, so its hide gate reads the class from html.
    document.documentElement.classList.toggle('devbar-shown', !!shown);
    // the same push moves back/forward/reload, which the floating
    // bar hosts while it is up.
    setNavGroupInRow(navRowForRoot(root), !shown);
  };

  // A click opens the palette on this row: the window converts the box to
  // window coordinates and anchors the card on it.
  root.addEventListener('click', (e) => {
    const bar = e.target.closest?.('#sbUrl');
    if (!bar) {
      return;
    }
    const box = bar.getBoundingClientRect();
    bar.classList.add('addr-hidden');
    chrome.send('sidebarAddressRowClicked',
                [Math.round(box.left), Math.round(box.top),
                 Math.round(box.width), Math.round(box.height)]);
  });

  root.addEventListener('click', (e) => {
    if (e.target.closest('.x')) {
      const row = e.target.closest('.tabrow');
      if (row) {
        // A pane's close button closes that pane only, so the rest of the
        // split stays open.
        const seg = e.target.closest('.sseg');
        if (seg) {
          chrome.send('sidebarTabClosed', [seg.dataset.tabId]);
          return;
        }
        // Option-click morphs the close button into the snooze menu. Only
        // the regular section resolves an index snooze_tab can use.
        const zone = row.parentElement;
        if (e.altKey && zone?.dataset?.dropzone === 'tabs') {
          const index = slotOf(zone, row);
          if (index >= 0) {
            const bounds = root.getBoundingClientRect();
            openSnoozeMenu(root, e.clientX - bounds.left, e.clientY - bounds.top,
                           {index, emit: (name, args) => chrome.send(name, args)});
          }
          return;
        }
        // Stage two of the pinned close: a pinned row with
        // nothing loaded has no tab id to close, so the x takes the tile
        // itself. Its index is its position in the pinned section.
        if (!row.dataset.tabId && zone?.dataset?.dropzone === 'pinned') {
          const index = rowsIn(zone).indexOf(row);
          if (index >= 0) {
            chrome.send('sidebarPinnedTabRemoved', [index]);
          }
          return;
        }
        // A cold regular row has the same hole. A row live in
        // another window has no id here either, but its tab is open.
        if (!row.dataset.tabId && !row.dataset.elsewhereTabId &&
            row.dataset.url && zone?.dataset?.dropzone === 'tabs') {
          const index = slotOf(zone, row);
          if (index >= 0) {
            chrome.send('sidebarColdTabRemoved', [index]);
          }
          return;
        }
        chrome.send('sidebarTabClosed', [row.dataset.tabId]);
      }
      return;
    }
    const row = e.target.closest('.tabrow');
    if (row) {
      // A click inside a split row names the pane it landed in, and falls back
      // to the row's own id on the row's padding.
      const target = e.target.closest('.sseg') || row;
      if (target.dataset.tabId) {
        // The click is the focus change, taken here rather than waiting for
        // the snapshot: the ring on the row being left has to stop at once.
        setActiveKeys([target.dataset.tabId, target.dataset.url], root,
                      {claimed: true});
        // No ring: this row has a live tab, so the click activates it and
        // loads nothing. One that does load on activation, a hibernated tab,
        // rings from the browser's own start signal.
        chrome.send('sidebarTabClicked', [target.dataset.tabId]);
      } else if (row.dataset.elsewhereTabId) {
        // live, but in another window on this profile. Sending its
        // id takes the page HERE - the host resolves it across windows, freezes
        // the pane it left, and makes it active in this one. Checked before the
        // cold-row branch below: this row has a url too, and reopening it would
        // be the second copy the whole task exists to stop.
        // Keyed on url, not the element: the resolve rebuilds this row.
        setActiveKeys([row.dataset.url], root, {claimed: true});
        markLoadingKey(row.dataset.url, root);
        chrome.send('sidebarTabClicked', [row.dataset.elsewhereTabId]);
      } else if (row.dataset.url &&
                 (row.parentElement?.dataset?.dropzone === 'tabs' ||
                  row.parentElement?.dataset?.dropzone === 'pinned')) {
        // A COLD row: the tab is persisted but has no live counterpart, so
        // there is no id to activate. Reopening its URL is what makes it a row
        // rather than a decoration - the tab was discarded, or the session was
        // restored without it.
        //
        // Pinned rows included. They were once excluded because
        // the reopened tab came back as an ordinary one and quietly lost its
        // pinning. It does not any more: open_tab's adoptable_row searches
        // pinned_tabs BEFORE tabs, so reopening adopts the pinned row it came
        // from and the pin survives. Excluding them left a pinned tab that had
        // never loaded completely inert - a click did nothing at all.
        //
        // A folder's rows stay out. Their grouping would survive adoption the
        // same way, but nobody has asked for it, and widening this quietly is
        // how the pinned rule came to cover a case it no longer needed to.
        //
        // the reopen has no id yet, so the click marks the row by
        // its url, the only key it has - like a cold favourite (below).
        setActiveKeys([row.dataset.url], root, {claimed: true});
        markLoadingKey(row.dataset.url, root);
        chrome.send('sidebarColdTabClicked', [row.dataset.url]);
      }
      return;
    }
    // A click into a name being renamed places the caret, not a fold.
    if (e.target.isContentEditable) {
      return;
    }
    const folder = e.target.closest('.frow');
    if (folder) {
      const id = folder.dataset.folderId;
      // The live DOM is the current state, not the intent map: a folder the
      // user has never touched is closed or open because the MODEL said so,
      // and toggling from the map would ignore that.
      const body = root.querySelector(
          `.fold[data-folder-body="${CSS.escape(id)}"]`);
      const nowClosed = !(body && body.classList.contains('closed'));
      // The state it moved TO, not just which folder: C++ persists this
      // explicitly rather than toggling, so a double-click cannot land the
      // surface and the model on opposite values.
      setFolded(id, nowClosed);
      return;
    }
    if (e.target.closest('[data-sidebar-clear]')) {
      // The rows fly to the Overview dot before they go, the same flourish an
      // archive from any other entry point uses.
      flyRowsToOverview(
          [...root.querySelectorAll('[data-dropzone="tabs"] .tabrow')],
          {host: root});
      chrome.send('sidebarTabsCleared', []);
      return;
    }
    if (e.target.closest('.newtab')) {
      hintSend('sidebarNewTab', [], {fromClick: true});
      return;
    }
    if (e.target.closest('[data-sidebar-new-space]')) {
      hintSend('sidebarSpaceCreated', [], {fromClick: true});
      return;
    }
    // A fast press can land mid-glide of a neighbour's bloom shift and miss
    // its painted box; dotAt falls back to the layout position.
    const spacedots = e.target.closest('.spacedots');
    const dot = e.target.closest('.sdot') ||
        (spacedots && dotAt(spacedots, e.clientX));
    if (dot) {
      if (dot.classList.contains('ovdot')) {
        // Slides to the pane rather than opening the old widget. A second
        // press returns to the space it came from.
        swipe.jumpTo(swipe.targetIndex() === 0 ? lastSpacePane : 0);
        return;
      }
      // The controller announces sidebarSpaceSelected when the slide lands, so
      // a dot press and a swipe report a switch the same way.
      swipe.jumpTo(Number(dot.dataset.spaceIndex) + OVERVIEW_PANES);
      return;
    }
    const cell = e.target.closest('.fav');
    if (cell) {
      // The tab id goes along because the URL alone cannot find the page again:
      // a favourite that redirects on load commits a different URL, the lookup
      // missed it, and every click opened another tab.
      setActiveKeys([cell.dataset.favTab, cell.dataset.favId], root,
                    {claimed: true});
      // Only a COLD tile loads anything. One bound to a live tab activates it,
      // and the ring would sit for its whole timeout with nothing to clear it.
      if (!cell.dataset.favTab) {
        markLoadingKey(cell.dataset.favId, root);
      }
      chrome.send('sidebarFavoriteClicked',
                  [cell.dataset.favId, cell.dataset.favTab || '']);
    }
  });

  // double-click a row to rename it, as the lab does. The
  // index is the row's position in its own section, which is what the core
  // addresses by.
  root.addEventListener('dblclick', (e) => {
    const row = e.target.closest('.tabrow');
    if (!row) {
      return;
    }
    const zone = row.parentElement;
    const section = zone?.dataset?.dropzone;
    if (section !== 'tabs' && section !== 'pinned') {
      return;
    }
    // A split segment renames its own tab, not the row's first one.
    const seg = e.target.closest('.sseg');
    const index = slotOf(zone, seg?.dataset?.slot ? seg : row);
    if (index < 0) {
      return;
    }
    startRowRename(seg || row, (name) => {
      chrome.send('sidebarTabRenamed', [index, section === 'pinned' ? 1 : 0, name]);
    });
  });

  // tell the browser when a field in here takes or loses the
  // keyboard. The hotkey gate reads the ACTIVE TAB's focus live and cannot see
  // this surface, because the sidebar is its own WebContents - so without this
  // a chord macOS treats as text editing fires its hotkey while the user is
  // renaming a tab, and the edit never happens.
  //
  // focusin and focusout, not focus and blur: those two do not bubble, so a
  // field added later by a re-render would never be heard.
  //
  // The state moved TO is reported, never a toggle. A missed event would
  // otherwise strand the flag at true, and a stranded true blocks every gated
  // hotkey for the rest of the session.
  // Read off the tag rather than with instanceof: a constructor check is
  // identity-based, so it answers false for the same element seen from another
  // realm, and it needs those globals to exist to be testable at all.
  const editable = target => {
    if (!target || !target.tagName) {
      return false;
    }
    const tag = target.tagName.toUpperCase();
    return ((tag === 'INPUT' || tag === 'TEXTAREA') && !target.readOnly) ||
        target.isContentEditable === true;
  };
  let textFocused = false;
  const reportTextFocus = next => {
    if (next === textFocused) {
      return;
    }
    textFocused = next;
    chrome.send('sidebarTextFocusChanged', [next]);
  };
  root.addEventListener('focusin', e => reportTextFocus(editable(e.target)));
  root.addEventListener('focusout', e => {
    // relatedTarget is where the keyboard is GOING. Moving between two fields
    // fires focusout before focusin, so reading it here stops a false "nothing
    // is focused" in the gap.
    reportTextFocus(editable(e.relatedTarget));
  });

  chrome.send('sidebarReady', []);
}
