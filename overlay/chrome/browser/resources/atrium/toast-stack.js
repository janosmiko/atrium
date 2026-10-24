// Copyright 2026 The Atrium Authors
//
// showToast(), ported from swipe-lab.html's toast stack.
// icon takes a caller-built Node: the WebUI CSP blocks innerHTML.

import {icon} from './icons.js';
import {keycaps} from './keycaps.js';
import {REAL_TIMEOUT_CLOCK} from './clock.js';

// Matches the .bye fade in toast-stack.css.
const BYE_MS = 250;

// Four lost confirmations the user never saw during a burst of copies. Ten
// cards of the 48px a confirmation needs, plus nine 8px gaps, come to 552px,
// which still clears a normal window height.
const STACK_CAP = 10;

// How long a confirmation stays up. The native bubble it replaces used 1400ms
// and nobody called that slow, so the number carries over.
export const CONFIRM_MS = 1400;

// Keeps both ends, unlike CSS text-overflow:ellipsis. `measure`
// is injected so a test can use a deterministic stand-in for font metrics.
export function elideMiddle(text, maxWidth, measure) {
  if (measure(text) <= maxWidth) {
    return text;
  }
  const ellipsis = '…';
  if (measure(ellipsis) >= maxWidth) {
    return ellipsis;
  }
  let lo = 0;
  let hi = Math.floor(text.length / 2);
  let best = 0;
  while (lo <= hi) {
    const mid = Math.floor((lo + hi) / 2);
    const candidate =
        text.slice(0, mid) + ellipsis + text.slice(text.length - mid);
    if (measure(candidate) <= maxWidth) {
      best = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  if (best === 0) {
    return ellipsis;
  }
  return text.slice(0, best) + ellipsis + text.slice(text.length - best);
}

// Ported from the lab's showCopyToast (swipe-lab.html:4913), which is an
// ordinary toast rather than a surface of its own. `ok` false drops the tick,
// because a failure has nothing to confirm.
export function showConfirmToast(
    {title, sub = '', ok = true, accent, iconName, key}, opts = {}) {
  // 'dismissAfterMs' in opts, not ??: a caller passing the 'keep' step's
  // undefined on purpose must reach showToast as undefined, not fall back to
  // CONFIRM_MS - only an opts object with no opinion on the key gets that.
  const dismissAfterMs =
      'dismissAfterMs' in opts ? opts.dismissAfterMs : CONFIRM_MS;
  return showToast(
      {
        color: accent || 'var(--ov-accent)',
        icon: icon(iconName || (ok ? 'check' : 'close')),
        title,
        sub,
        key,
      },
      {...opts, dismissAfterMs});
}

// `o` is {color, icon, title, sub, keys, onClick, closable}. `opts.side` is the sidebar's
// current side ('left', the default, or 'right') - the stack renders in the
// opposite top corner. Returns the card element.
export function showToast(o, {host, side = 'left', clock = REAL_TIMEOUT_CLOCK,
                               dismissAfterMs} = {}) {
  const win = host || document.getElementById('atrium-root');
  let box = win.querySelector('.toasts');
  if (!box) {
    box = document.createElement('div');
    box.className = 'toasts';
    if (side === 'right') {
      box.classList.add('left');
    }
    win.appendChild(box);
  }

  if (o.key) {
    const existing =
        [...box.children].find((c) => c.dataset.toastKey === o.key);
    if (existing) {
      const titleEl = existing.querySelector('.ctl');
      const subEl = existing.querySelector('.ctu');
      if (titleEl) {
        titleEl.textContent = o.title;
      }
      if (subEl) {
        subEl.textContent = o.sub || '';
      }
      existing._restartDismiss(dismissAfterMs);
      return existing;
    }
  }

  const card = document.createElement('div');
  card.className = 'toast ov-card';
  if (o.key) {
    card.dataset.toastKey = o.key;
  }
  card.style.setProperty('--ctc', o.color);

  const text = document.createElement('span');
  const titleEl = document.createElement('div');
  titleEl.className = 'ctl';
  titleEl.textContent = o.title;
  const subEl = document.createElement('div');
  subEl.className = 'ctu';
  subEl.textContent = o.sub || '';
  text.append(titleEl, subEl);

  // Omitted rather than left empty, so the flex gap ahead of the text goes
  // with it - about 44px in a card whose whole job is to show text.
  if (o.icon) {
    const iconEl = document.createElement('span');
    iconEl.className = 'cti';
    iconEl.appendChild(o.icon);
    card.appendChild(iconEl);
  }
  card.appendChild(text);
  if (o.keys) {
    const keys = keycaps(o.keys);
    keys.classList.add('ctk');
    card.appendChild(keys);
  }

  const gone = () => {
    card.remove();
    if (!box.children.length) {
      box.remove();
    }
  };
  let byeHandle;
  const bye = () => {
    if (!box.contains(card)) {
      return;
    }
    if (byeHandle !== undefined) {
      clock.clear(byeHandle);
    }
    card.classList.add('bye');
    byeHandle = clock.set(BYE_MS, gone);
  };
  // Lets a repeated key restart the dismiss clock instead of stacking a card.
  let dismissHandle;
  card._restartDismiss = (ms) => {
    if (dismissHandle !== undefined) {
      clock.clear(dismissHandle);
      dismissHandle = undefined;
    }
    // A replace that lands mid-fade must stop the removal that bye() queued.
    if (byeHandle !== undefined) {
      clock.clear(byeHandle);
      byeHandle = undefined;
      card.classList.remove('bye');
    }
    if (ms > 0) {
      dismissHandle = clock.set(ms, bye);
    }
  };

  // Opt out, not opt in: on the 'keep' timeout there is no dismissAfterMs,
  // and a card with no timer and no button can only leave by eviction.
  if (o.closable !== false) {
    const closeBtn = document.createElement('button');
    closeBtn.className = 'ctcl ov-btn';
    closeBtn.setAttribute('title', 'Dismiss');
    closeBtn.appendChild(icon('close'));
    closeBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      bye();
    });
    card.appendChild(closeBtn);
  }

  if (o.onClick) {
    card.style.cursor = 'pointer';
    card.addEventListener('click', () => {
      gone();
      o.onClick();
    });
  }

  box.prepend(card);
  while (box.children.length > STACK_CAP) {
    box.children[box.children.length - 1].remove();
  }

  card._restartDismiss(dismissAfterMs);

  return card;
}
