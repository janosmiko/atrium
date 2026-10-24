// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The toast stack on its own widget: hosted in the sidebar document, a card's
// top/right resolved against that narrow column rather than the window.

import {elideMiddle, showConfirmToast, showToast} from './toast-stack.js';
import {parseShadowLayer, splitShadowLayers} from './overlay-shadow-spec.js';
import {dismissAfterMsForTimeout, shouldShowToast} from './notification-timeout.js';
import {REAL_TIMEOUT_CLOCK} from './clock.js';

// Re-measures once the viewport catches up to a just-requested resize - a
// card burst can outrun it, stranding left/right against a stale width.
let toastResizeObserver = null;

// showToast builds .toasts on the first card, so the side arrives before there
// is anything to put it on. Held here and re-applied on every report instead.
let stackSide = 'right';

// The widget sizes itself to the stack rather than spanning the window, so it
// is not over the page when there are no cards to click.
function reportSize(root) {
  const box = root.querySelector('.toasts');
  if (!box) {
    chrome.send('toastStackEmpty', []);
    chrome.send('toastCardShadows', ['[]']);
    return;
  }
  box.classList.toggle('left', stackSide === 'left');
  const r = box.getBoundingClientRect();
  // .toasts shrink-to-fits against the widget's own current viewport, so its
  // rect can never come back wider than the widget already is. A
  // card, a non-stretched flex item, renders past that clamp - read it directly.
  let width = r.width;
  for (const card of box.children) {
    width = Math.max(width, card.getBoundingClientRect().width);
  }
  // The accent ring paints outside the border box and a window clips at its
  // frame, so without this room the widest card loses its ring and its rounded
  // corners. toast-stack.css insets the stack by the same amount.
  const ring =
      parseFloat(getComputedStyle(box).getPropertyValue('--ct-ring')) || 0;
  chrome.send('toastStackSized',
              [Math.ceil(width + 2 * ring), Math.ceil(r.height + 2 * ring),
               ring]);
  pushCardShadows(box);
  if (!toastResizeObserver) {
    toastResizeObserver = new ResizeObserver(() => {
      const liveBox = root.querySelector('.toasts');
      if (liveBox) {
        pushCardShadows(liveBox);
      }
    });
    toastResizeObserver.observe(document.documentElement);
  }
}

// Off-screen, not display:none (which some engines never resolve style
// for) - see toast-stack.css's .shadow-probe, the one place --ov-shadow-tight
// can still be read now that .toast itself carries no shadow.
let shadowProbe = null;
function resolveCardShadow() {
  // isConnected, not just null: a detached node resolves no style, so a probe
  // that lost its document would answer nothing and no shadow would be pushed.
  if (!shadowProbe || !shadowProbe.isConnected) {
    shadowProbe = document.createElement('div');
    shadowProbe.className = 'toast shadow-probe';
    shadowProbe.style.cssText =
        'position:absolute;visibility:hidden;left:-9999px;top:-9999px;';
    document.body.appendChild(shadowProbe);
  }
  const layers = splitShadowLayers(getComputedStyle(shadowProbe).boxShadow);
  return layers.length ? parseShadowLayer(layers[0]) : null;
}

// How long the shadow takes to arrive once the card has settled.
const SHADOW_FADE_IN_MS = 140;

// When each card's shadow may first appear: the end of its own entry keyframe.
// A deadline per card rather than a flag, because an insertion provokes two
// pushes and each rebuilds every shadow slot.
const shadowStart = new WeakMap();
// Cards whose fade-in has already been asked for. Re-asking would restart it.
const fadeAsked = new WeakSet();
// The native side keys a shadow by its slot in the stack, and the stack
// prepends, so a slot changes hands. This is how it tells that apart from the
// same card pushed again, and drops what the previous occupant left behind.
let lastCardId = 0;
const cardIds = new WeakMap();
let nowMs = () => Date.now();
let shadowClock = REAL_TIMEOUT_CLOCK;

