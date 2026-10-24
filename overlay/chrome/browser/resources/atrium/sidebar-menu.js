// Copyright 2026 The Atrium Authors
//
// The tab row context menu.

import {startRowRename} from './row-rename.js';
import {refreshMenuState, registerMenu} from './menu-dismiss.js';
import {bindingFor} from './hint-triggers.js';
import {keycaps} from './keycaps.js';

// A zone's items in core order: a folder counts as one, so `.tabrow` alone
// skips every folder head and misaligns the index every click, rename and
// menu action behind it sends to C++.
export function rowsIn(zone) {
  return zone ? [...zone.children].filter((el) => el.matches('.tabrow, .frow')) : [];
}

// How many slots a zone holds. A split row stands for one slot per pane.
export function slotCount(zone) {
  return rowsIn(zone).reduce(
      (n, row) => n + Math.max(1, row.querySelectorAll('.sseg').length), 0);
}

// The slot C++ knows this row by. A split draws several tabs as one row, so
// DOM position stops counting slots there and buildRows stamps the real one.
export function slotOf(zone, row) {
  const stamped = row?.dataset?.slot;
  return stamped === undefined || stamped === '' ? rowsIn(zone).indexOf(row)
                                                 : Number(stamped);
}
//
// Right-clicking a row used to fall through to Chromium's own page menu -
// Back/Forward/Reload, all greyed out, because they describe the sidebar's own
// WebContents rather than the tab under the cursor. That left pinning
// unreachable in this arm entirely.
//
// Most actions here reuse the message its matching drag path already sends.
// Add to Dock is the one exception, with its own message and
// ABI entry - a pinned tab has no drag path to reuse.
//
// Built with createElement and textContent throughout. Trusted Types is on with
// an empty allowlist, so innerHTML is not merely discouraged here -
// it throws.
//
// No row carries a keycap: the bindings map pushed to the sidebar only names
// native hotkey ids, and none of this menu's actions has one.

export const MENU_CLASS = 'ctxmenu';

// spaces.rs's SwiftDate counts seconds since 2001-01-01, not the Unix epoch
// `Date.now()` uses - same constant archived-snoozed-section.js converts back
// with.
const SWIFT_TO_UNIX_EPOCH_OFFSET_S = 978307200;

// Computed in JS, not the Rust core - a wall-clock preset needs the
// browser's own local time zone, which the core has no notion of.
export const SNOOZE_DURATIONS = [
  {key: 'inOneHour', label: 'In 1 hour'},
  {key: 'thisEvening', label: 'This evening'},
  {key: 'tomorrowMorning', label: 'Tomorrow morning'},
  {key: 'nextMonday', label: 'Next Monday'},
];

// `nowMs` is a parameter, not `Date.now()`, so a test can pin the clock
// instead of racing real time across midnight or a Monday.
export function presetWakeAtSwiftSeconds(key, nowMs) {
  const now = new Date(nowMs);
  let target;
  switch (key) {
    case 'inOneHour':
      target = new Date(nowMs + 3600 * 1000);
      break;
    case 'thisEvening': {
      target = new Date(now);
      target.setHours(18, 0, 0, 0);
      if (target.getTime() <= nowMs) {
        target.setDate(target.getDate() + 1);
      }
      break;
    }
    case 'tomorrowMorning':
      target = new Date(now);
      target.setDate(target.getDate() + 1);
      target.setHours(8, 0, 0, 0);
      break;
    case 'nextMonday': {
      target = new Date(now);
      // Monday is weekday 1. `+ 6` before `% 7` turns "0 days away" (today
      // is Monday) into 7, so "next" Monday is never today.
      const daysAhead = ((1 - target.getDay() + 6) % 7) + 1;
      target.setDate(target.getDate() + daysAhead);
      target.setHours(9, 0, 0, 0);
      break;
    }
    default:
      target = new Date(nowMs + 3600 * 1000);
  }
  return target.getTime() / 1000 - SWIFT_TO_UNIX_EPOCH_OFFSET_S;
}

