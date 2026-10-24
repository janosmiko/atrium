// Copyright 2026 The Atrium Authors
//
// The Courtyard route: an endless plane of rooms, replacing the
// CSS grid. Built with createElement/textContent throughout - the WebUI CSP
// registers no Trusted Types policy, so innerHTML throws.
//
// The plane is one transformed element and every room is absolutely placed
// inside it, so pan and zoom are one style write rather than a relayout of
// sixty cards. There is no virtualisation: sixty cards are sixty elements.

import {closedCourtyard, courtyardStep} from './courtyard.js';
import {spaceGlyph} from './space-glyph.js';
import {
  CARD_TOP,
  cardY,
  clampZoom,
  dropDecision,
  fitView,
  itemsFrom,
  laneHeight,
} from './canvas-model.js';

// A wheel notch is a fraction of a doubling, so a fast pinch reaches a limit
// in a few notches and a slow one still lands between them.
const ZOOM_PER_DELTA = 0.0015;

function buildRoom(item) {
  const room = document.createElement('div');
  room.className = 'room' + (item.current ? ' cur' : '');
  room.dataset.room = String(item.spaceIndex);
  room.style.left = `${item.rect.x}px`;
  room.style.top = `${item.rect.y}px`;
  room.style.width = `${item.rect.w}px`;
  room.style.height = `${item.rect.h}px`;
  if (item.color) {
    room.style.background = item.color;
  }

  const head = document.createElement('div');
  head.className = 'rhead';
  const icon = document.createElement('span');
  icon.className = 'ric';
  icon.appendChild(spaceGlyph(item.icon, item.name));
  const name = document.createElement('span');
  name.textContent = item.name;
  head.append(icon, name);

  const grip = document.createElement('div');
  grip.className = 'rgrip';

  room.append(head, grip);
  for (const lane of item.lanes) {
    const el = document.createElement('div');
    el.className = 'rlane';
    el.dataset.section = lane.section;
    el.style.left = `${lane.x - item.rect.x}px`;
    el.style.top = `${lane.y - item.rect.y}px`;
    el.style.width = `${lane.w}px`;
    el.style.height = `${lane.h}px`;
    room.appendChild(el);
  }
  return room;
}

function buildCard(item) {
  const card = document.createElement('div');
  card.className = 'rtab' + (item.selected ? ' sel' : '');
  card.dataset.index = String(item.index);
  card.dataset.section = item.section;
  card.dataset.room = String(item.spaceIndex);
  // Relative to the LANE it is appended to, not to the plane. `x` is a plane
  // coordinate and `lanePlaneX` is the lane's own origin, so the difference is
  // the stored offset plus nothing else.
  card.style.left = `${item.x - item.lanePlaneX}px`;
  card.style.top = `${cardY(item.index)}px`;
  const dot = document.createElement('span');
  dot.className = 'rfv';
  dot.style.background = item.color || '';
  const title = document.createElement('span');
  title.textContent = item.title || '';
  card.append(dot, title);
  return card;
}