// Each card's own rect (this document's coordinates - the same ones
// AtriumBrowserWindow adds the widget's screen origin to) plus the shadow
// every card shares, one JSON string per push.
function pushCardShadows(box) {
  const shadow = resolveCardShadow();
  if (!shadow) {
    chrome.send('toastCardShadows', ['[]']);
    return;
  }
  // A card's offsetParent is .toasts, which toast-stack.css positions in every
  // variant, and .toasts itself is never animated.
  const boxRect = box.getBoundingClientRect();
  const cards = [...box.children].map((card) => {
    const cs = getComputedStyle(card);
    const radius = parseFloat(cs.borderTopLeftRadius) || 0;
    // One property for both ends: .toast animates atrium-toast-peekin in,
    // .bye animates atrium-toast-peekfade out, and each is the live one while
    // it runs.
    const animMs = (parseFloat(cs.animationDuration) || 0) * 1000;
    const fading = card.classList.contains('bye');
    if (!cardIds.has(card)) {
      cardIds.set(card, ++lastCardId);
    }
    if (!shadowStart.has(card)) {
      shadowStart.set(card, nowMs() + animMs);
      // The card's box does not change when its keyframe ends, so nothing else
      // pushes again and the shadow would never arrive.
      shadowClock.set(animMs, () => {
        if (box.isConnected) {
          pushCardShadows(box);
        }
      });
    }
    // Positive while the keyframe runs. atrium-toast-peekin holds the card at
    // scale(.93) 10px lower than the box the ring below is drawn around, so a
    // shadow shown before then rings empty space beside the card.
    const untilShadow = shadowStart.get(card) - nowMs();
    const shadowUp = untilShadow <= 0;
    const startsFade = shadowUp && !fadeAsked.has(card);
    if (startsFade) {
      fadeAsked.add(card);
    }
    // A card can be dismissed inside its own entry keyframe, and a ring that
    // never arrived has nothing to fade out - it would be registered solid for
    // the first frame of the fade, which is the complaint again.
    const entering = !shadowUp;
    // offset*, not getBoundingClientRect: the entry keyframe scales the card
    // to .93 and drops it 10px, so a rect read mid-entry is the box the shadow
    // would keep once the card had settled somewhere else.
    return {
      id: cardIds.get(card),
      left: Math.round(boxRect.left + card.offsetLeft),
      top: Math.round(boxRect.top + card.offsetTop),
      width: Math.round(card.offsetWidth),
      height: Math.round(card.offsetHeight),
      radius,
      offsetX: shadow.offsetX, offsetY: shadow.offsetY, blur: shadow.blur,
      r: shadow.r, g: shadow.g, b: shadow.b, a: shadow.a,
      // A .bye card keeps its box for the whole fade. Its shadow is painted
      // outside this document and would otherwise stay solid over a card
      // nobody can see any more.
      fading: fading && shadowUp,
      fadeMs: animMs,
      // The same problem at the other end. While entering there is no shadow
      // at all, and the fade that follows is asked for once, so a re-push
      // during it re-registers the ring without restarting it.
      entering,
      enterMs: startsFade && !fading ? SHADOW_FADE_IN_MS : 0,
    };
  });
  chrome.send('toastCardShadows', [JSON.stringify(cards)]);
}

// A share of the web card the url describes. ToastOverlayWidget pushes that
// width through atriumSetPaneWidth. The fallback stands in only until the
// first push, since a card can be built before it lands.
const HOVER_WIDTH_SHARE = 0.4;
const HOVER_WIDTH_FLOOR = 240;
// A status line past this stops being scannable, and on a very wide pane the
// share alone would run the card across most of the page.
const HOVER_WIDTH_CEIL = 640;
const HOVER_WIDTH_FALLBACK = 600;

let paneWidth = 0;