// The alert checkbox only affects what the NEXT duration pick sends - picking
// a duration both commits the snooze and closes the menu.
export function openSnoozeMenu(root, x, y, {index, emit, now = Date.now()} = {}) {
  closeMenu(root);
  const menu = document.createElement('div');
  menu.className = MENU_CLASS + ' ov-card snoozemenu';

  const alertRow = document.createElement('label');
  alertRow.className = 'ctxitem ov-row snoozealert';
  const alertBox = document.createElement('input');
  alertBox.type = 'checkbox';
  alertBox.checked = false;
  alertRow.appendChild(alertBox);
  alertRow.appendChild(document.createTextNode('Alert me'));
  menu.appendChild(alertRow);

  const rule = document.createElement('div');
  rule.className = 'ctxrule';
  menu.appendChild(rule);

  for (const {key, label} of SNOOZE_DURATIONS) {
    const button = document.createElement('button');
    button.className = 'ctxitem ov-row';
    button.type = 'button';
    button.textContent = label;
    button.addEventListener('click', () => {
      const wakeAt = presetWakeAtSwiftSeconds(key, now);
      closeMenu(root);
      emit('sidebarTabSnoozed', [index, wakeAt, alertBox.checked]);
    });
    menu.appendChild(button);
  }

  root.appendChild(menu);
  place(menu, x, y, root);
  menu.querySelector('.ctxitem')?.focus();
  refreshMenuState();
  return menu;
}

/** Removes any open menu. Safe to call when none is open. */
export function closeMenu(root) {
  for (const open of root.querySelectorAll('.' + MENU_CLASS)) {
    open.remove();
  }
  refreshMenuState();
}

/**
 * Positions the menu inside the surface.
 *
 * The sidebar is narrow and short, so a menu opened near an edge would spill
 * out and be clipped - there is no window to overflow into, unlike a page
 * context menu.
 */
export function place(menu, x, y, root) {
  const bounds = root.getBoundingClientRect();
  const size = menu.getBoundingClientRect();
  const left = Math.max(2, Math.min(x, bounds.width - size.width - 2));
  const top = Math.max(2, Math.min(y, bounds.height - size.height - 2));
  menu.style.left = `${left}px`;
  menu.style.top = `${top}px`;
}

// Opens a menu at (x, y). No label is a separator, `disabled: true` greys an
// item out, `items` instead of `action` opens a nested menu in place.
function openMenu(root, x, y, items) {
  closeMenu(root);
  const menu = document.createElement('div');
  menu.className = MENU_CLASS + ' ov-card';
  for (const item of items) {
    if (!item.label) {
      const rule = document.createElement('div');
      rule.className = 'ctxrule';
      menu.appendChild(rule);
      continue;
    }
    const button = document.createElement('button');
    button.className = 'ctxitem ov-row';
    button.type = 'button';
    button.textContent = item.label;
    const binding = bindingFor(item.actionId);
    if (binding) {
      const chip = keycaps(binding);
      chip.classList.add('ctxkey');
      button.appendChild(chip);
    }
    if (item.disabled) {
      button.disabled = true;
      button.classList.add('ctxitem-disabled');
    } else {
      button.addEventListener('click', () => {
        if (item.items) {
          openMenu(root, x, y, item.items);
        } else {
          closeMenu(root);
          item.action();
        }
      });
    }
    menu.appendChild(button);
  }
  root.appendChild(menu);
  place(menu, x, y, root);
  // Focus the first ENABLED item, so Escape/arrow keys and a keyboard-opened
  // menu have somewhere to land. Filtered in JS: dom-shim has no :not().
  [...menu.querySelectorAll('.ctxitem')].find((el) => !el.disabled)?.focus();
  refreshMenuState();
  return menu;
}

// the sidebar has no icon picker yet, so the folder and space
// "Change Icon" items cycle this fixed palette. An empty entry clears back to
// the default glyph.
const ICON_PALETTE = ['🗂️', '⭐', '📌', '💡', '🎯', ''];

