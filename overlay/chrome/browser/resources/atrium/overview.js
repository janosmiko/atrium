// Copyright 2026 The Atrium Authors
// Section CONTENTS are not built yet. Spaces still renders a
// placeholder here.

import {renderArchivedSection, renderSnoozedSection} from
    './archived-snoozed-section.js';
import {renderDownloadsSection} from './downloads-section.js';
import {renderNotesSection} from './notes-section.js';
import {renderShelfSection} from './shelf-section.js';
import {renderSpacesSection} from './spaces-section.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

// Path/rect data copied from swipe-lab.html's `ic` object (~line 2996).
const ICONS = {
  downloads: [{tag: 'path', d: 'M8 2v8m0 0l-3-3m3 3l3-3M3 13h10'}],
  spaces: [
    {tag: 'rect', x: 2, y: 2, width: 5, height: 5, rx: 1},
    {tag: 'rect', x: 9, y: 2, width: 5, height: 5, rx: 1},
    {tag: 'rect', x: 2, y: 9, width: 5, height: 5, rx: 1},
    {tag: 'rect', x: 9, y: 9, width: 5, height: 5, rx: 1},
  ],
  archived: [{tag: 'path', d: 'M2 5h12M3 5l1 8h8l1-8M6 8h4'}],
  snoozed: [{
    tag: 'path',
    d: 'M13.7 8.5A5.7 5.7 0 1 1 7.5 2.3a4.5 4.5 0 0 0 6.2 6.2z',
  }],
  shelf: [{
    tag: 'path',
    d: 'M2 9.8L4.2 4h7.6L14 9.8M2 9.8V12a1 1 0 0 0 1 1h10a1 1 0 0 0 1-1V9.8' +
        'M2 9.8h3l1.5 2h3l1.5-2h3',
  }],
  notes: [{tag: 'path', d: 'M11.2 2.6l2.2 2.2L6 12.2l-3 .8.8-3zM10 3.8l2.2 2.2'}],
};

const SECTIONS = [
  {id: 'downloads', label: 'Downloads'},
  {id: 'spaces', label: 'Spaces'},
  {id: 'archived', label: 'Archived'},
  {id: 'snoozed', label: 'Snoozed'},
  {id: 'shelf', label: 'Shelf'},
  {id: 'notes', label: 'Notes'},
];

// Module-level, like the lab's own `ovSection` global: a surface rebuild
// re-runs renderOverview, and switching spaces and back must not reset the
// section back to Downloads.
let ovSection = 'downloads';

function buildIcon(sectionId) {
  const svg = document.createElementNS(SVG_NS, 'svg');
  svg.setAttribute('viewBox', '0 0 16 16');
  for (const part of ICONS[sectionId]) {
    const el = document.createElementNS(SVG_NS, part.tag);
    for (const [attr, value] of Object.entries(part)) {
      if (attr === 'tag') {
        continue;
      }
      el.setAttribute(attr, String(value));
    }
    svg.appendChild(el);
  }
  return svg;
}

function buildPlaceholder(section) {
  const placeholder = document.createElement('div');
  placeholder.className = 'ovplaceholder';
  placeholder.textContent =
      `${section.label} - content lands in a follow-up task`;
  return placeholder;
}

export function renderOverview(root) {
  root.replaceChildren();

  const wrap = document.createElement('div');
  wrap.className = 'ovwrap';

  const rail = document.createElement('div');
  rail.className = 'ovrail';

  const pane = document.createElement('div');
  pane.className = 'ovpane';

  // The top strip carries only the traffic lights over this pane, so the
  // section names itself here rather than in an address bar.
  const head = document.createElement('div');
  head.className = 'ovhead';
  const main = document.createElement('div');
  main.className = 'ovmain';
  main.append(head, pane);

  const buttons = new Map();
  let activeDispose = null;

  function selectSection(id) {
    // Cmd+Shift+A selects the section it opens on, and rebuilding the list
    // there cost frames in the middle of the glide.
    if (id === ovSection && pane.firstChild) {
      return;
    }
    ovSection = id;
    head.textContent = SECTIONS.find((s) => s.id === id)?.label || '';
    for (const [sectionId, button] of buttons) {
      button.classList.toggle('on', sectionId === id);
    }
    activeDispose?.();
    activeDispose = null;
    // replaceChildren, not a style toggle: the previous section's content
    // must unmount, matching the lab's rebuildPanes().
    if (id === 'downloads') {
      const {root, dispose} = renderDownloadsSection();
      pane.replaceChildren(root);
      activeDispose = dispose;
    } else if (id === 'notes') {
      const {root, dispose} = renderNotesSection();
      pane.replaceChildren(root);
      activeDispose = dispose;
    } else if (id === 'spaces') {
      const {root, dispose} = renderSpacesSection();
      pane.replaceChildren(root);
      activeDispose = dispose;
    } else if (id === 'shelf') {
      const {root, dispose} = renderShelfSection();
      pane.replaceChildren(root);
      activeDispose = dispose;
    } else if (id === 'archived') {
      const {root, dispose} = renderArchivedSection();
      pane.replaceChildren(root);
      activeDispose = dispose;
    } else if (id === 'snoozed') {
      const {root, dispose} = renderSnoozedSection();
      pane.replaceChildren(root);
      activeDispose = dispose;
    } else {
      pane.replaceChildren(
          buildPlaceholder(SECTIONS.find((s) => s.id === id)));
    }
  }

  for (const section of SECTIONS) {
    const button = document.createElement('button');
    button.className = 'ovbtn';
    button.dataset.ovsec = section.id;
    button.appendChild(buildIcon(section.id));
    // textContent: the WebUI CSP blocks innerHTML outright.
    button.appendChild(document.createTextNode(section.label));
    button.addEventListener('click', () => selectSection(section.id));
    rail.appendChild(button);
    buttons.set(section.id, button);
  }

  selectSection(ovSection);

  wrap.append(rail, main);
  root.appendChild(wrap);
  // a shelf toast opens this surface AT the Shelf section, so the
  // rail's own click handler is not the only way in.
  window.atriumSelectOverviewSection = (id) => {
    if (buttons.has(id)) {
      selectSection(id);
    }
  };
  return wrap;
}

