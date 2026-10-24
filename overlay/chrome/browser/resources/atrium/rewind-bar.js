// Copyright 2026 The Atrium Authors
//
// Rewind scrubber bar, ported from
// docs/lab/swipe-lab.html:1612.

import {momentSummary, scopeChoices, defaultScope, restorePayload} from './rewind.js';
import {showToast} from './toast-stack.js';

// The lab's rewind accent (swipe-lab.html:1672).
const REWIND_TINT = '#7FB3D5';

// Set when Restore is pressed, consumed when the browser confirms. Held rather
// than shown immediately because a restore the core refuses must show nothing
//, and only the browser knows which happened.
let pendingRewindLabel = null;

function scopeLabelFor(select, value) {
  for (const option of select.querySelectorAll('option')) {
    if (option.value === value) {
      return option.textContent;
    }
  }
  return 'window state';
}

function momentClockTime(moment) {
  const at = moment && moment.at;
  if (!at) {
    return 'an earlier moment';
  }
  return new Date(at).toLocaleTimeString(
      [], {hour: '2-digit', minute: '2-digit', second: '2-digit'});
}

// Called by RestoreSidebarSlice once the restore has really happened. A refused
// one returns before this, so nothing is shown for it.
export function rewindRestored(opts = {}) {
  const label = pendingRewindLabel;
  pendingRewindLabel = null;
  if (!label) {
    return;
  }
  showToast({color: REWIND_TINT, title: 'Rewound', sub: label},
            {host: opts.host, side: opts.side, clock: opts.clock});
}

const BAR_CLASS = 'rewindwrap';

export function closeRewindBar(root) {
  const wrap = root.querySelector('.' + BAR_CLASS);
  if (wrap) {
    // Marked before removal so a click already in flight from a torn-out
    // bar reads as closed.
    wrap.dataset.closed = 'true';
    wrap.remove();
  }
}

// `getCurrent` reads the live snapshot at click time - a value captured at
// open time could go stale before the click.
export function openRewindBar(root, moments, getCurrent) {
  closeRewindBar(root);
  if (!moments || !moments.length) {
    return null;
  }
  const lastIndex = moments.length - 1;
  const latest = moments[lastIndex].state;
  const scope = defaultScope(latest);

  const wrap = document.createElement('div');
  wrap.className = BAR_CLASS;
  const bar = document.createElement('div');
  bar.className = 'rewindbar';

  const title = document.createElement('b');
  title.textContent = 'Rewind';

  const select = document.createElement('select');
  select.className = 'rwscope';
  for (const choice of scopeChoices(latest)) {
    const option = document.createElement('option');
    option.value = choice.value;
    option.textContent = choice.label;
    if (choice.value === scope) {
      option.selected = true;
    }
    select.appendChild(option);
  }
  select.value = scope;

  const slider = document.createElement('input');
  slider.type = 'range';
  slider.className = 'rwslider';
  slider.min = '0';
  slider.max = String(lastIndex);
  slider.step = '1';
  slider.value = String(lastIndex);
  // A ring of one moment still gets a slider rather than a hidden control -
  // there is just nowhere for it to move.
  slider.disabled = lastIndex === 0;

  const info = document.createElement('span');
  info.className = 'rwinfo';

  const restore = document.createElement('button');
  restore.type = 'button';
  restore.className = 'rwbtn rwgo';
  restore.textContent = 'Restore';
  restore.addEventListener('click', () => {
    // A bar already torn out by a re-render (dataset.closed) must not act -
    // its slider/select values belong to a ring that has moved on
    //.
    if (wrap.dataset.closed === 'true') {
      return;
    }
    const payload = restorePayload(
        moments[Number(slider.value)], getCurrent(), select.value);
    // Null means restoreSnapshot itself refused - a scope the moment never
    // held. Sending nothing is the refusal, there is no partial payload to
    // send instead.
    if (!payload) {
      return;
    }
    // what the toast will say, held until the browser confirms
    // the restore actually happened. Described HERE because this is where the
    // scope label and the moment's time are known - C++ would have to re-derive
    // both from the payload to say the same sentence.
    const moment = moments[Number(slider.value)];
    pendingRewindLabel = scopeLabelFor(select, select.value) + ' as of ' +
        momentClockTime(moment);
    chrome.send('sidebarRestoreSlice', [JSON.stringify(payload)]);
    closeRewindBar(root);
  });

  const close = document.createElement('button');
  close.type = 'button';
  close.className = 'rwbtn rwx';
  close.title = 'Close';
  close.textContent = '×';
  close.addEventListener('click', () => closeRewindBar(root));

  const update = () => {
    const summary = momentSummary(moments[Number(slider.value)], select.value);
    info.textContent = summary ? summary.text : '';
  };
  slider.addEventListener('input', update);
  select.addEventListener('change', update);
  update();

  bar.append(title, select, slider, info, restore, close);
  wrap.appendChild(bar);
  root.appendChild(wrap);
  return wrap;
}
