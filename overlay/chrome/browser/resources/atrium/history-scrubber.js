// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Opened by sidebarBackHeld -> atriumSetVisitTimeline. The
// hold that opened it is still down, so move/up listen on window rather than
// on the element - the pointer travels off the element it started on.
// Escape listens on wrap.focus() instead, like courtyard-view.js.

export const SCRUBBER_CLASS = 'histscrubwrap';

function buildStop(entry, index) {
  const stop = document.createElement('div');
  stop.className = 'histstop';
  stop.dataset.index = String(index);

  const thumb = document.createElement('div');
  thumb.className = 'histthumb';
  if (entry.thumb) {
    const img = document.createElement('img');
    img.className = 'histthumbimg';
    img.src = entry.thumb;
    thumb.appendChild(img);
  } else {
    // No capture for this stop yet - the C++ side that fills `thumb` lands
    // separately, so a tile with nothing to show must still read as a stop
    // rather than a gap.
    thumb.classList.add('empty');
  }

  const title = document.createElement('span');
  title.className = 'histtitle';
  title.textContent = entry.title || entry.url || '';

  stop.append(thumb, title);
  return stop;
}

export function closeHistoryScrubber(root, {onClose} = {}) {
  const wrap = root.querySelector('.' + SCRUBBER_CLASS);
  if (!wrap) {
    return;
  }
  // Marked before removal, same reason rewind-bar.js marks its own wrap: a
  // pointer event already in flight from a torn-out overlay must read as
  // closed rather than act again.
  wrap.dataset.closed = 'true';
  wrap.__detach?.();
  // Clears the data: URL itself, not just the node holding it.
  for (const img of wrap.querySelectorAll('.histthumbimg')) {
    img.src = '';
  }
  wrap.remove();
  onClose?.();
}

// `send` takes (name, args) - chrome.send's own shape - so a test can inject
// one the way buildBackButton already does.
export function openHistoryScrubber(
    root, timeline, {send = (name, args) => chrome.send(name, args),
                      onClose} = {}) {
  closeHistoryScrubber(root);
  if (!timeline || !timeline.length) {
    return null;
  }

  const wrap = document.createElement('div');
  wrap.className = SCRUBBER_CLASS;
  wrap.tabIndex = -1;

  const strip = document.createElement('div');
  strip.className = 'histstrip';
  const stops = timeline.map(buildStop);
  strip.append(...stops);
  wrap.appendChild(strip);
  root.appendChild(wrap);

  // Starts on the newest stop - releasing without dragging lands back where
  // the tab already is, the same "nothing moved" outcome Escape gives, rather
  // than silently taking one step back on a hold that never dragged.
  let selected = timeline.length - 1;
  const paintSelection = () => {
    stops.forEach((stop, index) => {
      stop.classList.toggle('sel', index === selected);
    });
  };
  paintSelection();

  // Nearest stop by CENTER rather than which rect contains the pointer - a
  // pointer that has drifted past the last stop's edge must still resolve to
  // it instead of losing the selection.
  const selectFromPoint = (clientX) => {
    let closest = selected;
    let bestDistance = Infinity;
    stops.forEach((stop, index) => {
      const rect = stop.getBoundingClientRect();
      const distance = Math.abs(clientX - (rect.left + rect.right) / 2);
      if (distance < bestDistance) {
        bestDistance = distance;
        closest = index;
      }
    });
    if (closest !== selected) {
      selected = closest;
      paintSelection();
    }
  };

  const onMove = (event) => {
    if (wrap.dataset.closed === 'true') {
      return;
    }
    selectFromPoint(event.clientX);
  };
  const finish = (commit) => {
    if (wrap.dataset.closed === 'true') {
      return;
    }
    if (commit) {
      send('sidebarTimelineCommit', [timeline[selected].url, selected]);
    }
    closeHistoryScrubber(root, {onClose});
  };
  const onUp = () => finish(true);

  const MOVES = ['pointermove', 'mousemove'];
  const ENDS = ['pointerup', 'mouseup', 'pointercancel'];
  for (const type of MOVES) {
    window.addEventListener(type, onMove, true);
  }
  for (const type of ENDS) {
    window.addEventListener(type, onUp, true);
  }
  wrap.__detach = () => {
    for (const type of MOVES) {
      window.removeEventListener(type, onMove, true);
    }
    for (const type of ENDS) {
      window.removeEventListener(type, onUp, true);
    }
  };

  wrap.addEventListener('keydown', (event) => {
    if (event.key === 'Escape') {
      finish(false);
    }
  });
  wrap.focus();

  return wrap;
}