function hoverMaxLabelWidth(label) {
  const pane = paneWidth || HOVER_WIDTH_FALLBACK;
  const cardWidth = Math.max(
      HOVER_WIDTH_FLOOR, Math.min(HOVER_WIDTH_CEIL, pane * HOVER_WIDTH_SHARE));
  const card = label.closest('.toast');
  const style = card ? getComputedStyle(card) : null;
  const padding = style ?
      (parseFloat(style.paddingLeft) || 0) + (parseFloat(style.paddingRight) || 0) :
      0;
  // The modifier hint rides in the same card, so the url gets what it leaves.
  const hint = card ? card.querySelector('.cthint') : null;
  const hintWidth = hint ? hint.getBoundingClientRect().width : 0;
  const gap = style ? (parseFloat(style.gap) || 0) : 0;
  return cardWidth - padding - (hintWidth ? hintWidth + gap : 0);
}

// blink::NavigationPolicyFromEventModifiers' mac branch (navigation_policy.cc),
// plus the two intercepts browser.cc puts ahead of it.
function modifierHint(shift, cmd, option) {
  // Cmd+Option is NEW_SPLIT_VIEW, which browser.cc spends on a little window.
  // Checked before the plain cmd branch, which would swallow it.
  if (cmd && option && !shift) {
    return 'Little window';
  }
  if (cmd) {
    return shift ? 'New tab' : 'Background tab';
  }
  // Both are NEW_WINDOW, which the peek intercept takes.
  if (shift || option) {
    return 'Peek';
  }
  return '';
}

// An off-screen twin of the label. Measuring the live one would mutate a
// node this surface's own MutationObserver watches, resizing the widget
// mid-search.
let measurer = null;
function measureLabel(text) {
  if (!measurer) {
    const wrap = document.createElement('div');
    wrap.className = 'toastroot';
    wrap.style.cssText =
        'position:absolute;visibility:hidden;white-space:nowrap;' +
        'left:-9999px;top:-9999px;';
    const card = document.createElement('div');
    card.className = 'toast ov-card';
    const span = document.createElement('span');
    measurer = document.createElement('div');
    measurer.className = 'ctl';
    span.appendChild(measurer);
    card.appendChild(span);
    wrap.appendChild(card);
    document.body.appendChild(wrap);
  }
  measurer.textContent = text;
  return measurer.scrollWidth;
}

// A test overrides this whole seam instead of measureLabel/hoverMaxLabelWidth
// individually - the dom-shim has no layout engine to measure against. The
// budget arrives as an argument so a test can read which width the fit got.
function defaultFitHoverLabel(label, text, maxWidth) {
  label.textContent = elideMiddle(text, maxWidth, measureLabel);
}

