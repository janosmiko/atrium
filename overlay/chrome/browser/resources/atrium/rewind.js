// Copyright 2026 The Atrium Authors
//
// Rewind's session ring and scoped restore.
//
// Ported from the lab's Rewind, docs/lab/swipe-lab.html:1602-1677. Pure: it
// takes the sidebar snapshot the surface already receives and returns plain
// values. No DOM, no browser messages - the view layer above it does both.
//
// The lab owns its stores, so it can snapshot on each structural gesture.
// Atrium sees only the snapshot, so the ring dedupes on a structural digest
// instead: an update that moved a tint but no tab is not a moment.
//
// Nothing here reaches off the machine. The ring is an array the caller holds;
// the module keeps no state of its own, writes nothing to disk, and has no way
// to talk to anything.

/** How many moments the ring keeps. 60, as in the lab. */
export const MAX_MOMENTS = 60;

const SCOPE_WINDOW = 'all';

function copy(value) {
  return JSON.parse(JSON.stringify(value));
}

// Identity, order, selection and fold state are structure. Colour and tint are
// not: they change on every theme tick and would fill the ring with nothing.
function projectRow(row) {
  return {
    id: row.id,
    title: row.title,
    url: row.url,
    selected: !!row.selected,
  };
}

function projectFolder(folder) {
  return {
    id: folder.id,
    name: folder.name,
    closed: !!folder.closed,
    tabs: (folder.tabs || []).map(projectRow),
  };
}

// restore.rs's Slice::from_chip flattens a pinned folder to its member rows
// too, so a moment restores the pins ungrouped - projecting the folder object
// itself (no url) is what made the restore payload fail to parse.
function projectPinnedEntry(entry) {
  if (entry && entry.kind === 'folder') {
    return (entry.tabs || []).map(projectRow);
  }
  return [projectRow(entry)];
}

function projectSpace(space) {
  return {
    name: space.name,
    overview: !!space.overview,
    tabs: (space.tabs || []).map(projectRow),
    pinned: (space.pinned || []).flatMap(projectPinnedEntry),
    folders: (space.folders || []).map(projectFolder),
    favorites: (space.favorites || []).map(projectRow),
    pinnedCollapsed: !!space.pinnedCollapsed,
  };
}

/** The snapshot reduced to what a restore has to put back. */
export function projectSnapshot(snapshot) {
  return {
    activeSpace: snapshot ? snapshot.activeSpace : -1,
    spaces: ((snapshot && snapshot.spaces) || []).map(projectSpace),
  };
}

/** A stable string for that projection: equal digests mean nothing moved. */
export function structuralDigest(snapshot) {
  return JSON.stringify(projectSnapshot(snapshot));
}

/**
 * `log` with this snapshot appended, or `log` itself when the snapshot is
 * structurally the moment already on top.
 *
 * Returns a new array so a caller can hold the old ring - the view compares
 * identity to decide whether an open scrubber needs a new maximum.
 */
export function captureMoment(log, snapshot, at) {
  const ring = log || [];
  const digest = structuralDigest(snapshot);
  const top = ring[ring.length - 1];
  if (top && top.digest === digest) {
    return ring;
  }
  const grown = ring.concat([{at, digest, state: projectSnapshot(snapshot)}]);
  return grown.length > MAX_MOMENTS ? grown.slice(grown.length - MAX_MOMENTS) : grown;
}

/**
 * The scope a freshly opened scrubber starts on: the space you are in, because
 * putting one room back is the common case. The Overview is not a room, so
 * from there the whole window is the only honest default.
 */
export function defaultScope(snapshot) {
  const spaces = (snapshot && snapshot.spaces) || [];
  const active = snapshot ? snapshot.activeSpace : -1;
  const space = spaces[active];
  if (!space || space.overview) {
    return SCOPE_WINDOW;
  }
  return String(active);
}

/** The scope picker's rows: the whole window, then every real space. */
export function scopeChoices(snapshot) {
  const spaces = (snapshot && snapshot.spaces) || [];
  const choices = [{value: SCOPE_WINDOW, label: 'Whole window'}];
  spaces.forEach((space, index) => {
    if (!space.overview) {
      choices.push({value: String(index), label: space.name});
    }
  });
  return choices;
}

function countTabs(space) {
  if (!space) {
    return 0;
  }
  const folders = (space.folders || [])
      .reduce((total, folder) => total + (folder.tabs || []).length, 0);
  return (space.tabs || []).length + (space.pinned || []).length + folders;
}

function clockTime(at) {
  return new Date(at).toLocaleTimeString(
      [], {hour: '2-digit', minute: '2-digit', second: '2-digit'});
}