export function installCourtyard(root) {
  root.replaceChildren();

  let session = closedCourtyard();
  let snapshot = null;
  let items = [];
  // Per window and never persisted: the plane fits every room on every open.
  let view = {x: 0, y: 0, zoom: 1};
  let centred = false;

  const wrap = document.createElement('div');
  wrap.className = 'courtwrap';
  const plane = document.createElement('div');
  plane.className = 'plane';
  wrap.appendChild(plane);
  root.appendChild(wrap);

  function applyView() {
    plane.style.transform =
        `translate(${view.x}px, ${view.y}px) scale(${view.zoom})`;
  }

  // The inverse of applyView's transform: a client point back through the
  // plane's own pan and zoom, so a drag can work in the same units the
  // snapshot's rects and lanes are stored in.
  function planePoint(clientX, clientY) {
    const wrapRect = wrap.getBoundingClientRect();
    return {
      x: (clientX - wrapRect.left - view.x) / view.zoom,
      y: (clientY - wrapRect.top - view.y) / view.zoom,
    };
  }

  function setZoom(next) {
    const clamped = clampZoom(next);
    if (clamped === view.zoom) {
      // Still report it: the close-at-the-floor rule needs to know the plane
      // is AT the floor, and a pinch that changes nothing is exactly when it
      // matters.
      chrome.send('courtyardZoom', [view.zoom]);
      return;
    }
    view = {...view, zoom: clamped};
    applyView();
    chrome.send('courtyardZoom', [view.zoom]);
  }

  function fitAllRooms() {
    // No measured viewport yet (unlaid-out DOM, or the node test shim, which
    // models no layout at all): nothing to fit against, so leave the view at
    // its default rather than aiming at width/height zero.
    if (!wrap.clientWidth || !wrap.clientHeight) {
      return;
    }
    const rects = items.filter((i) => i.kind === 'room').map((i) => i.rect);
    view = fitView(rects, wrap.clientWidth, wrap.clientHeight);
    applyView();
  }

  // Clearing `centred` is the point: the next open re-fits every room instead
  // of showing the pan and zoom this one was left at.
  function closeSession() {
    session = closedCourtyard();
    centred = false;
  }

  function dismiss() {
    const {state, effect} = courtyardStep(
        session,
        {type: 'dismiss', activeSpace: snapshot ? snapshot.activeSpace : -1});
    session = state;
    if (effect && effect.type === 'enterSpace') {
      chrome.send('sidebarSpaceSelected', [effect.spaceIndex]);
    }
    chrome.send('courtyardDismiss', []);
  }

  function enterRoom(spaceIndex) {
    const {state, effect} =
        courtyardStep(session, {type: 'enterRoom', spaceIndex});
    session = state;
    if (effect && effect.type === 'enterSpace') {
      chrome.send('sidebarSpaceSelected', [effect.spaceIndex]);
    }
    chrome.send('courtyardDismiss', []);
  }

  function render() {
    if (drag) {
      // The pointer is still down, so a release is still coming. Without this
      // the release lands as a click and dismisses or activates.
      dragJustEnded = true;
    }
    drag = null;
    plane.replaceChildren();
    if (!snapshot) {
      return;
    }
    const active = snapshot.spaces[snapshot.activeSpace];
    wrap.style.setProperty('--cy-tint', (active && active.color) || '#7f8f9f');
    items = itemsFrom(snapshot);
    const roomOf = new Map();
    for (const item of items) {
      if (item.kind !== 'room') {
        continue;
      }
      const room = buildRoom(item);
      roomOf.set(item.spaceIndex, room);
      plane.appendChild(room);
    }
    for (const item of items) {
      if (item.kind !== 'card') {
        continue;
      }
      const room = roomOf.get(item.spaceIndex);
      const lane = room &&
          room.querySelector(`.rlane[data-section="${item.section}"]`);
      if (!lane) {
        continue;
      }
      lane.appendChild(buildCard(item));
    }
    applyView();
  }

  // Pointer capture rather than HTML5 drag and drop: a drop needs a PLANE
  // coordinate, and a DragEvent only carries client coordinates that no
  // listener can turn back through the plane's own transform.
  let drag = null;
  let dragJustEnded = false;

  function laneNeed(item) {
    return item.lanes.reduce(
        (tallest, lane) => Math.max(tallest, laneHeight(lane.count)),
        CARD_TOP);
  }

  function beginRoomDrag(event, room, mode) {
    const spaceIndex = Number(room.dataset.room);
    const item = items.find((i) => i.kind === 'room' && i.spaceIndex === spaceIndex);
    if (!item) {
      return;
    }
    if (event.target.setPointerCapture) {
      event.target.setPointerCapture(event.pointerId);
    }
    drag = {
      mode,
      pointerId: event.pointerId,
      spaceIndex,
      origin: planePoint(event.clientX, event.clientY),
      rect: {...item.rect},
      minHeight: laneNeed(item),
      node: room,
      moved: false,
    };
  }

  function beginCardDrag(event, card) {
    const spaceIndex = Number(card.dataset.room);
    const section = card.dataset.section;
    const index = Number(card.dataset.index);
    const item = items.find(
        (i) => i.kind === 'card' && i.spaceIndex === spaceIndex &&
            i.section === section && i.index === index);
    if (!item) {
      return;
    }
    if (event.target.setPointerCapture) {
      event.target.setPointerCapture(event.pointerId);
    }
    drag = {
      mode: 'card',
      pointerId: event.pointerId,
      spaceIndex,
      section,
      index,
      url: item.url,
      id: item.id,
      origin: planePoint(event.clientX, event.clientY),
      start: {x: item.x, y: item.y},
      node: card,
      moved: false,
    };
  }

  // The plane itself, so the drag works in CLIENT units: planePoint reads the
  // very view this gesture is moving, and would chase its own tail.
  function beginPan(event) {
    if (wrap.setPointerCapture) {
      wrap.setPointerCapture(event.pointerId);
    }
    drag = {
      mode: 'pan',
      pointerId: event.pointerId,
      origin: {x: event.clientX, y: event.clientY},
      start: {x: view.x, y: view.y},
      moved: false,
    };
  }

  function moveDrag(event) {
    if (!drag || event.pointerId !== drag.pointerId) {
      return;
    }
    if (drag.mode === 'pan') {
      const panX = event.clientX - drag.origin.x;
      const panY = event.clientY - drag.origin.y;
      if (!drag.moved && Math.abs(panX) < 3 && Math.abs(panY) < 3) {
        return;
      }
      drag.moved = true;
      view = {...view, x: drag.start.x + panX, y: drag.start.y + panY};
      applyView();
      return;
    }
    const at = planePoint(event.clientX, event.clientY);
    const dx = at.x - drag.origin.x;
    const dy = at.y - drag.origin.y;
    if (!drag.moved && Math.abs(dx) < 3 && Math.abs(dy) < 3) {
      return;
    }
    drag.moved = true;
    if (drag.mode === 'move') {
      drag.next = {
        x: drag.rect.x + dx,
        y: drag.rect.y + dy,
        w: drag.rect.w,
        h: drag.rect.h,
      };
      drag.node.style.left = `${drag.next.x}px`;
      drag.node.style.top = `${drag.next.y}px`;
      return;
    }
    if (drag.mode === 'resize') {
      drag.next = {
        x: drag.rect.x,
        y: drag.rect.y,
        // A room narrower than one lane cannot be hit-tested, and one shorter
        // than its tallest lane clips the cards it is meant to show.
        w: Math.max(drag.rect.w + dx, 80),
        h: Math.max(drag.rect.h + dy, drag.minHeight),
      };
      drag.node.style.width = `${drag.next.w}px`;
      drag.node.style.height = `${drag.next.h}px`;
    }
    if (drag.mode === 'card') {
      drag.at = {x: drag.start.x + dx, y: drag.start.y + dy};
      // Lifted out of its lane's flow for the duration, so it can cross a lane
      // and a room boundary without the lane clipping it.
      drag.node.classList.add('dragging');
      drag.node.style.transform = `translate(${dx}px, ${dy}px)`;
    }
  }

  function endDrag(event) {
    if (!drag || event.pointerId !== drag.pointerId) {
      return;
    }
    const done = drag;
    drag = null;
    dragJustEnded = done.moved;
    if (!done.moved) {
      return;
    }
    if (done.mode === 'card') {
      done.node.classList.remove('dragging');
      done.node.style.transform = '';
      const decision = dropDecision(
          {
            spaceIndex: done.spaceIndex,
            section: done.section,
            index: done.index,
            url: done.url,
          },
          done.at, items, snapshot ? snapshot.activeSpace : -1);
      if (decision) {
        chrome.send(decision.send, decision.args);
      }
      return;
    }
    if (!done.next) {
      return;
    }
    if (done.mode === 'move' || done.mode === 'resize') {
      chrome.send('courtyardRoomRect', [
        done.spaceIndex,
        Math.round(done.next.x),
        Math.round(done.next.y),
        Math.round(done.next.w),
        Math.round(done.next.h),
      ]);
    }
  }

  // A cancelled gesture has no release and so no click follows it - unlike
  // endDrag, this must not raise dragJustEnded or the next real click is lost.
  function cancelDrag(event) {
    if (!drag || event.pointerId !== drag.pointerId) {
      return;
    }
    drag = null;
    render();
  }

  wrap.addEventListener('pointerdown', (event) => {
    const target = event.target;
    if (!target.closest) {
      return;
    }
    const grip = target.closest('.rgrip');
    if (grip) {
      beginRoomDrag(event, grip.closest('.room'), 'resize');
      return;
    }
    const head = target.closest('.rhead');
    if (head) {
      beginRoomDrag(event, head.closest('.room'), 'move');
      return;
    }
    const card = target.closest('.rtab');
    if (card) {
      beginCardDrag(event, card);
      return;
    }
    beginPan(event);
  });
  wrap.addEventListener('pointermove', moveDrag);
  wrap.addEventListener('pointerup', endDrag);
  wrap.addEventListener('pointercancel', cancelDrag);

  wrap.addEventListener('wheel', (event) => {
    // Blink treats ctrl+wheel as a page zoom request, and a plain wheel would
    // still scroll the (non-scrolling) wrap - either fires alongside the
    // plane's own pan/zoom without this.
    event.preventDefault();
    if (event.ctrlKey) {
      setZoom(view.zoom * (1 - event.deltaY * ZOOM_PER_DELTA));
      return;
    }
    view = {...view, x: view.x - event.deltaX, y: view.y - event.deltaY};
    applyView();
  }, {passive: false});

  wrap.addEventListener('click', (event) => {
    if (dragJustEnded) {
      dragJustEnded = false;
      return;
    }
    const card = event.target.closest && event.target.closest('.rtab');
    if (card) {
      // By room, section and index rather than by tab id: live ids reach the
      // snapshot for the ACTIVE space alone, so every card in another room
      // carries an empty one and so does a cold row.
      closeSession();
      chrome.send('courtyardCardActivated', [
        Number(card.dataset.room),
        card.dataset.section,
        Number(card.dataset.index),
      ]);
      chrome.send('courtyardDismiss', []);
      return;
    }
    const head = event.target.closest && event.target.closest('.rhead');
    if (head) {
      const room = head.closest('.room');
      enterRoom(Number(room.dataset.room));
      return;
    }
    if (!event.target.closest || !event.target.closest('.room')) {
      dismiss();
    }
  });

  // wrap, focused below, rather than window: keydown targets whatever holds
  // focus, and this document has no field of its own to give it to.
  wrap.tabIndex = -1;
  wrap.addEventListener('keydown', (event) => {
    if (event.key === 'Escape') {
      dismiss();
    }
  });

  window.atriumSetCourtyard = (pushed) => {
    snapshot = pushed;
    if (!session.open) {
      session = courtyardStep(session, {type: 'open', snapshot}).state;
      centred = false;
    }
    render();
    if (!centred) {
      fitAllRooms();
      centred = true;
    }
  };

  // The widget can be hidden without the page hearing about it (a pinch, the
  // command bar verb, a card click). Left open, the session would refuse the
  // next `open` step and the plane would come back at the old pan and zoom.
  window.atriumCourtyardClosed = () => {
    closeSession();
  };

  // `delta` is the same wheel deltaY sign the AppKit event carried, so the
  // two paths agree on which way is closer.
  window.atriumCourtyardPinch = (delta) => {
    setZoom(view.zoom * (1 - delta * ZOOM_PER_DELTA));
  };

  wrap.focus();
  chrome.send('courtyardReady', []);
}
