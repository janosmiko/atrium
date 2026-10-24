// Copyright 2026 The Atrium Authors
//
// The Courtyard's session model. Pure: no DOM, no chrome.send -
// the view layer above it does both. The plane geometry lives in
// canvas-model.js.

/** The Courtyard shut: nothing open, no space to return to. */
export function closedCourtyard() {
  return {open: false, priorSpace: -1};
}

/**
 * One step of the Courtyard session: `{state, effect}` for the state and the
 * action, never mutating either.
 *
 * Actions: open (snapshot), enterRoom (spaceIndex), dismiss (activeSpace -
 * Escape and click-away are the same door). The only effect is
 * {type: 'enterSpace', spaceIndex}, which the view sends through the swipe
 * controller so a room click and a dot press announce a switch alike.
 *
 * Knows nothing about where the view lives - a sidebar panel and a full-window
 * overlay drive the same machine.
 */
export function courtyardStep(state, action) {
  const now = state || closedCourtyard();
  const type = action && action.type;
  if (type === 'open') {
    if (now.open) {
      // Re-opening must not re-capture priorSpace, or a second open would
      // strand the way back.
      return {state: now, effect: null};
    }
    const snapshot = action.snapshot;
    return {
      state: {
        open: true,
        priorSpace: snapshot ? snapshot.activeSpace : -1,
      },
      effect: null,
    };
  }
  if (!now.open) {
    return {state: now, effect: null};
  }
  if (type === 'enterRoom') {
    const same = action.spaceIndex === now.priorSpace;
    return {
      state: closedCourtyard(),
      // Opening never left the space, so entering the room you came from is a
      // close, not a switch.
      effect: same ? null : {type: 'enterSpace', spaceIndex: action.spaceIndex},
    };
  }
  if (type === 'dismiss') {
    const moved = action.activeSpace !== undefined &&
        action.activeSpace !== now.priorSpace;
    return {
      state: closedCourtyard(),
      effect: moved ? {type: 'enterSpace', spaceIndex: now.priorSpace} : null,
    };
  }
  return {state: now, effect: null};
}