/**
 * What the scrubber shows before you commit: when the moment was, how many
 * tabs it holds under the chosen scope, and which room that is.
 */
export function momentSummary(moment, scope) {
  if (!moment) {
    return null;
  }
  const state = moment.state;
  const time = clockTime(moment.at);
  if (scope !== SCOPE_WINDOW && scope != null) {
    const space = state.spaces[Number(scope)];
    const tabs = countTabs(space);
    const scopeLabel = space ? space.name : '?';
    return {
      time,
      tabs,
      scopeLabel,
      text: `${time} · ${tabs} tab${tabs === 1 ? '' : 's'} in ${scopeLabel}`,
    };
  }
  const tabs = state.spaces.reduce((total, space) => total + countTabs(space), 0);
  const landing = state.spaces[state.activeSpace];
  const scopeLabel = landing ? landing.name : '?';
  return {
    time,
    tabs,
    scopeLabel,
    text: `${time} · ${tabs} open tabs · in ${scopeLabel}`,
  };
}

// The active pane reads the snapshot's TOP-LEVEL rows, not its own space entry
// (see build/sidebar-render-test.mjs), so a restore that only rewrote the
// spaces would leave the pane you are looking at showing the old contents.
function mirrorActiveSpace(snapshot) {
  const space = snapshot.spaces[snapshot.activeSpace];
  if (!space) {
    return snapshot;
  }
  return {
    ...snapshot,
    spaceName: space.name,
    tabs: space.tabs,
    pinned: space.pinned,
    folders: space.folders,
    favorites: space.favorites,
    pinnedCollapsed: space.pinnedCollapsed,
  };
}

function restoreSpace(current, saved) {
  return {
    ...current,
    name: saved.name,
    tabs: copy(saved.tabs),
    pinned: copy(saved.pinned),
    folders: copy(saved.folders),
    favorites: copy(saved.favorites),
    pinnedCollapsed: saved.pinnedCollapsed,
  };
}

/**
 * The snapshot as it would look after restoring `moment` under `scope`, or
 * null when the restore cannot be honoured.
 *
 * Whole window: every space, the selection and the space you were in go back.
 * One space: only that space's slice moves, every other room is left exactly as
 * the user has it now, and you land in the room you just rewound - a restore
 * you cannot see is the one that reads as a bug.
 *
 * A per-space restore of a room the moment never saw returns null. Treating
 * "absent" as "empty" would delete a space the user made since, which is the
 * one outcome Rewind must never produce.
 */
export function restoreSnapshot(moment, current, scope) {
  if (!moment || !moment.state || !current) {
    return null;
  }
  const saved = moment.state;
  const perSpace = scope !== SCOPE_WINDOW && scope != null;
  if (perSpace) {
    const index = Number(scope);
    const savedSpace = saved.spaces[index];
    const currentSpace = current.spaces && current.spaces[index];
    if (!savedSpace || !currentSpace) {
      return null;
    }
    const spaces = current.spaces.map(
        (space, at) => (at === index ? restoreSpace(space, savedSpace) : space));
    return mirrorActiveSpace({...current, spaces, activeSpace: index});
  }
  const spaces = (current.spaces || []).map((space, at) => {
    const savedSpace = saved.spaces[at];
    return savedSpace ? restoreSpace(space, savedSpace) : space;
  });
  const activeSpace =
      spaces[saved.activeSpace] ? saved.activeSpace : current.activeSpace;
  return mirrorActiveSpace({...current, spaces, activeSpace});
}

// projectRow/projectFolder already match restore.rs's wire Row/Folder shape
// field-for-field, so a saved space only needs its sidebar `index` added.
function sliceOf(index, savedSpace) {
  return {
    index,
    name: savedSpace.name,
    pinned: savedSpace.pinned,
    tabs: savedSpace.tabs,
    folders: savedSpace.folders,
    favorites: savedSpace.favorites,
    pinnedCollapsed: savedSpace.pinnedCollapsed,
  };
}

// Every other space is ABSENT from spaces[], not merely unchanged.
// restore.rs only touches the indices a slice names, so omission - not an
// equal value - is what keeps the rest of the window untouched.
export function restorePayload(moment, current, scope) {
  const restored = restoreSnapshot(moment, current, scope);
  if (!restored) {
    return null;
  }
  const saved = moment.state;
  const perSpace = scope !== SCOPE_WINDOW && scope != null;
  const indices = perSpace ?
      [Number(scope)] :
      (current.spaces || [])
          .map((_, at) => at)
          .filter((at) => saved.spaces[at]);
  return {
    scope: perSpace ? String(Number(scope)) : SCOPE_WINDOW,
    activeSpace: restored.activeSpace,
    spaces: indices.map((index) => sliceOf(index, saved.spaces[index])),
  };
}