export function nextIcon(current) {
  const at = ICON_PALETTE.indexOf(current || '');
  return ICON_PALETTE[(at + 1) % ICON_PALETTE.length];
}

function nextFolderIcon(folderHead) {
  return nextIcon(folderHead.querySelector('.fic-g')?.textContent);
}

function startFolderRename(folderHead, id, emit) {
  startInlineRename(folderHead.querySelector('.tl'),
                    (next) => emit('sidebarFolderRenamed', [id, next]));
}

/** Edits `label` in place until Enter, Escape or blur ends it. */
export function startInlineRename(label, commit) {
  if (!label) {
    return;
  }
  const original = label.textContent;
  // `finish` runs from three different triggers (Enter, Escape, blur) and a
  // real blur fires again after label.blur() below - the guard keeps that
  // from sending the rename twice.
  let done = false;
  const finish = (commitChange) => {
    if (done) {
      return;
    }
    done = true;
    label.contentEditable = 'false';
    label.removeEventListener('blur', onBlur);
    label.removeEventListener('keydown', onKey);
    const next = label.textContent.trim();
    if (commitChange && next && next !== original) {
      commit(next);
    } else {
      label.textContent = original;
    }
  };
  const onKey = (event) => {
    if (event.key === 'Enter') {
      event.preventDefault();
      finish(true);
      label.blur();
    } else if (event.key === 'Escape') {
      finish(false);
      label.blur();
    }
  };
  const onBlur = () => finish(true);
  label.contentEditable = 'true';
  label.focus();
  label.addEventListener('blur', onBlur);
  label.addEventListener('keydown', onKey);
}

let unregisterRowMenu = null;

