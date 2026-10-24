// Copyright 2026 The Atrium Authors
//
// Mirror View's choice list.
//
// Ported from the lab's menu build, docs/lab/swipe-lab.html:4426-4428. Pure:
// it takes the sidebar snapshot the surface already receives and returns plain
// values. No DOM, no browser messages - the menu above it does both.
//
// The lab keeps a global list of profiles. Atrium has none: a profile is a
// property of a space, so the offer is derived from the chips - which is why
// each one now carries profileId.
//
// Nothing here reaches off the machine. It reads its argument and returns an
// array; the module keeps no state.

/**
 * The profileId standing for "no session at all" - the lab's `prof=null`.
 *
 * A sentinel rather than null so a caller can key on `choice.profileId`
 * without a separate branch, and so an empty profileId (a chip that arrived
 * without one) cannot be mistaken for the logged-out choice.
 */
export const LOGGED_OUT = ' logged-out';

/**
 * The profiles a mirror of the current tab can be opened as: every profile the
 * user's spaces name except the one they are already in, in the order those
 * spaces appear, then Logged out.
 *
 * The current profile is left out because a pane showing the same session as
 * the one beside it is not a comparison - it is the same page twice.
 *
 * `label` is the chip's profileName, the name the settings pane shows. A chip
 * without one (a profile the document does not name) is labelled by its id.
 *
 * Returns an empty list rather than throwing on a snapshot that is missing or
 * has no spaces - the menu asks before the first push has necessarily landed.
 */
export function mirrorChoices(snapshot) {
  const spaces = snapshot?.spaces;
  if (!Array.isArray(spaces) || spaces.length === 0) {
    return [];
  }
  const current = spaces[snapshot.activeSpace]?.profileId || null;
  const seen = new Set();
  const choices = [];
  for (const space of spaces) {
    const id = space?.profileId;
    // An empty id names no profile, so there is nothing to open a session as.
    if (!id || id === current || seen.has(id)) {
      continue;
    }
    seen.add(id);
    choices.push({profileId: id, label: space.profileName || id});
  }
  choices.push({profileId: LOGGED_OUT, label: 'Logged out'});
  return choices;
}