export function installToastSurface(
    root,
    {fitHoverLabel = defaultFitHoverLabel, clock = REAL_TIMEOUT_CLOCK,
     now = () => Date.now()} = {}) {
  root.classList.add('toastroot');
  // Two seams, because a card's shadow needs both: how far into its entry
  // keyframe it already is, and the one re-push that ends that keyframe.
  nowMs = now;
  shadowClock = clock;

  // The stack removes its own box when the last card leaves. A dismissal
  // starts as a class though, not a removal, and the shadow has to learn about
  // the fade while it runs rather than when the card finally goes.
  const observer = new MutationObserver(() => reportSize(root));
  observer.observe(root, {
    childList: true,
    subtree: true,
    attributes: true,
    attributeFilter: ['class'],
  });

  // Set by window.atriumSetNotificationTimeout. 'fiveSeconds' matches
  // settings.rs's default for a surface built before the first push.
  let notificationTimeoutStep = 'fiveSeconds';

  window.atriumConfirmToast = (title, sub, ok, iconName, key) => {
    if (!shouldShowToast(notificationTimeoutStep)) {
      return null;
    }
    const card = showConfirmToast(
        {title, sub: sub || '', ok: ok !== false, iconName: iconName || undefined,
         key: key || undefined},
        {
          host: root,
          clock,
          dismissAfterMs: dismissAfterMsForTimeout(notificationTimeoutStep),
        });
    reportSize(root);
    return card;
  };

  window.atriumSetNotificationTimeout = (step) => {
    notificationTimeoutStep = step;
  };

  // A hovered link is not a confirmation: it stays while the pointer is on the
  // link, retargets in place when it moves to another, and clears on unhover.
  // showToast's dismissAfterMs path does none of that, so the card is held.
  let hoverCard = null;
  let hoverText = '';

  // The fit mutates the live label, the observer above answers with reportSize,
  // and that report is what makes the widget push a width back. The flag keeps
  // that return trip out of the fit it would re-enter.
  let fitting = false;
  const applyFit = (label, text) => {
    fitting = true;
    try {
      // A width can arrive through the size report this fit provokes. Take it
      // on a second pass rather than leaving the card fitted to the old one.
      let passes = 0;
      let used;
      do {
        used = paneWidth;
        fitHoverLabel(label, text, hoverMaxLabelWidth(label));
      } while (paneWidth !== used && ++passes < 2);
    } finally {
      fitting = false;
    }
  };

  // Which held modifier the card is currently announcing. Kept across a
  // retarget: the key is still down when the pointer reaches the next link.
  let navHint = '';
  const hintElement = () => {
    let el = hoverCard.querySelector('.cthint');
    if (!el) {
      el = document.createElement('span');
      el.className = 'cthint';
      hoverCard.appendChild(el);
    }
    return el;
  };

  window.atriumHoverUrl = (text) => {
    if (!text) {
      if (hoverCard) {
        hoverCard.remove();
        hoverCard = null;
      }
      hoverText = '';
      reportSize(root);
      return;
    }
    if (!(hoverCard && hoverCard.isConnected)) {
      hoverCard = showToast({title: text, closable: false}, {host: root});
    }
    hoverText = text;
    if (navHint) {
      hintElement().textContent = navHint;
    }
    const label = hoverCard.querySelector('.ctl');
    if (label) {
      applyFit(label, text);
    }
    reportSize(root);
  };

  // AtriumBrowserWindow::SetSidebarNavModifiers pushes here from the native
  // flagsChanged monitor. The page never sees a keydown of its own: the
  // pointer is over the web content, which owns the keyboard.
  window.atriumSetNavModifiers = (shift, cmd, option) => {
    const next = modifierHint(shift, cmd, option);
    if (next === navHint) {
      return;
    }
    navHint = next;
    if (!(hoverCard && hoverCard.isConnected)) {
      return;
    }
    hintElement().textContent = navHint;
    const label = hoverCard.querySelector('.ctl');
    if (label) {
      applyFit(label, hoverText);
    }
    reportSize(root);
  };

  // ToastOverlayWidget::PushPaneWidth sends the web card's width every time it
  // places the card and on every pane layout change, so the sidebar can be
  // resized under a card that is already up.
  window.atriumSetPaneWidth = (px) => {
    const next = Math.max(0, Math.round(Number(px) || 0));
    // An unchanged width refits nothing: the refit reports a size, the report
    // brings the width back, and the two would chase each other forever.
    if (next === paneWidth) {
      return;
    }
    paneWidth = next;
    if (fitting || !(hoverCard && hoverCard.isConnected)) {
      return;
    }
    const label = hoverCard.querySelector('.ctl');
    if (label) {
      applyFit(label, hoverText);
      reportSize(root);
    }
  };

  // Which side the stack anchors to INSIDE its widget. It has to match the
  // side the widget is anchored to the pane on, or a viewport that has not
  // caught up to the last resize slides the card sideways.
  window.atriumToastSide = (side) => {
    stackSide = side === 'left' ? 'left' : 'right';
    const box = root.querySelector('.toasts');
    if (box) {
      box.classList.toggle('left', stackSide === 'left');
    }
  };

  chrome.send('toastReady', []);
}