// Wires the row context menu under `root`. `emptyMenuItems(emit)` builds the
// empty-space menu, `favoriteMenuItems(favCell, emit)` the favourite-tile one.
export function installRowMenu(
    root,
    {send, emptyMenuItems, favoriteMenuItems, spaceDotMenuItems, mirrorMenuItem,
     spaces, incognito} = {}) {
  const emit = send || ((name, args) => chrome.send(name, args));

  // One row menu per document, so a re-install releases the previous root -
  // without this the registry would hold every detached one for the session.
  unregisterRowMenu?.();
  unregisterRowMenu = registerMenu({
    close: () => closeMenu(root),
    isOpen: () => !!root.querySelector('.' + MENU_CLASS),
  });

  const openAt = (event, items) => {
    if (!items || !items.length) {
      return false;
    }
    event.preventDefault();
    const bounds = root.getBoundingClientRect();
    openMenu(root, event.clientX - bounds.left, event.clientY - bounds.top,
             items);
    return true;
  };

  root.addEventListener('contextmenu', (event) => {
    // Before the empty-space fallback below, which is what a dot used to fall
    // through to - it offers the ACTIVE space's actions, not this dot's.
    const dot = event.target.closest?.('.sdot');
    if (dot && dot.dataset.spaceIndex && spaceDotMenuItems) {
      openAt(event, spaceDotMenuItems(dot, emit));
      return;
    }

    const favCell = event.target.closest?.('.fav');
    if (favCell && favoriteMenuItems) {
      const items = favoriteMenuItems(favCell, emit);
      if (items && items.length) {
        event.preventDefault();
        const bounds = root.getBoundingClientRect();
        openMenu(root, event.clientX - bounds.left, event.clientY - bounds.top, items);
      }
      return;
    }

    // Before the folder branch below: the space name row folds the pinned
    // section, so it wears .frow and would otherwise be offered Rename and
    // Delete Folder against PINNED_FOLD_ID, which names no group in the core.
    const spaceName = event.target.closest?.('.wsname');
    if (spaceName && emptyMenuItems) {
      const items = emptyMenuItems(emit);
      if (items && items.length) {
        event.preventDefault();
        const bounds = root.getBoundingClientRect();
        openMenu(root, event.clientX - bounds.left, event.clientY - bounds.top, items);
      }
      return;
    }

    const folderHead = event.target.closest?.('.frow');
    // The pinned section folds through the same markup but is not a group -
    // it has no id these messages could act on.
    if (folderHead && !folderHead.classList.contains('sechdrow')) {
      const id = folderHead.dataset.folderId;
      if (!id) {
        return;
      }
      event.preventDefault();
      const bounds = root.getBoundingClientRect();
      const items = [
        {label: 'Rename Folder',
         action: () => startFolderRename(folderHead, id, emit)},
        {label: 'Change Icon',
         action: () => emit('sidebarFolderIconSet', [id, nextFolderIcon(folderHead)])},
        {},
        {label: 'Delete Folder', action: () => emit('sidebarFolderDeleted', [id])},
      ];
      openMenu(root, event.clientX - bounds.left, event.clientY - bounds.top, items);
      return;
    }

    const row = event.target.closest?.('.tabrow');
    // A split row stands for several tabs, so the pane under the cursor is
    // what these entries act on. Plain rows have no segment and answer row.
    const target = event.target.closest?.('.sseg') || row;
    const zone = row?.parentElement;
    const section = zone?.dataset?.dropzone;
    // Only rows this menu can actually act on. A grouped row lives in a folder
    // body, whose section is `folder:<id>`, and none of these messages address
    // one - so it keeps the page menu rather than getting a menu of no-ops.
    if (!row || (section !== 'tabs' && section !== 'pinned')) {
      if (!row && emptyMenuItems) {
        const items = emptyMenuItems(emit);
        if (items && items.length) {
          event.preventDefault();
          const bounds = root.getBoundingClientRect();
          openMenu(root, event.clientX - bounds.left, event.clientY - bounds.top, items);
        }
      }
      return;
    }
    // Suppress Chromium's page menu, which describes the sidebar's own
    // WebContents and not the tab under the cursor.
    event.preventDefault();

    // A segment of a split row carries its own slot, so the menu acts on the
    // pane under the cursor rather than on the row's first tab.
    const index = slotOf(zone, target.dataset?.slot ? target : row);
    if (index < 0) {
      return;
    }
    const bounds = root.getBoundingClientRect();
    // Mirror View sits in its own separator group, where a Split
    // entry would go once one exists here (docs/lab/swipe-lab.html:4463-4473).
    // Omitted, not disabled, when the caller supplies no mirrorMenuItem.
    const mirrorGroup = mirrorMenuItem
        ? [{}, mirrorMenuItem(target.dataset.url || '', emit)]
        : [];
    // The lab opens every row menu with Copy Link (lab 4443). The favourite
    // menu has had it; a row menu without it is an
    // inconsistency inside one surface, not just a gap against the lab.
    const copyLink = target.dataset.url
        ? [{label: 'Copy Link',
            action: () => navigator.clipboard?.writeText(target.dataset.url)},
           {}]
        : [];
    // the lab's Rename entry, opening the same inline field a
    // double-click does - one path, so the two cannot diverge.
    const rename = {
      label: 'Rename',
      action: () => startRowRename(row, (name) => emit(
          'sidebarTabRenamed', [index, section === 'pinned' ? 1 : 0, name])),
    };
    const rowCount = slotCount(zone);
    // The rail gives each pane a data-space-index; a single-space sidebar has
    // no pane and is space 0.
    const fromSpace =
        Number(row.closest('.railpane')?.dataset.spaceIndex ?? 0) || 0;
    // The lab's Duplicate, Move to..., Archive Tab and Archive
    // Tabs Below (swipe-lab.html:4487-4501). Share, Summarize, Change Icon,
    // Mute and Notes Beside are in the lab too and have nothing behind them
    // here, so they are absent rather than dead.
    const duplicate = {
      label: 'Duplicate',
      action: () => emit('sidebarTabDuplicated', [target.dataset.tabId]),
    };
    const spaceList = spaces?.() || [];
    const moveTo = spaceList.length > 1
        ? [{label: 'Move to...',
            action: () => openMenu(
                root, event.clientX - bounds.left, event.clientY - bounds.top,
                spaceList
                    .map((space, to) => ({space, to}))
                    .filter(({to}) => to !== fromSpace)
                    .map(({space, to}) => ({
                           label: space.name || '',
                           // -1: a menu entry names a space, not a position in
                           // it, so the core appends.
                           action: () => emit('sidebarTabMovedToSpace',
                                              [fromSpace, index, section, to, -1]),
                         })))}]
        : [];
    // Both archive by position in the REGULAR list, so neither is offered on a
    // pinned row below - the same index there names an unrelated tab.
    const archive = [
      {label: 'Archive Tab', action: () => emit('sidebarTabArchived', [index])},
      ...(index < rowCount - 1
              ? [{label: 'Archive Tabs Below',
                  action: () => emit('sidebarTabsArchivedBelow', [index])}]
              : []),
    ];

    // Everything else on this menu files the row somewhere that outlives the
    // window - a space, the shelf, the archive, the dock, another profile.
    // Copy, Duplicate and Close are the three that stay inside it.
    if (incognito?.()) {
      openMenu(root, event.clientX - bounds.left, event.clientY - bounds.top,
               [...copyLink, duplicate, {},
                {label: 'Close Tab',
                 action: () => emit('sidebarTabClosed', [target.dataset.tabId])}]);
      return;
    }

    const items = section === 'pinned'
        ? [...copyLink,
           rename,
           {label: 'Unpin Tab',
            actionId: 'pinToggleTab',
            // Append to the regular list: the row's position among pinned
            // tabs says nothing about where it belongs once unpinned.
            action: () => emit('sidebarTabUnpinned',
                               [index, slotCount(root.querySelector('[data-dropzone="tabs"]'))])},
           {label: 'Add to Dock', action: () => emit('sidebarTabAddedToDock', [index])},
           ...mirrorGroup,
           {},
           duplicate,
           ...moveTo,
           {},
           // shelving never touches the tab, so it needs no
           // index math - the row's own id is enough, same as Close Tab below.
           {label: 'Put on Shelf', action: () => emit('sidebarTabShelved', [target.dataset.tabId])},
           {},
           {label: 'Close Tab', action: () => emit('sidebarTabClosed', [target.dataset.tabId])}]
        : [...copyLink,
           rename,
           {label: 'Pin Tab',
            actionId: 'pinToggleTab',
            // Slot 0 in the pinned list, matching where a drag to the top of
            // that section lands.
            action: () => emit('sidebarTabPinned', [index, 0])},
           {label: 'Snooze',
            action: () => openSnoozeMenu(
                root, event.clientX - bounds.left, event.clientY - bounds.top,
                {index, emit})},
           ...mirrorGroup,
           {},
           duplicate,
           ...moveTo,
           {},
           {label: 'Put on Shelf', action: () => emit('sidebarTabShelved', [target.dataset.tabId])},
           ...archive,
           {},
           // A cold row has no id for sidebarTabClosed to name, so it goes by
           // its rendered index, like the x does.
           {label: 'Close Tab',
            action: () => !target.dataset.tabId && !target.dataset.elsewhereTabId
                ? emit('sidebarColdTabRemoved', [index])
                : emit('sidebarTabClosed', [target.dataset.tabId])}];

    openMenu(root, event.clientX - bounds.left, event.clientY - bounds.top, items);
  });

  // Escape and a scroll dismiss it too, the ways a native menu goes away.
  // The press listens on the document, not `root`: the nav row is root's
  // previous sibling, inside the sidebar rect the native monitor excludes.
  document.addEventListener('pointerdown', (event) => {
    if (!event.target.closest?.('.' + MENU_CLASS)) {
      closeMenu(root);
    }
  }, true);
  root.addEventListener('keydown', (event) => {
    if (event.key === 'Escape') {
      closeMenu(root);
    }
  });
  root.addEventListener('scroll', () => closeMenu(root), true);
}
