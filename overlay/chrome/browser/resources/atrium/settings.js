// Copyright 2026 The Atrium Authors
//
// The Settings route. The Swift settings window went with the ABI
// and this replaces it.
//
// THIS FILE IS THE SHELL ONLY: the pane list, the selection, and
// the two messages. What each pane CONTAINS is separate, and reads its
// values from the Rust core rather than keeping a second copy of the settings
// state.
//
// Built against DOM APIs rather than markup strings. Pane titles are ours
// today, but the WebUI CSP forbids innerHTML anyway (no
// innerHTML, no Trusted Types policy), and a surface that will grow fields
// holding user data should not start by learning the wrong habit.

import {NOTIFICATION_TIMEOUT_STEPS, migrateNotificationTimeoutStep} from
    './notification-timeout.js';
import {renderEnginesPane, shippableEngines} from './settings-engines.js';
import {renderRoutingPane, shippableRules} from './settings-routing.js';
import {renderImportPane} from './settings-import.js';
import {spaceGlyph} from './space-glyph.js';
import {mountThemeStudio} from './theme-studio.js';
import {installVimSurface, yieldsToEditableOnly} from './vim-surface.js';
import {TRANSLATE_LANGUAGES, languageLabel} from './translate-languages.js';

// The panes, in the order the rail shows them. `id` travels to the browser, so
// it is a stable name rather than an index - a rail reordered later must not
// change what a selection means.
//
// The first five and their order come from the lab, which is the
// approved design: docs/lab/swipe-lab.html, renderSettings. Routing is sixth
// and has no lab counterpart, on purpose.
//
// `inputs` rather than `shortcuts` matters: the lab MERGED the old shortcuts
// and swipe sections into one, and its own code still rewrites either id to
// 'inputs' on the way in. Keeping two panes here would re-open a split the
// design closed.
export const PANES = [
  {id: 'general', title: 'General'},
  {id: 'appearance', title: 'Appearance'},
  {id: 'spaces', title: 'Spaces'},
  // Right after spaces: a space names a profile, so the two
  // panes read together.
  {id: 'profiles', title: 'Profiles'},
  {id: 'engines', title: 'Search engines'},
  {id: 'ai', title: 'AI'},
  {id: 'inputs', title: 'Inputs'},
  // Routing has no lab counterpart (on purpose).
  {id: 'routing', title: 'Routing'},
  {id: 'import', title: 'Import'},
  // Last on purpose: the six above are the approved set
  // and their order is the lab's. About is not a settings pane at all - it
  // carries the attribution Chromium's BSD-3 terms require a binary to
  // reproduce, and it has no lab counterpart to be placed against.
  {id: 'about', title: 'About'},
];

// Drawn icons for the rail (variant A), one shape list per pane id, built
// node by node rather than from a markup string - the WebUI CSP forbids
// innerHTML (see the file header). Kept local: icons.js's own set has
// nothing shaped like a gear, a 4-square grid or a keyboard.
export const SVG_NS = 'http://www.w3.org/2000/svg';

const RAIL_ICON_SHAPES = {
  general: [
    {tag: 'circle', cx: 8, cy: 8, r: 2.6},
    {tag: 'path', d: 'M8 1.5v2M8 12.5v2M1.5 8h2M12.5 8h2M3.4 3.4l1.4 1.4' +
         'M11.2 11.2l1.4 1.4M12.6 3.4l-1.4 1.4M4.8 11.2l-1.4 1.4'},
  ],
  appearance: [
    {tag: 'circle', cx: 8, cy: 8, r: 6},
    {tag: 'path', d: 'M8 2a6 6 0 0 0 0 12Z', fill: 'currentColor', stroke: 'none'},
  ],
  spaces: [
    {tag: 'rect', x: 2, y: 2.5, width: 5, height: 5, rx: 1.4},
    {tag: 'rect', x: 9, y: 2.5, width: 5, height: 5, rx: 1.4},
    {tag: 'rect', x: 2, y: 9, width: 5, height: 5, rx: 1.4},
    {tag: 'rect', x: 9, y: 9, width: 5, height: 5, rx: 1.4},
  ],
  profiles: [
    {tag: 'circle', cx: 8, cy: 5.4, r: 2.6},
    {tag: 'path', d: 'M3 14c0-3 2.2-5 5-5s5 2 5 5'},
  ],
  engines: [
    {tag: 'circle', cx: 7, cy: 7, r: 4.5},
    {tag: 'path', d: 'm10.5 10.5 3 3'},
  ],
  ai: [
    {tag: 'path', d: 'M8 1.8v12.4M1.8 8h12.4M3.6 3.6l8.8 8.8M12.4 3.6l-8.8 8.8'},
  ],
  inputs: [
    {tag: 'rect', x: 1.8, y: 4, width: 12.4, height: 8, rx: 2},
    {tag: 'path', d: 'M4.5 7h.01M7 7h.01M9.5 7h.01M5 9.5h6'},
  ],
  routing: [
    {tag: 'path', d: 'M2 5h7l-2-2M14 11H7l2 2'},
  ],
  import: [
    {tag: 'path', d: 'M8 2v8M4.8 6.8 8 10l3.2-3.2M2.5 11v2.5h11V11'},
  ],
  about: [
    {tag: 'circle', cx: 8, cy: 8, r: 6},
    {tag: 'path', d: 'M8 7.2v4M8 5h.01'},
  ],
};

// docs/lab/settings-lab.html's PIN constant.
const PIN_ICON_SHAPES = [
  {tag: 'path', d: 'M5.5 2h5M7 2v4.5L5 8.2c-.4.3-.6.8-.6 1.3h7.2c0-.5-.2-1' +
       '-.6-1.3L9 6.5V2'},
  {tag: 'path', d: 'M8 10.5V14'},
];

export function buildIcon(shapes) {
  const svg = document.createElementNS(SVG_NS, 'svg');
  svg.setAttribute('viewBox', '0 0 16 16');
  svg.setAttribute('aria-hidden', 'true');
  svg.classList.add('ov-icon');
  for (const shape of shapes) {
    const {tag, ...attrs} = shape;
    const node = document.createElementNS(SVG_NS, tag);
    for (const [key, val] of Object.entries(attrs)) {
      node.setAttribute(key, val);
    }
    svg.appendChild(node);
  }
  return svg;
}

// The ids the lab retired. A stored or in-flight selection naming one resolves
// to the pane that absorbed it rather than falling back to General, which is
// what paneOrDefault would otherwise do.
const MERGED_INTO_INPUTS = ['shortcuts', 'swipe'];

export function isKnownPane(id) {
  return PANES.some((pane) => pane.id === id);
}

// The pane a fresh open lands on, and the fallback for a name this build does
// not know. A stale id has to resolve to something rather than an empty frame.
export function paneOrDefault(id) {
  if (isKnownPane(id)) {
    return id;
  }
  if (MERGED_INTO_INPUTS.includes(id)) {
    return 'inputs';
  }
  return PANES[0].id;
}

// rust/atrium-core/src/settings.rs's MEMORY_SAVER_THRESHOLDS - "never" is the
// retired spelling (RETIRED_MEMORY_SAVER_NEVER) and does not belong here.
const MEMORY_SAVER_THRESHOLDS =
    ['fifteenMinutes', 'thirtyMinutes', 'oneHour', 'twoHours', 'fourHours'];

// rust/atrium-core/src/choices.rs's FAVORITE_KEEP_ALIVE_THRESHOLDS.
const FAVORITE_KEEP_ALIVE_THRESHOLDS =
    ['oneMinute', 'fiveMinutes', 'fifteenMinutes', 'thirtyMinutes', 'oneHour'];

// rust/atrium-core/src/settings.rs's STARTUP_BEHAVIORS.
const STARTUP_BEHAVIORS = ['restoreLastActive', 'blankTab'];

// rust/atrium-core/src/settings.rs's AUTO_MINI_PLAYER_MODES.
const AUTO_MINI_PLAYER_MODES = ['off', 'siteAsks', 'audible', 'anyVideo'];

// rust/atrium-core/src/settings.rs's CONTROLLER_VISIBILITY_MODES.
const CONTROLLER_VISIBILITY_MODES = ['background', 'always'];

// rust/atrium-core/src/settings.rs's SWIPE_HAPTICS_MODES.
const SWIPE_HAPTICS_MODES = ['off', 'crisp', 'smooth'];

// Slugs match overlay.css's html.atrium-theme-* classes, pinned to the brand
// book by overlay-themes-test.mjs. 'system' is the exception - no class, and
// last so a stored 'brand' still lands on Crimson, the first option.
const OVERLAY_ACCENT_CHOICES = [
  'crimson', 'vermilion', 'magenta', 'ultraviolet', 'cyan', 'lime', 'amber',
  'tokyo-night', 'catppuccin', 'gruvbox', 'kanagawa', 'synthwave-everything',
  'bluloco', 'monokai-pro', 'dracula', 'nord', 'rose-pine', 'everforest',
  'solarized', 'system',
];

// rust/atrium-core/src/choices.rs's AppIconVariant, one slug per file in
// build/icons. Hardcoded because the browser serves the PNGs but has no list
// to push, so settings-test.mjs pins this against the directory instead.
export const APP_ICON_VARIANTS = [
  'auto',
  'crimson-paper',
  'crimson-ink',
  'amber-accent',
  'bluloco-accent',
  'catppuccin-accent',
  'crimson-accent',
  'cyan-accent',
  'dracula-accent',
  'everforest-accent',
  'gruvbox-accent',
  'kanagawa-accent',
  'lime-accent',
  'magenta-accent',
  'monokai-pro-accent',
  'nord-accent',
  'rose-pine-accent',
  'solarized-accent',
  'synthwave-everything-accent',
  'tokyo-night-accent',
  'ultraviolet-accent',
  'vermilion-accent',
];

// The Model row's own choices, and the label each shows.
// Ollama and Custom endpoint feed the same rows they always did - this only
// decides which of those rows are on screen at once.
const TRANSLATE_LANGUAGE_LABELS = Object.fromEntries(
    TRANSLATE_LANGUAGES.map((tag) => [tag, languageLabel(tag)]));

const AI_MODELS = ['onDevice', 'ollama', 'customEndpoint'];
const AI_MODEL_LABELS = {
  onDevice: 'On-device (Apple Intelligence)',
  ollama: 'Ollama',
  customEndpoint: 'Custom endpoint',
};

// Which PANE_ROWS.ai key belongs to which model, so the render loop can skip
// a row that is not the selected model's own.
const AI_MODEL_ROW_OWNER = {
  ollamaHost: 'ollama',
  ollamaPort: 'ollama',
  ollamaModel: 'ollama',
  customEndpointUrl: 'customEndpoint',
  customEndpointModel: 'customEndpoint',
  customEndpointMaxTokens: 'customEndpoint',
  customEndpointTimeoutSeconds: 'customEndpoint',
};

// The rows each pane shows, in the lab's order (docs/lab/swipe-lab.html,
// renderSettings). Labels and hints are the lab's own words.
//
// `key` names the field in the Rust core's settings store. A row with no key
// has no backend yet and says so on screen (NOT_WIRED) - the rule is
// that such a row is built rather than omitted, so the whole surface can be
// reviewed, and it must not look like a setting that works.
//
// A row whose key names a field NOTHING reads is a third thing and does not
// belong here at all: the ones that were in that
// state are wired, and this list must not grow another without a consumer.
//
// Spaces, Profiles and Inputs are absent on purpose - none of the three is a
// row list (the first two are master-detail editors, the third is built from
// the hotkey table and the swipe presets).
export const PANE_ROWS = {
  general: [
    // `special` routes these two to their own renderers: neither is a store
    // key. Hide sidebar is the window's live state, Default browser the OS's.
    {label: 'Hide sidebar', key: null, special: 'hideSidebar',
     hint: 'The page takes the full window; hover the left edge to peek'},
    {label: 'Sidebar position', key: 'sidebarPosition',
     choices: ['left', 'right'],
     hint: 'The whole chrome mirrors - edge gestures and the Shelf drop zone too'},
    {label: 'Restore tabs on launch', key: 'startupBehavior',
     choices: STARTUP_BEHAVIORS,
     hint: "Reopen every space's tabs"},
    {label: 'Hibernate background tabs', key: 'memorySaverEnabled',
     toggle: true, hint: 'Idle tabs suspend to free memory'},
    {label: 'Hibernation timeout', key: 'memorySaverThreshold',
     choices: MEMORY_SAVER_THRESHOLDS,
     hint: 'Idle time before a background tab suspends'},
    {label: 'Keep closed favourites loaded', key: 'favoriteKeepAliveEnabled',
     toggle: true, hint: 'Reopen a favourite instantly after you close it'},
    {label: 'Keep-alive time', key: 'favoriteKeepAliveThreshold',
     choices: FAVORITE_KEEP_ALIVE_THRESHOLDS,
     hint: 'How long a closed favourite stays loaded'},
    {label: 'Notification timeout', key: 'notificationTimeout',
     choices: NOTIFICATION_TIMEOUT_STEPS,
     hint: 'Keep leaves them until dismissed'},
    {label: 'Warn before quitting', key: 'confirmToQuitEnabled', toggle: true,
     hint: '\u2318Q asks first'},
    {label: 'Little window for external links',
     key: 'externalLinksOpenInLittle', toggle: true,
     hint: 'A link handed over by another app opens on its own, not in a space'},
    {label: 'Auto mini player', key: 'autoMiniPlayer',
     choices: AUTO_MINI_PLAYER_MODES,
     hint: 'Which playing videos pop out to a mini player on a tab switch'},
    {label: 'Sidebar media controller', key: 'controllerVisibility',
     choices: CONTROLLER_VISIBILITY_MODES,
     hint: "Background only shows it for a tab you've switched away from"},
    {label: 'Default browser', key: null, special: 'defaultBrowser', hint: ''},
  ],
  inputs: [
    // `special` routes this to renderSwipeFeelRow instead of the generic
    // control a keyed row gets - three preset buttons, not one control.
    {label: 'Swipe feel', key: null, special: 'swipeFeel',
     hint: 'Default is the tuned baseline · Quick snaps · Cozy floats'},
    {label: 'Swipe haptics', key: 'swipeHaptics', choices: SWIPE_HAPTICS_MODES,
     hint: 'Trackpad taps as a space swipe moves · Smooth also taps the slide'},
    {label: 'Vim mode', key: 'vimModeEnabled', toggle: true,
     hint: 'Use j k d u gg G to scroll pages'},
  ],
  // Appearance (light/dark/auto) and Native tint strength are gone, not marked
  //. The others have no store YET, which a later store can give them. These two have no field anywhere to give - the themes store
  // holds colours and not the mode, and the space tint lives per space in
  // spaces.json. A row nothing will ever fill is worse than an absent one.
  appearance: [
    {label: 'Page tint', key: 'chromePageTint', toggle: true,
     hint: 'Drift the window colour toward the page in front'},
    {label: 'Native app view', key: 'opaqueSurround', toggle: true,
     hint: 'System neutral chrome instead of tinted glass'},
    {label: 'Border width', key: 'borderWidth',
     number: {min: 4, max: 10, step: 1, slider: true},
     hint: 'How much tinted backdrop shows around the page'},
    {label: 'Accent', key: 'overlayAccent',
     choices: OVERLAY_ACCENT_CHOICES,
     hint: 'The palette every overlay surface wears · System follows macOS, ' +
         'where Multicolor is blue'},
    {label: 'Glass opacity', key: 'glassOpacity',
     number: {min: 0.02, max: 1, step: 0.01, slider: true},
     hint: 'How much of the desktop shows through'},
    // Both modules already composed every state and were only
    // ever handed a pinned default, so these rows are the state reaching them
    // rather than new rendering.
    {label: 'Night Shift', key: 'skylightNightShift', toggle: true,
     hint: 'The backdrop follows the hour of the day'},
    {label: 'Weather', key: 'skylightWeather',
     choices: ['clear', 'cloudy', 'rain', 'snow'],
     hint: 'Drifts over the backdrop'},
    // `special` routes this to renderAppIconRow instead of the <select> a
    // choice row gets - 22 tiles, and the tile IS the label.
    {label: 'App icon', key: 'appIconVariant', special: 'appIcon',
     choices: APP_ICON_VARIANTS,
     hint: 'The Dock changes now · Finder and ⌘Tab follow a relaunch'},
  ],
  // The kill switch and Ollama host and port
  // have a store. The rest do not yet - each unwired row says so rather than
  // reading as a setting that is off.
  ai: [
    {label: 'Enable AI features', key: 'aiEnabled', toggle: true,
     hint: 'The kill switch - off means nothing AI-shaped runs or phones home ' +
         '· On, open the AI panel with ⇧⌘I or the command bar\'s Toggle AI panel'},
    // `special` routes this to renderAiModelRow instead of the generic
    // control a keyed row gets - the availability hint needs its own layout.
    {label: 'Model', key: 'aiModel', special: 'aiModel', choices: AI_MODELS,
     hint: "On-device and Ollama are free - your hardware; Hosted and Custom endpoint are the Pro tier"},
    {label: 'Auto summarize', key: 'autoSummarize', toggle: true,
     hint: 'Summarize the page when the AI panel opens'},
    {label: 'Translation language', key: 'translateLanguage',
     choices: TRANSLATE_LANGUAGES,
     hint: 'What the panel’s summaries answer in - changeable there too'},
    // Not in the Advanced card: that one is gated to customEndpoint by
    // AI_MODEL_ROW_OWNER, and this cap applies to every backend.
    {label: 'Page text cap', key: 'aiPageTextCap',
     number: {min: 1000, max: 1000000, step: 1000},
     hint: 'Characters of the page the model sees, counted across every tab ' +
         'a pinned thread asks about'},
    {label: 'Ollama host', key: 'ollamaHost', text: true,
     hint: 'Any machine you own - stays free'},
    {label: 'Ollama port', key: 'ollamaPort',
     number: {min: 1, max: 65535, step: 1}, hint: 'Default 11434'},
    {label: 'Ollama model', key: 'ollamaModel', text: true,
     hint: 'A tag you have pulled locally, e.g. llama3'},
    {label: 'Endpoint', key: 'customEndpointUrl', text: true,
     hint: 'https:// only - your own Anthropic or OpenAI-compatible URL'},
    {label: 'Custom endpoint model', key: 'customEndpointModel', text: true,
     hint: 'The model name your endpoint expects, e.g. gpt-4o-mini'},
    {label: 'Response length cap', key: 'customEndpointMaxTokens',
     number: {min: 0, max: 200000, step: 1},
     hint: '0 sends no cap and lets the model decide where to stop'},
    {label: 'Request timeout', key: 'customEndpointTimeoutSeconds',
     number: {min: 0, max: 600, step: 1},
     hint: 'Seconds of silence before giving up, not total time - 0 for the ' +
         'built-in deadline'},
  ],
};

// Which titled card a row lands in (a pane reads as sections rather
// than a list). A row is named by its `key`, or by its `label` when it has
// none (Hide sidebar, Default browser). Kept apart from PANE_ROWS itself:
// grouping is a layout decision, not a change to what a pane contains.
// 'ollamaConnection', 'customEndpointApiKey' and 'customEndpointConnection'
// have no PANE_ROWS entry - they are the dynamically-appended rows below -
// but a group still names them so they land beside the fields they answer
// for. appearance's four titles are the lab's own (settings-lab.html's ROWS).
const GROUPS = {
  general: [
    {title: 'Window', keys: ['Hide sidebar', 'sidebarPosition', 'startupBehavior']},
    {title: 'Performance', keys: ['memorySaverEnabled', 'memorySaverThreshold',
                                  'favoriteKeepAliveEnabled',
                                  'favoriteKeepAliveThreshold']},
    {title: 'Alerts', keys: ['notificationTimeout',
                              'confirmToQuitEnabled', 'externalLinksOpenInLittle']},
    {title: 'Media', keys: ['autoMiniPlayer', 'controllerVisibility']},
    {title: 'Browser', keys: ['Default browser']},
  ],
  appearance: [
    {title: 'The window', keys: ['chromePageTint', 'opaqueSurround',
                                 'borderWidth']},
    {title: 'Overlays', keys: ['overlayAccent', 'glassOpacity']},
    {title: 'The backdrop', keys: ['skylightNightShift', 'skylightWeather']},
    {title: 'The icon', keys: ['appIconVariant']},
  ],
  ai: [
    {title: 'AI',
     keys: ['aiEnabled', 'aiModel', 'autoSummarize', 'translateLanguage',
            'aiPageTextCap']},
    {title: 'Ollama',
     keys: ['ollamaHost', 'ollamaPort', 'ollamaModel', 'ollamaConnection']},
    {title: 'Custom endpoint',
     keys: ['customEndpointUrl', 'customEndpointModel',
            'customEndpointApiKey', 'customEndpointConnection']},
    // Collapsed: both rows are empty for everyone who has not met a slow or
    // expensive endpoint, and an open card of blank numbers reads as setup
    // left unfinished.
    {title: 'Advanced', collapsed: true,
     keys: ['customEndpointMaxTokens', 'customEndpointTimeoutSeconds']},
  ],
};

// The choice rows short enough for a segmented strip rather than a field -
// the under-three rule the lab and brand book settled on.
const SEGMENTED_CHOICE_ROWS =
    new Set(['sidebarPosition', 'startupBehavior', 'controllerVisibility',
             'swipeHaptics']);

// Kept as its own export because the settings suite pins the General set
// against the lab.
export const GENERAL_ROWS = PANE_ROWS.general;

// The hotkeys the browser last pushed: action name to the combos bound to it.
let hotkeys = {};
// The chords Atrium ships, in the same shape. What makes a row's binding
// readable as the user's own rather than the build's.
let hotkeyShipped = {};
// Every preset by name, and which of them the file says its rows merge over.
let hotkeyPresets = {};
let hotkeyPreset = 'atrium';
let hotkeysSchemaVersion = 2;
let hotkeysPendingRevert = null;

// A document-level capture listener outlives its own row's DOM across a
// re-render, so this is cleared from renderSettings itself, not only from
// inside the listener - the row that armed it may already be gone.
let activeHotkeyCapture = null;

function stopHotkeyCapture() {
  if (!activeHotkeyCapture) {
    return;
  }
  const {listener, reset} = activeHotkeyCapture;
  document.removeEventListener('keydown', listener, true);
  activeHotkeyCapture = null;
  reset();
}

export function setHotkeys(next, defaults, presets) {
  hotkeys = (next && typeof next === 'object' && next.actions) || {};
  hotkeyShipped =
      (defaults && typeof defaults === 'object' && defaults.actions) || {};
  hotkeyPresets = (presets && typeof presets === 'object') ? presets : {};
  hotkeyPreset =
      (next && typeof next.preset === 'string') ? next.preset : 'atrium';
  hotkeysSchemaVersion =
      (next && typeof next.schemaVersion === 'number') ? next.schemaVersion : 2;
  if (rootEl) {
    renderSettings(rootEl, currentPane);
  }
}

// A row is measured against the preset the file names, so a reset returns it
// to the chord that preset gives it. `defaults` is what a browser half with
// no presets to push still supplies.
function hotkeyDefaults() {
  return hotkeyPresets[hotkeyPreset] || hotkeyShipped;
}

// An action with no entry among the defaults ships unassigned - createSpace
// does - so an empty row is at its default and a bound one is not. Order is
// not part of it: Record appends, so a re-recorded row keeps the shipped set.
export function hotkeyIsShipped(action, combos) {
  const shipped = hotkeyDefaults()[action] || [];
  if (shipped.length !== combos.length) {
    return false;
  }
  const sorted = [...combos].sort();
  return [...shipped].sort().every((combo, i) => combo === sorted[i]);
}

// Called by the browser with what atrium_core_save_hotkeys returned - the
// same codes atrium_core_save_settings uses.
export function setHotkeysSaveResult(code) {
  if (code === 1) {
    saveError = null;
    if (rootEl) {
      syncSaveErrorBanner(rootEl);
    }
    return;
  }
  saveError = SAVE_FAILURES[code] || 'Atrium could not save that change.';
  if (hotkeysPendingRevert) {
    hotkeys = hotkeysPendingRevert.actions;
    hotkeyPreset = hotkeysPendingRevert.preset;
  }
  hotkeysPendingRevert = null;
  if (rootEl) {
    renderSettings(rootEl, currentPane);
  }
}

// Which combos are bound to more than one action.
//
// The lab surfaces a conflict on BOTH rows rather than only the second, which
// is the useful shape: a user looking at either binding needs to know. Pure,
// so it is tested rather than eyeballed.
export function conflictingCombos(actions) {
  const seen = new Map();
  for (const [action, combos] of Object.entries(actions || {})) {
    for (const combo of combos || []) {
      if (!seen.has(combo)) {
        seen.set(combo, []);
      }
      seen.get(combo).push(action);
    }
  }
  const out = new Map();
  for (const [combo, owners] of seen) {
    if (owners.length > 1) {
      out.set(combo, owners);
    }
  }
  return out;
}

// The spaces document the browser last pushed.
let spaces = [];
let selectedSpace = 0;

// Set only by this window's own "+ New Space" click, and cleared on the very
// next push - a growth another window caused must not steal the selection
// from whatever this window has open.
let spaceCreatedLocally = false;

// The profiles the same document carries - one list, one push.
let profiles = [];
let selectedProfile = 0;

// Same rule as spaceCreatedLocally, for "+ New Profile".
let profileCreatedLocally = false;

// A profile id the browser could not apply the picked engine to right now.
// Cleared on the next pick for that profile.
let profileEngineApplyFailedId = null;

// Called by the browser when a search-engine pick could not reach the
// running profile - the write to disk still happened, only the live
// TemplateURLService did not take it.
export function setProfileEngineApplyFailed(profileId) {
  profileEngineApplyFailedId = profileId;
  if (rootEl) {
    renderSettings(rootEl, currentPane);
  }
}

// A profile id whose data link could not open a window just now. Cleared on
// the next data-link click for that profile.
let profilePageRefusedId = null;

// Called by the browser when a data link found no window to open, and
// could not open one either.
export function setProfilePageRefused(profileId) {
  profilePageRefusedId = profileId;
  if (rootEl) {
    renderSettings(rootEl, currentPane);
  }
}

export function setSpaces(next) {
  const before = spaces.length;
  spaces = (next && Array.isArray(next.spaces)) ? next.spaces : [];
  // Only this window's own click jumps the selection - a push that merely
  // grew the list (another window's create, or any other unrelated push)
  // must leave whatever the user has open alone.
  if (spaceCreatedLocally && spaces.length > before) {
    selectedSpace = spaces.length - 1;
  } else if (selectedSpace >= spaces.length) {
    selectedSpace = 0;
  }
  spaceCreatedLocally = false;

  const profilesBefore = profiles.length;
  profiles = (next && Array.isArray(next.profiles)) ? next.profiles : [];
  // Same rule as selectedSpace above.
  if (profileCreatedLocally && profiles.length > profilesBefore) {
    selectedProfile = profiles.length - 1;
  } else if (selectedProfile >= profiles.length) {
    selectedProfile = 0;
  }
  profileCreatedLocally = false;

  if (rootEl) {
    renderSettings(rootEl, currentPane);
  }
}

// What the detail panel shows for one space. Name, Profile, Search engine
// and Auto-archive are all rendered separately below as live controls.
export function spaceDetailRows(space) {
  if (!space) {
    return [];
  }
  const tabs = (space.tabs || []).length;
  const pinned = (space.pinnedTabs || []).length;
  return [
    {label: 'Tabs', value: `${tabs} open, ${pinned} pinned`},
  ];
}

// The Name row: a real text input over `space.name`, sent back by
// sidebar position, same convention as archiveTtlRow.
export function spaceNameRow(space, spaceIndex) {
  const line = document.createElement('div');
  line.className = 'settings-row';

  const label = document.createElement('span');
  label.className = 'settings-row-label';
  label.textContent = 'Name';

  const value = document.createElement('input');
  value.type = 'text';
  value.className = 'settings-row-value settings-row-text settings-space-name';
  value.value = space.name || '';

  line.appendChild(label);
  line.appendChild(value);
  return line;
}

// A deleted profile still gets an appended option showing the raw id, so the
// picker never silently jumps to another one.
export function spaceProfileRow(space, spaceIndex, profileList) {
  const line = document.createElement('div');
  line.className = 'settings-row';

  const label = document.createElement('span');
  label.className = 'settings-row-label';
  label.textContent = 'Profile';

  const list = profileList || [];
  const current = space.profileID || '';

  const value = document.createElement('select');
  value.className = 'settings-row-value settings-row-choice settings-space-profile';
  for (const profile of list) {
    const option = document.createElement('option');
    option.value = profile.id;
    option.textContent = profile.name;
    value.appendChild(option);
  }
  if (!list.some((p) => p.id === current)) {
    const option = document.createElement('option');
    option.value = current;
    option.textContent = current;
    value.appendChild(option);
  }
  value.value = current;

  line.appendChild(label);
  line.appendChild(value);
  return line;
}

// docs/lab/swipe-lab.html's ARCHIVE_OPTS (line 5142), plus Follow General for
// the absent case - the lab has no global fallback to fall back to.
const ARCHIVE_TTL_CHOICES = [
  {value: '', label: 'Follow General'},
  {value: 'twelveHours', label: '12 hours'},
  {value: 'twentyFourHours', label: '24 hours'},
  {value: 'sevenDays', label: '7 days'},
  {value: 'thirtyDays', label: '30 days'},
  {value: 'never', label: 'Never'},
];

// The Auto-archive row: a real select over `space.archiveTtl`, sent back by
// sidebar position - the pushed list is already in sidebar order.
export function archiveTtlRow(space, spaceIndex) {
  const line = document.createElement('div');
  line.className = 'settings-row';

  const label = document.createElement('span');
  label.className = 'settings-row-label';
  label.textContent = 'Auto-archive';

  const value = document.createElement('select');
  value.className = 'settings-row-value settings-row-choice';
  for (const choice of ARCHIVE_TTL_CHOICES) {
    const option = document.createElement('option');
    option.value = choice.value;
    option.textContent = choice.label;
    value.appendChild(option);
  }
  value.value = space.archiveTtl || '';
  value.addEventListener('change', () => {
    chrome.send('settingsSpaceArchiveTtlChanged', [spaceIndex, value.value]);
  });

  line.appendChild(label);
  line.appendChild(value);
  return line;
}

const HIBERNATE_MODE_CHOICES = [
  {value: '', label: 'Follow General'},
  {value: 'never', label: 'Never'},
  {value: 'always', label: 'Always'},
];

// The same memorySaverThreshold ladder the General pane's own Hibernation
// timeout row uses (MEMORY_SAVER_THRESHOLDS above) - a per-profile override,
// not a third ladder.
const HIBERNATE_THRESHOLD_LABELS = {
  fifteenMinutes: '15 minutes',
  thirtyMinutes: '30 minutes',
  oneHour: '1 hour',
  twoHours: '2 hours',
  fourHours: '4 hours',
};
const HIBERNATE_THRESHOLD_CHOICES = [
  {value: '', label: 'Follow General'},
  ...MEMORY_SAVER_THRESHOLDS.map(
      (t) => ({value: t, label: HIBERNATE_THRESHOLD_LABELS[t]})),
];

function hibernateSelect(choices, current) {
  const select = document.createElement('select');
  select.className = 'settings-row-value settings-row-choice';
  for (const choice of choices) {
    const option = document.createElement('option');
    option.value = choice.value;
    option.textContent = choice.label;
    select.appendChild(option);
  }
  select.value = current || '';
  return select;
}

// An action name as the rail shows it: goToSpace1 reads as 'Go to space 1'.
//
// Acronyms stay whole. Splitting before every capital turned editCurrentURL
// into 'Edit current u r l', so a run of capitals breaks only where the next
// word starts - the last capital of the run belongs to it.
export function actionLabel(action) {
  const spaced = action
      .replace(/([a-z])([A-Z])/g, '$1 $2')
      .replace(/([A-Z]+)([A-Z][a-z])/g, '$1 $2')
      .replace(/([a-zA-Z])([0-9]+)/g, '$1 $2');
  const words = spaced.split(' ');
  return words
      .map((word, i) => {
        // A run of capitals is an acronym and keeps its case.
        if (word.length > 1 && word === word.toUpperCase() && /[A-Z]/.test(word)) {
          return word;
        }
        return i === 0 ? word.charAt(0).toUpperCase() + word.slice(1).toLowerCase()
                       : word.toLowerCase();
      })
      .join(' ');
}

// A stable id for a row with no PANE_ROWS key. buildStandardRow sets the
// same slug on data-row, so a search hit can find its way back to the DOM.
function slugifyLabel(label) {
  return label.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '');
}

// Every row a query can land on: PANE_ROWS across every pane, plus one entry
// per hotkey action run through actionLabel - the Inputs pane has no
// PANE_ROWS list of its own, it builds its rows from the hotkey store.
function buildSearchIndex(actions) {
  const index = [];
  for (const [pane, rows] of Object.entries(PANE_ROWS)) {
    for (const row of rows) {
      // renderSwipeFeelRow gives its row data-key="swipeTuning" even though
      // PANE_ROWS marks it key: null, to route it to the special renderer.
      const key = row.special === 'swipeFeel' ? 'swipeTuning' : (row.key || null);
      index.push({pane, key, action: null, label: row.label, hint: row.hint || ''});
    }
  }
  for (const action of actions) {
    index.push({pane: 'inputs', key: null, action, label: actionLabel(action), hint: ''});
  }
  return index;
}

// Label matches rank above hint matches - a row's own name is a stronger
// signal than a word buried in its hint.
export function searchSettings(query, actions = Object.keys(hotkeys)) {
  const q = query.trim().toLowerCase();
  if (!q) {
    return [];
  }
  const index = buildSearchIndex(actions);
  const labelHits = [];
  const hintHits = [];
  for (const entry of index) {
    if (entry.label.toLowerCase().includes(q)) {
      labelHits.push(entry);
    } else if (entry.hint.toLowerCase().includes(q)) {
      hintHits.push(entry);
    }
  }
  return [...labelHits, ...hintHits];
}

// Where a search hit's row lives in the DOM. An action or a key names it
// directly. Neither (Hide sidebar, Default browser) falls back to the slug
// buildStandardRow puts on data-row.
export function searchResultSelector(hit) {
  if (hit.action) {
    return `.settings-row[data-action="${hit.action}"]`;
  }
  if (hit.key) {
    return `.settings-row[data-key="${hit.key}"]`;
  }
  return `.settings-row[data-row="${slugifyLabel(hit.label)}"]`;
}

const NOTIFICATION_TIMEOUT_LABELS = {
  oneSecond: '1 s',
  threeSeconds: '3 s',
  fiveSeconds: '5 s',
  tenSeconds: '10 s',
  keep: 'Keep',
};

const FAVORITE_KEEP_ALIVE_LABELS = {
  oneMinute: '1 minute',
  fiveMinutes: '5 minutes',
  fifteenMinutes: '15 minutes',
  thirtyMinutes: '30 minutes',
  oneHour: '1 hour',
};

// Lab wording (docs/lab/swipe-lab.html) per row key. An absent key or value
// falls back to actionLabel's camelCase split.
// The brand book's own spellings, which actionLabel's slug split cannot
// reach: it would render Rosé Pine as 'Rose Pine' and Tokyo Night as
// 'Tokyo night'.
const OVERLAY_ACCENT_LABELS = {
  'crimson': 'Crimson', 'vermilion': 'Vermilion', 'magenta': 'Magenta',
  'ultraviolet': 'Ultraviolet', 'cyan': 'Cyan', 'lime': 'Lime',
  'amber': 'Amber', 'tokyo-night': 'Tokyo Night', 'catppuccin': 'Catppuccin',
  'gruvbox': 'Gruvbox', 'kanagawa': 'Kanagawa',
  'synthwave-everything': 'Synthwave Everything', 'bluloco': 'Bluloco',
  'monokai-pro': 'Monokai Pro', 'dracula': 'Dracula', 'nord': 'Nord',
  'rose-pine': 'Rosé Pine', 'everforest': 'Everforest',
  'solarized': 'Solarized', 'system': 'System',
};

const CHOICE_LABELS = {
  sidebarPosition: {left: 'Left', right: 'Right'},
  overlayAccent: OVERLAY_ACCENT_LABELS,
  memorySaverThreshold: HIBERNATE_THRESHOLD_LABELS,
  favoriteKeepAliveThreshold: FAVORITE_KEEP_ALIVE_LABELS,
  notificationTimeout: NOTIFICATION_TIMEOUT_LABELS,
  translateLanguage: TRANSLATE_LANGUAGE_LABELS,
};

export function choiceLabel(key, value) {
  return (CHOICE_LABELS[key] && CHOICE_LABELS[key][value]) || actionLabel(value);
}

// "brand" pinned rgb(176 58 72), which is theme Crimson's own accent, so an
// existing file selects Crimson instead of falling through to whichever option
// happens to be first. AtriumWindowAppearance maps it the same way.
export function normalizeChoice(key, value) {
  if (key === 'overlayAccent' && value === 'brand') {
    return 'crimson';
  }
  if (key === 'notificationTimeout') {
    return migrateNotificationTimeoutStep(value);
  }
  return value;
}

// The settings the browser last pushed. Empty until atriumSetSettings runs,
// which is why a row renders its own placeholder rather than 'undefined'.
let settings = {};

// routing.json, pushed separately from settings - the store the core keeps
// for it is its own file, not a settings.json key.
let routing = {schemaVersion: 2, defaultTargetSpaceID: '', rules: []};
let routingPendingRevert = null;

export function setRoutingRules(next) {
  routing = {
    schemaVersion: (next && typeof next.schemaVersion === 'number') ? next.schemaVersion : 2,
    defaultTargetSpaceID: (next && typeof next.defaultTargetSpaceID === 'string')
        ? next.defaultTargetSpaceID : '',
    rules: (next && Array.isArray(next.rules)) ? next.rules : [],
  };
  if (rootEl) {
    renderSettings(rootEl, currentPane);
  }
}

// The import pane's state. Previews and the last result arrive by push.
const importState = {previews: {}, pending: {}, running: null, result: null};

export function setImportPreview(preview) {
  if (!preview || typeof preview.source !== 'string') {
    return;
  }
  importState.previews[preview.source] = preview;
  importState.pending[preview.source] = false;
  if (rootEl) {
    renderSettings(rootEl, currentPane);
  }
}

export function setImportResult(result) {
  importState.result = {source: importState.running, ...(result || {})};
  importState.running = null;
  // The preview stays: the card and its counts must still be there for the
  // result line to render next to. A fresh push replaces it when it arrives.
  if (rootEl) {
    renderSettings(rootEl, currentPane);
  }
}

// Which model the Model row shows selected, straight off the pushed settings
// - falls back to the same default the store falls back to when
// the field is missing or invalid, so a push in flight never shows a blank.
function currentAiModel(from = settings) {
  return AI_MODELS.includes(from.aiModel) ? from.aiModel : AI_MODELS[0];
}

// Whether `model` can run right now. Only On-device has a hardware
// precondition, reported through `aiOnDeviceAvailable` - 'unknown' until the
// browser's own check answers.
function aiModelAvailability(model, from = settings) {
  if (model !== 'onDevice') {
    return 'available';
  }
  if (from.aiOnDeviceAvailable === true) {
    return 'available';
  }
  if (from.aiOnDeviceAvailable === false) {
    return 'unavailable';
  }
  return 'unknown';
}

// The element installSettings was given. Held rather than looked up by id: the
// host page's root is 'atrium-root', and a getElementById('root') here found
// nothing and silently skipped every repaint - the values arrived and the rows
// kept showing dashes.
let rootEl = null;
let currentPane = null;

// The search field's own state, held here rather than read back from the
// DOM: renderSettings wipes and rebuilds the whole tree on every pane
// switch, and a query typed before that switch must survive it.
let searchQuery = '';
let searchHits = [];
let searchActiveIndex = 0;

// Called by the browser with the Rust core's settings.
export function setSettings(next) {
  settings = next && typeof next === 'object' ? next : {};
  defaultBrowserAsked = false;
  if (rootEl) {
    renderSettings(rootEl, currentPane);
  }
}

// The last failed save, as the sentence the user reads. Null when the last
// attempt worked or there has not been one.
let saveError = null;

// Save id to the keys it changed and their pre-edit values. Keyed by id, not
// a single slot: an older edit failing after a newer one was sent must
// revert only its own keys, not whichever object `settings` holds by then.
const pendingSettingsSaves = new Map();
let nextSettingsSaveId = 1;

// Snapshots the keys `next` changes against the current `settings`, applies
// `next`, and returns a save id for the caller to send with chrome.send.
function beginSettingsSave(next) {
  const id = nextSettingsSaveId++;
  const prev = settings;
  const changedKeys = {};
  for (const key of new Set([...Object.keys(prev), ...Object.keys(next)])) {
    if (prev[key] !== next[key]) {
      changedKeys[key] = prev[key];
    }
  }
  pendingSettingsSaves.set(id, changedKeys);
  settings = next;
  return id;
}

// What a failed save says. The codes are atrium_core_save_settings's own - see
// third_party/atrium/core/include/atrium_core.h.
//
// EVERY failure surfaces, not only -2. The row has already repainted with the
// new value, so a save that fails in silence leaves the user believing an edit
// the file never took.
const SAVE_FAILURES = {
  '0': 'Atrium could not use that change, so nothing was saved.',
  '-1': 'Atrium could not write your settings file. This change is lost when ' +
      'you quit.',
  '-2': 'Your settings file comes from a newer version of Atrium. This change ' +
      'was not saved, and the file was left as it is.',
};

// Called by the browser with what atrium_core_save_settings returned. The row
// already shows the edit, so success only settles the banner. A failure puts
// a value back, on just the row(s) that changed.
export function setSaveResult(code, id) {
  if (code === 1) {
    pendingSettingsSaves.delete(id);
    saveError = null;
    if (rootEl) {
      syncSaveErrorBanner(rootEl);
    }
    return;
  }
  // A code this build does not know still says something: silence is the one
  // answer that misleads.
  saveError = SAVE_FAILURES[code] || 'Atrium could not save that change.';
  const revertKeys = pendingSettingsSaves.get(id);
  pendingSettingsSaves.delete(id);
  // A key a newer, still-outstanding save already changed keeps that value -
  // this save's own snapshot of it predates that edit.
  const supersededKeys = new Set();
  for (const [otherId, otherKeys] of pendingSettingsSaves) {
    if (otherId > id) {
      Object.keys(otherKeys).forEach((key) => supersededKeys.add(key));
    }
  }
  const revertedKeys = revertKeys
      ? Object.keys(revertKeys).filter((key) => !supersededKeys.has(key))
      : [];
  if (revertedKeys.length > 0) {
    settings = {...settings,
      ...Object.fromEntries(revertedKeys.map((key) => [key, revertKeys[key]]))};
  }
  if (!rootEl) {
    return;
  }
  const patched = revertedKeys.length > 0 &&
      revertedKeys.every((key) => updateRowAndCompanions(rootEl, key));
  if (patched) {
    syncSaveErrorBanner(rootEl);
  } else {
    // A full render paints the banner itself - see the credential-bearing
    // branch of setRow for the same reasoning.
    renderSettings(rootEl, currentPane);
  }
}

// Whether the user can change this row in place. Both halves are required: the
// row has to be one this build wired, and the browser has to have pushed a
// bool for it - the core refuses a partial object, so a row showing a dash has
// nothing it could legally send.
export function isToggleable(row, from = settings) {
  return Boolean(row.toggle) && typeof from[row.key] === 'boolean';
}

// Which control shape a row gets, or null for a row this build cannot edit in
// place. A bool is a toggle, a string one of a named set is a choice, a
// number is a number, and any other string is text.
export function controlKind(row, from = settings) {
  if (isToggleable(row, from)) {
    return 'toggle';
  }
  // The tiles are the choice list, so a stored value the list no longer
  // carries (a retired variant) must still let a click pick a new one.
  if (row.special === 'appIcon') {
    return 'choice';
  }
  if (Array.isArray(row.choices) && typeof from[row.key] === 'string' &&
      row.choices.includes(normalizeChoice(row.key, from[row.key]))) {
    return 'choice';
  }
  if (row.number && typeof from[row.key] === 'number') {
    return 'number';
  }
  if (row.text && typeof from[row.key] === 'string') {
    return 'text';
  }
  return null;
}

// What a row with no store at all says. Words rather than the dash
// those rows used to show: a dash is what a row whose value has not ARRIVED
// yet shows, and "the browser has not pushed this" and "this build has nowhere
// to put it" are different states that were reading identically. A pane full
// of dashes reads as broken; this reads as unfinished, which is what it is.
export const NOT_WIRED = 'Not yet wired';

// What a row shows. A keyed row whose value the store does not carry still
// reads as a dash - that one IS waiting for a value.
export function rowValue(row, from = settings) {
  if (!row.key) {
    return NOT_WIRED;
  }
  const value = from[row.key];
  if (value === undefined || value === null) {
    return '\u2014';
  }
  return typeof value === 'boolean' ? (value ? 'On' : 'Off') : String(value);
}

// Which element inside root has focus, as something that survives the rebuild
// below. Returns 'rail' for a rail item - the rail's focus follows
// the SELECTION, so after a pane change it belongs on the newly selected item
// rather than on the one the old focus sat on - a selector for anything else,
// and null when focus is outside root or nowhere.
function focusAnchor(root) {
  const active = document.activeElement;
  if (!active || active === root || !root.contains(active)) {
    return null;
  }
  if (active.classList.contains('settings-rail-item')) {
    return 'rail';
  }
  if (active.classList.contains('settings-search-field')) {
    return '.settings-search-field';
  }
  if (active.classList.contains('settings-pin')) {
    return '.settings-pin';
  }
  const space = active.closest('.settings-space-item');
  if (space) {
    return `.settings-space-item[data-space="${space.dataset.space}"]`;
  }
  const row = active.closest('.settings-row');
  if (row && row.dataset.key) {
    for (const control of ROW_CONTROLS) {
      if (active.classList.contains(control)) {
        return `.settings-row[data-key="${row.dataset.key}"] .${control}`;
      }
    }
  }
  return null;
}

const ROW_CONTROLS = [
  'settings-row-toggle', 'settings-row-choice', 'settings-row-number',
  'settings-row-text', 'settings-slider-range'
];

// A text control commits only on 'change', so focus means its value is not
// yet what the store holds. Space/profile name inputs have no PANE_ROWS
// entry, so they need their own selector rather than row.dataset.key.
function textEditAnchor(root) {
  const active = document.activeElement;
  if (!active || !root.contains(active) ||
      !active.classList.contains('settings-row-text') ||
      // Always empty after a rebuild (see renderCustomEndpointApiKey) - never
      // restore the typed plaintext key back into it.
      active.classList.contains('settings-row-api-key')) {
    return null;
  }
  const key = active.closest('.settings-row')?.dataset.key;
  const selector = active.classList.contains('settings-space-name') ?
      '.settings-space-name' :
      active.classList.contains('settings-profile-name') ?
      '.settings-profile-name' :
      key ? `.settings-row[data-key="${key}"] .settings-row-text` : null;
  if (!selector) {
    return null;
  }
  return {
    selector,
    value: active.value,
    selectionStart: active.selectionStart,
    selectionEnd: active.selectionEnd,
  };
}

// The browser owns this bit - it edits the widget, not
// settings.json, and only the browser knows the z-order the widget took. So a
// click sends and waits, and a reloaded renderer is told the truth.
let alwaysOnTop = false;

// Kept out of `settings` - that object is the whole document the core writes
// back, and a window-only control must not reach settings.json.
export function setSettingsAlwaysOnTop(next) {
  alwaysOnTop = Boolean(next);
  const pin = rootEl && rootEl.querySelector('.settings-pin');
  if (pin) {
    pin.setAttribute('aria-pressed', String(alwaysOnTop));
  } else if (rootEl) {
    renderSettings(rootEl, currentPane);
  }
}

function buildPin() {
  const pin = document.createElement('button');
  pin.className = 'settings-pin';
  pin.appendChild(buildIcon(PIN_ICON_SHAPES));
  pin.title = 'Keep this window above every other window';
  pin.setAttribute('aria-label', 'On top');
  pin.setAttribute('aria-pressed', String(alwaysOnTop));
  return pin;
}

// One row, in whichever control shape its data calls for.
// Shared by the flat loop
// (a pane with no GROUPS entry) and the grouped one below, so a pane gaining
// a group later does not fork this logic a second time.
function buildStandardRow(row) {
  const line = document.createElement('div');
  line.className = 'settings-row';
  line.dataset.key = row.key || '';
  // A key-less row (Hide sidebar, Default browser) needs its own address for
  // search to land on - see searchResultSelector's fallback.
  line.dataset.row = slugifyLabel(row.label);
  // The WHOLE row dims, label and hint with it - see the CSS. Marked on the
  // row rather than the value so a reader who never gets as far as the right
  // column still sees which rows are not there yet.
  if (!row.key) {
    line.classList.add('unwired');
  }
  const label = document.createElement('span');
  label.className = 'settings-row-label';
  label.textContent = row.label;
  if (row.hint) {
    const hint = document.createElement('small');
    hint.className = 'settings-row-hint';
    hint.textContent = row.hint;
    label.appendChild(hint);
  }
  // A native control rather than a styled span, so the row is reachable by
  // keyboard and reads as a control to a screen reader. Which element
  // depends on the row's shape: a toggle is a button, a choice under three is
  // a segmented strip, a choice of three or more is a select styled as a
  // field, a number marked `slider` is a slider, and any other number or text
  // stays a plain input - a static row stays a span.
  const kind = controlKind(row);
  let value;
  if (kind === 'choice' && SEGMENTED_CHOICE_ROWS.has(row.key)) {
    value = buildSegmentedControl(row);
  } else if (kind === 'choice') {
    value = document.createElement('select');
    value.className = 'settings-row-value settings-row-choice';
    for (const choice of row.choices) {
      const option = document.createElement('option');
      option.value = choice;
      option.textContent = choiceLabel(row.key, choice);
      option.selected = choice === normalizeChoice(row.key, settings[row.key]);
      value.appendChild(option);
    }
    // option.selected marks the right <option>, but the select's own
    // .value is what commitControl reads back on change - set it directly
    // rather than relying on option/select linkage to compute it.
    value.value = normalizeChoice(row.key, settings[row.key]);
  } else if (kind === 'number' && row.number.slider) {
    value = buildSliderControl(row);
  } else if (kind === 'number') {
    // type=text, not type=number: Blink's number input does not
    // implement the selection API (select(), setSelectionRange(),
    // selectionStart/End) the HTML spec requires for text selection, so
    // Cmd+A is a silent no-op on it - a user who does not manually clear
    // the field first gets their typed digits appended to whatever was
    // already there. A text input implements that API, so both the
    // browser's native Cmd+A and this file's own focus-select below
    // (commitControl still parses with Number() and rejects NaN, same as
    // before) actually work. min/max/step are dropped with the type - a
    // browser only enforces them for number/range, so they did nothing
    // here as attributes; row.number still gates controlKind() above.
    value = document.createElement('input');
    value.type = 'text';
    value.inputMode = 'decimal';
    value.className = 'settings-row-value settings-row-number';
    value.value = String(settings[row.key]);
  } else if (kind === 'text') {
    value = document.createElement('input');
    value.type = 'text';
    value.className = 'settings-row-value settings-row-text';
    value.value = settings[row.key];
  } else if (kind === 'toggle') {
    value = document.createElement('button');
    value.className = 'settings-row-value settings-row-toggle';
    value.setAttribute('aria-pressed', String(settings[row.key]));
    const knob = document.createElement('span');
    knob.className = 'settings-row-toggle-knob';
    value.appendChild(knob);
    // Carried for a11y and for a caller reading .textContent - the knob's
    // position already says this visually, so brand book 08's switch has no
    // visible label of its own (see the CSS).
    const state = document.createElement('span');
    state.className = 'settings-row-toggle-state';
    state.textContent = rowValue(row);
    value.appendChild(state);
  } else {
    value = document.createElement('span');
    value.className = 'settings-row-value';
    value.textContent = rowValue(row);
  }
  line.appendChild(label);
  line.appendChild(value);
  return line;
}

// The segmented shape for a choice under three (Sidebar position, Restore
// tabs on launch) - two or more fixed-width options and a thumb that slides
// between them, rather than a native <select>'s single-line field.
function buildSegmentedControl(row) {
  const current = normalizeChoice(row.key, settings[row.key]);
  const index = Math.max(0, row.choices.indexOf(current));

  const wrap = document.createElement('div');
  wrap.className = 'settings-row-value settings-seg';
  wrap.setAttribute('role', 'radiogroup');
  // The lab's own formula: two 3px pads plus one 3px gap per extra option.
  wrap.style.setProperty(
      '--w', `calc((100% - ${3 * (row.choices.length + 1)}px) / ${row.choices.length})`);
  wrap.style.setProperty('--i', String(index));

  const thumb = document.createElement('span');
  thumb.className = 'settings-seg-thumb';
  wrap.appendChild(thumb);

  for (const choice of row.choices) {
    // A button, not a styled span - .settings-app-icon and
    // .settings-swipe-preset are the same family and are both real controls,
    // reachable without a mouse.
    const option = document.createElement('button');
    option.className = 'settings-seg-option';
    option.dataset.choice = choice;
    option.setAttribute('role', 'radio');
    option.setAttribute('aria-checked', String(choice === current));
    option.textContent = choiceLabel(row.key, choice);
    option.classList.toggle('on', choice === current);
    wrap.appendChild(option);
  }
  return wrap;
}

// num precedes range in the DOM so the existing scenarios' own
// `.settings-row[data-key="..."] input` query still lands on the typeable
// field. settings.css's `order` puts range back on the left visually.
function buildSliderControl(row) {
  const wrap = document.createElement('span');
  wrap.className = 'settings-slider';

  const num = document.createElement('input');
  num.type = 'text';
  num.inputMode = 'decimal';
  num.className = 'settings-row-value settings-row-number settings-slider-num';
  num.value = String(settings[row.key]);

  const range = document.createElement('input');
  range.type = 'range';
  range.className = 'settings-slider-range';
  range.min = String(row.number.min);
  range.max = String(row.number.max);
  range.step = String(row.number.step);
  range.value = String(settings[row.key]);
  range.setAttribute('aria-label', row.label);

  wrap.appendChild(num);
  wrap.appendChild(range);
  paintSliderFill(range);
  return wrap;
}

// The fill gradient's stop, as a CSS variable so ::-webkit-slider-runnable-track
// (settings.css) can read it - a drag or a keyboard step repaints this without
// touching the DOM.
function paintSliderFill(range) {
  const min = Number(range.min);
  const pct = ((Number(range.value) - min) / (Number(range.max) - min)) * 100;
  range.style.setProperty('--fill', `${pct}%`);
}

// aiEnabled can add or remove OTHER rows, even empty a whole card (see
// renderGroupedRows), so it needs the full-render fallback below. aiModel
// needs the same fallback but no entry - row.special already returns null.
const ROW_UPDATE_NEEDS_FULL_RENDER = new Set(['aiEnabled']);

// Rows with no PANE_ROWS entry - built by their own function, straight onto
// a container, rather than from a row definition (see renderGroupedRows).
const SYNTHETIC_ROW_RENDERERS = {
  ollamaConnection: renderOllamaConnection,
  customEndpointApiKey: renderCustomEndpointApiKey,
  customEndpointConnection: renderCustomEndpointConnection,
};

// ollamaConnection's label embeds the host and port live, without a save
// - editing either has to refresh it too, or it goes
// stale while updateRowInPlace only repaints the row that was actually edited.
const ROW_COMPANIONS = {
  ollamaHost: ['ollamaConnection'],
  ollamaPort: ['ollamaConnection'],
  customEndpointUrl: ['customEndpointApiKey'],
};

// One row's fresh DOM, or null when `key` needs updateRowInPlace's fallback.
function buildRowNode(key) {
  if (ROW_UPDATE_NEEDS_FULL_RENDER.has(key)) {
    return null;
  }
  const row = (PANE_ROWS[currentPane] || []).find((r) => r.key === key);
  if (row) {
    if (row.special === 'appIcon') {
      const temp = document.createElement('div');
      renderAppIconRow(temp);
      return temp.firstElementChild;
    }
    return row.special ? null : buildStandardRow(row);
  }
  const synthetic = SYNTHETIC_ROW_RENDERERS[key];
  if (!synthetic) {
    return null;
  }
  const temp = document.createElement('div');
  synthetic(temp);
  return temp.firstElementChild;
}

// Swaps one row's DOM for a fresh copy, leaving the rail, the heading and
// every other row untouched. False means the caller must fall
// back to a full pane render instead.
function updateRowInPlace(root, key) {
  const existing = root.querySelector(`.settings-row[data-key="${key}"]`);
  if (!existing) {
    return false;
  }
  const replacement = buildRowNode(key);
  if (!replacement) {
    return false;
  }
  const focusedControl = existing.contains(document.activeElement) ?
      ROW_CONTROLS.find((c) => document.activeElement.classList.contains(c)) :
      undefined;
  existing.replaceWith(replacement);
  if (focusedControl) {
    replacement.querySelector(`.${focusedControl}`)?.focus();
  }
  return true;
}

// updateRowInPlace for `key`, plus any other row its own value feeds
// (ROW_COMPANIONS) - a companion missing from the DOM is not a failure,
// there is nothing stale on screen to fix.
function updateRowAndCompanions(root, key) {
  if (!updateRowInPlace(root, key)) {
    return false;
  }
  for (const companion of ROW_COMPANIONS[key] || []) {
    updateRowInPlace(root, companion);
  }
  return true;
}

// The alert is always the pane content's first child after the heading
// (renderSettings), on every pane - so keeping it in step with `saveError`
// only ever needs that one fixed position, never a rebuild.
function syncSaveErrorBanner(root) {
  const content = root.querySelector('.settings-pane-content');
  if (!content) {
    return;
  }
  const existing = content.querySelector('.settings-alert');
  if (!saveError) {
    existing?.remove();
    return;
  }
  if (existing) {
    existing.textContent = saveError;
    return;
  }
  const alert = document.createElement('div');
  alert.className = 'settings-alert';
  alert.setAttribute('role', 'alert');
  alert.textContent = saveError;
  content.insertBefore(alert, content.children[1] || null);
}

// Which titled cards a pane is grouped into, and one row (or the dynamically
// appended AI extras) per key inside each - a pane reading as sections
// rather than one flat ladder. A group that ends up with nothing to show for
// the current selection (e.g. Ollama's card while Custom endpoint is picked)
// renders neither its title nor an empty card.
function renderGroupedRows(body, paneId) {
  for (const group of GROUPS[paneId]) {
    const card = document.createElement('div');
    card.className = 'settings-group';
    for (const key of group.keys) {
      const row = (PANE_ROWS[paneId] || []).find((r) => (r.key || r.label) === key);
      if (row) {
        if (row.special === 'aiModel') {
          renderAiModelRow(card);
        } else if (row.special === 'appIcon') {
          renderAppIconRow(card);
        } else if (row.special === 'hideSidebar') {
          renderHideSidebarRow(card);
        } else if (row.special === 'defaultBrowser') {
          renderDefaultBrowserRow(card);
        } else if (paneId === 'ai' && AI_MODEL_ROW_OWNER[row.key] &&
                   AI_MODEL_ROW_OWNER[row.key] !== currentAiModel()) {
          // Not this model's own row.
        } else {
          card.appendChild(buildStandardRow(row));
        }
        continue;
      }
      if (paneId !== 'ai' || settings.aiEnabled === false) {
        continue;
      }
      // !== false, not === true: undefined (before the first push) must
      // default on, same as every other row.
      if (key === 'ollamaConnection' && currentAiModel() === 'ollama' &&
          typeof settings.ollamaHost === 'string' &&
          typeof settings.ollamaPort === 'number') {
        renderOllamaConnection(card);
      } else if (key === 'customEndpointApiKey' &&
                 currentAiModel() === 'customEndpoint' &&
                 typeof settings.customEndpointUrl === 'string') {
        renderCustomEndpointApiKey(card);
      } else if (key === 'customEndpointConnection' &&
                 currentAiModel() === 'customEndpoint' &&
                 typeof settings.customEndpointUrl === 'string') {
        renderCustomEndpointConnection(card);
      }
    }
    if (card.children.length === 0) {
      continue;
    }
    // <details> rather than a class we toggle: it starts closed, takes the
    // keyboard and the screen reader for free, and forgets it was open on the
    // next render, which is what a rarely-touched card should do.
    if (group.collapsed) {
      const details = document.createElement('details');
      details.className = 'settings-group-collapsible';
      const summary = document.createElement('summary');
      summary.className = 'settings-group-title';
      summary.textContent = group.title;
      details.appendChild(summary);
      details.appendChild(card);
      body.appendChild(details);
      continue;
    }
    const title = document.createElement('div');
    title.className = 'settings-group-title';
    title.textContent = group.title;
    body.appendChild(title);
    body.appendChild(card);
  }
}

// One result row under the search field: the label it matched, and which
// pane it lands on.
function buildSearchResult(hit, index, isActive) {
  const item = document.createElement('button');
  item.type = 'button';
  item.className = 'settings-search-result' + (isActive ? ' active' : '');
  item.dataset.index = String(index);
  // Reachable only through the field's own arrow keys (see installSettings),
  // not a second Tab stop competing with the rail.
  item.tabIndex = -1;

  const label = document.createElement('span');
  label.className = 'settings-search-result-label';
  label.textContent = hit.label;
  item.appendChild(label);

  const pane = document.createElement('span');
  pane.className = 'settings-search-result-pane';
  pane.textContent = PANES.find((p) => p.id === hit.pane).title;
  item.appendChild(pane);

  return item;
}

// Repaints the result list and toggles the wrap's class, which the CSS reads
// to swap the rail for the results instead of filtering rail items.
function refreshSearchResults(container) {
  if (!container) {
    return;
  }
  const query = searchQuery.trim();
  searchHits = query ? searchSettings(searchQuery) : [];
  searchActiveIndex = 0;
  container.textContent = '';
  container.parentElement.classList.toggle('settings-search-active', Boolean(query));
  if (query && !searchHits.length) {
    const empty = document.createElement('div');
    empty.className = 'settings-search-empty';
    empty.textContent = 'No results';
    container.appendChild(empty);
    return;
  }
  searchHits.forEach((hit, i) => {
    container.appendChild(buildSearchResult(hit, i, i === 0));
  });
}

// The field plus its result list, sitting above the rail in the sidebar
// column. A filtered rail would fight the roving tabindex,
// so results are their own list instead - the rail is never touched here.
function buildSearchField() {
  const wrap = document.createElement('div');
  wrap.className = 'settings-search';

  const field = document.createElement('input');
  field.type = 'text';
  field.className = 'settings-search-field';
  field.placeholder = 'Search settings';
  field.autocomplete = 'off';
  field.value = searchQuery;
  wrap.appendChild(field);

  const results = document.createElement('div');
  results.className = 'settings-search-results';
  wrap.appendChild(results);
  refreshSearchResults(results);

  return wrap;
}

export function renderSettings(root, selectedId) {
  // Every rebuild replaces the whole tree, so the element holding focus is
  // destroyed with it and focus falls to the body - which leaves the keyboard
  // driving nothing. The browser's pushes answering settingsReady
  // each land here, so a window that opened focused on the rail loses it a few
  // milliseconds later without this. Read the anchor BEFORE clearing; put
  // focus back on the same element's replacement after.
  const anchor = focusAnchor(root);
  const textEdit = textEditAnchor(root);
  const selected = paneOrDefault(selectedId);
  stopHotkeyCapture();
  // Before the tree goes: an unsaved preview is painted on another window,
  // which no rebuild here would ever take back.
  if (selected !== 'spaces') {
    releaseThemeStudio(true);
  }
  // Read before the tree goes: a rebuild of the pane already open is not an
  // arrival, and replaying the arrival on every profile click reads as a blink.
  const arriving = root.querySelector('.settings-body')?.dataset.pane !== selected;
  root.textContent = '';

  const rail = document.createElement('nav');
  rail.className = 'settings-rail';
  for (const pane of PANES) {
    const item = document.createElement('button');
    item.className = 'settings-rail-item';
    item.dataset.pane = pane.id;
    const icon = document.createElement('span');
    icon.className = 'settings-rail-item-icon';
    icon.appendChild(buildIcon(RAIL_ICON_SHAPES[pane.id] || []));
    item.appendChild(icon);
    const label = document.createElement('span');
    label.className = 'settings-rail-item-label';
    label.textContent = pane.title;
    item.appendChild(label);
    // A roving tabindex: the rail is ONE Tab stop, and the arrow
    // keys move within it. Every item tabbable would make Tab walk six panes
    // before it reached the pane's own controls.
    item.tabIndex = pane.id === selected ? 0 : -1;
    if (pane.id === selected) {
      item.classList.add('selected');
      item.setAttribute('aria-current', 'page');
    }
    rail.appendChild(item);
  }

  const body = document.createElement('section');
  body.className = arriving ? 'settings-body settings-pane-swap' : 'settings-body';
  body.dataset.pane = selected;
  // .settings-body owns the scrollbar - see settings.css for why the drag
  // region lives on this inner wrapper instead.
  const content = document.createElement('div');
  content.className = 'settings-pane-content';
  const heading = document.createElement('h1');
  heading.className = 'settings-heading';
  heading.textContent = PANES.find((p) => p.id === selected).title;
  content.appendChild(heading);

  // On every pane rather than only the one the edit came from: the user can
  // move away between the click and the browser's answer, and a warning they
  // never see is the same as no warning.
  if (saveError) {
    const alert = document.createElement('div');
    alert.className = 'settings-alert';
    alert.setAttribute('role', 'alert');
    alert.textContent = saveError;
    content.appendChild(alert);
  }

  if (GROUPS[selected]) {
    renderGroupedRows(content, selected);
  } else {
    for (const row of PANE_ROWS[selected] || []) {
      if (row.special === 'aiModel') {
        renderAiModelRow(content);
        continue;
      }
      if (row.special === 'swipeFeel') {
        renderSwipeFeelRow(content);
        continue;
      }
      if (row.special === 'appIcon') {
        renderAppIconRow(content);
        continue;
      }
      content.appendChild(buildStandardRow(row));
    }
  }

  // After the pane's own rows, because the lab puts Swipe feel above the
  // Shortcuts list.
  if (selected === 'inputs') {
    renderInputs(content, (next, forget) => sendHotkeys(root, next, forget));
  }
  if (selected === 'spaces') {
    renderSpaces(content);
  }
  if (selected === 'profiles') {
    renderProfiles(content);
  }
  if (selected === 'engines') {
    renderEnginesPane(content, settings, (next, opts) => sendSettings(root, next, opts));
  }
  if (selected === 'routing') {
    renderRoutingPane(content, routing, spaces,
                      (next, opts) => sendRouting(root, next, opts));
  }
  if (selected === 'import') {
    renderImportPane(content, importState, {
      request: (source) => {
        importState.pending[source] = true;
        chrome.send('settingsImportPreview', [source]);
      },
      run: (source) => {
        importState.running = source;
        importState.result = null;
        chrome.send('settingsImportRun', [source]);
        renderSettings(root, currentPane);
      },
    });
  }
  if (selected === 'about') {
    renderAbout(content);
  }
  body.appendChild(content);

  const sidebar = document.createElement('div');
  sidebar.className = 'settings-sidebar';
  // The window's own drag handle - see settings.css and
  // atrium_settings_titlebar_mac.mm.
  const dragRegion = document.createElement('div');
  dragRegion.className = 'settings-drag-region';
  sidebar.appendChild(dragRegion);
  sidebar.appendChild(buildSearchField());
  sidebar.appendChild(rail);

  root.appendChild(sidebar);
  root.appendChild(buildPin());
  root.appendChild(body);

  // Only when something inside root held focus before the rebuild: restoring
  // it repairs what this function broke, but focusing out of nowhere would
  // steal focus from wherever the user actually put it.
  if (anchor) {
    const replacement = anchor === 'rail' ?
        root.querySelector(`.settings-rail-item[data-pane="${selected}"]`) :
        root.querySelector(anchor);
    if (replacement) {
      replacement.focus();
    }
  }
  // Put the uncommitted text (and the caret) back on top of whatever the
  // fresh row just painted from the store.
  if (textEdit) {
    const replacement = root.querySelector(textEdit.selector);
    if (replacement) {
      replacement.value = textEdit.value;
      replacement.focus();
      replacement.selectionStart = textEdit.selectionStart;
      replacement.selectionEnd = textEdit.selectionEnd;
    }
  }
  return selected;
}

// The About pane: where the third-party attribution is reachable
// from.
//
// The notices themselves are NOT rendered here. They are the build's generated
// set - around 761 components, ~19 MB - and they already have a page that can
// show them: chrome://credits, whose content components/resources/BUILD.gn
// generates with tools/licenses/licenses.py. This pane is the route to it,
// which is the part Atrium was missing.
//
// The sentence is a statement of fact about what this binary is built from.
// Anything with legal weight beyond that - who holds the copyright in Atrium's
// own code, and under what terms - is a separate piece of work and is deliberately
// absent rather than guessed at here.
export const ABOUT_NOTE =
    'Atrium is built on Chromium. Chromium and the open source components ' +
    'this build includes are used under their own licenses.';

function renderAbout(body) {
  const note = document.createElement('p');
  note.className = 'settings-about-note';
  note.textContent = ABOUT_NOTE;
  body.appendChild(note);

  const line = document.createElement('div');
  line.className = 'settings-row';

  const label = document.createElement('span');
  label.className = 'settings-row-label';
  label.textContent = 'Open source licenses';
  const hint = document.createElement('small');
  hint.className = 'settings-row-hint';
  hint.textContent = 'Every notice this build carries, readable offline';
  label.appendChild(hint);

  // A button rather than a link: the settings window renders in the system
  // profile, which allows no browser windows, so a navigation from this page
  // has nowhere to land. The browser picks the destination and the window.
  const value = document.createElement('button');
  value.className = 'settings-row-value settings-row-credits';
  value.textContent = 'Open';

  line.appendChild(label);
  line.appendChild(value);
  body.appendChild(line);
}

// /api/tags is Ollama's own lightest GET, so this proves reachability without
// sending any page content. opts.controller lets a
// caller abort it externally.
export async function checkOllamaConnection(
    host, port, {fetchImpl = fetch, timeoutMs = 3000, controller = new AbortController()} = {}) {
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const response = await fetchImpl(
        `http://${host}:${port}/api/tags`, {signal: controller.signal});
    return response.ok ?
        {reachable: true} :
        {reachable: false, error: `Ollama answered with HTTP ${response.status}`};
  } catch (err) {
    const timedOut = err && err.name === 'AbortError';
    return {
      reachable: false,
      error: timedOut ? 'Timed out waiting for a response' :
                        'Could not reach that host and port',
    };
  } finally {
    clearTimeout(timer);
  }
}

// The last connection check's outcome, or null before the first one - null and
// {reachable: false} are different states, so the row must not show either as
// the other.
let ollamaStatus = null;

// The in-flight check's own controller, so disabling AI mid-check can abort
// the real fetch rather than only discarding the answer.
let ollamaAbort = null;

async function runOllamaConnectionCheck(root) {
  if (settings.aiEnabled === false) {
    return;  // The row that calls this is absent once off; belt and braces.
  }
  ollamaAbort = new AbortController();
  const controller = ollamaAbort;
  ollamaStatus = {checking: true};
  renderSettings(root, currentPane);
  ollamaStatus = await checkOllamaConnection(
      settings.ollamaHost, settings.ollamaPort, {controller});
  if (ollamaAbort === controller) {
    ollamaAbort = null;
  }
  renderSettings(root, currentPane);
}

// which backend the AI features toggle actually calls.
// sidebarHidden is the window's live state, pushed beside the settings, and
// the flip goes back on its own message, never through a settingsChanged save.
function renderHideSidebarRow(body) {
  const line = document.createElement('div');
  line.className = 'settings-row';
  line.dataset.key = '';
  line.dataset.row = 'hide-sidebar';

  const label = document.createElement('span');
  label.className = 'settings-row-label';
  label.textContent = 'Hide sidebar';
  const hint = document.createElement('small');
  hint.className = 'settings-row-hint';
  hint.textContent = 'The page takes the full window; hover the left edge to peek';
  label.appendChild(hint);

  const on = settings.sidebarHidden === true;
  const value = document.createElement('button');
  value.className = 'settings-row-value settings-row-toggle settings-row-hide-sidebar';
  value.setAttribute('aria-pressed', String(on));
  const knob = document.createElement('span');
  knob.className = 'settings-row-toggle-knob';
  value.appendChild(knob);
  const state = document.createElement('span');
  state.className = 'settings-row-toggle-state';
  state.textContent = on ? 'On' : 'Off';
  value.appendChild(state);

  line.appendChild(label);
  line.appendChild(value);
  body.appendChild(line);
}

// Cleared by the next push, so Make default cannot be pressed twice while
// macOS shows its own prompt.
let defaultBrowserAsked = false;

// `defaultBrowser` is 'yes', 'no' or 'unknown' once the browser's check
// answers, and absent until then. Unknown still offers the button, since
// asking macOS is the way to find out.
function renderDefaultBrowserRow(body) {
  const line = document.createElement('div');
  line.className = 'settings-row';
  line.dataset.key = '';
  line.dataset.row = 'default-browser';

  const label = document.createElement('span');
  label.className = 'settings-row-label';
  label.textContent = 'Default browser';
  const hint = document.createElement('small');
  hint.className = 'settings-row-hint';
  label.appendChild(hint);

  let value;
  if (settings.defaultBrowser === 'no' || settings.defaultBrowser === 'unknown') {
    if (defaultBrowserAsked) {
      hint.textContent = 'Asking macOS…';
    } else if (settings.defaultBrowser === 'no') {
      hint.textContent = 'Another app opens links today';
    } else {
      hint.textContent = 'Could not tell which app opens links';
    }
    value = document.createElement('button');
    value.className = 'settings-row-value settings-row-make-default';
    value.textContent = 'Make default';
    value.disabled = defaultBrowserAsked;
  } else {
    value = document.createElement('span');
    value.className = 'settings-row-value';
    if (settings.defaultBrowser === 'yes') {
      hint.textContent = 'Atrium opens links from other apps';
      value.textContent = 'Default';
    } else {
      hint.textContent = 'Checking…';
      value.textContent = '—';
    }
  }

  line.appendChild(label);
  line.appendChild(value);
  body.appendChild(line);
}

// Renders in place of PANE_ROWS.ai's 'Model' entry (row.special === 'aiModel').
function renderAiModelRow(body) {
  const line = document.createElement('div');
  line.className = 'settings-row';
  line.dataset.key = 'aiModel';

  const label = document.createElement('span');
  label.className = 'settings-row-label';
  label.textContent = 'Model';
  const hint = document.createElement('small');
  hint.className = 'settings-row-hint';
  const availability = aiModelAvailability(currentAiModel());
  if (availability === 'unavailable') {
    hint.textContent = 'Unavailable on this Mac - Apple Intelligence is off';
    hint.classList.add('settings-row-warn');
  } else if (availability === 'unknown') {
    hint.textContent = 'Availability not detected yet';
  } else {
    hint.textContent = 'On-device and Ollama are free - your hardware; ' +
        'Hosted and Custom endpoint are the Pro tier';
  }
  label.appendChild(hint);

  const value = document.createElement('select');
  // settings-row-choice for the visual treatment only - commitControl routes
  // on settings-row-ai-model first, so this never reaches setRow (no `key`
  // to write it to).
  value.className = 'settings-row-value settings-row-choice settings-row-ai-model';
  for (const model of AI_MODELS) {
    const option = document.createElement('option');
    option.value = model;
    option.textContent = AI_MODEL_LABELS[model];
    option.selected = model === currentAiModel();
    value.appendChild(option);
  }
  value.value = currentAiModel();

  line.appendChild(label);
  line.appendChild(value);
  body.appendChild(line);
}

// Routes through setRow like every other choice row - the row def
// carries `choices: AI_MODELS`, so controlKind resolves it the same way any
// other picker resolves.
function setSelectedAiModel(root, value) {
  if (!AI_MODELS.includes(value) || value === currentAiModel()) {
    return;
  }
  setRow(root, 'aiModel', value);
}

function renderOllamaConnection(body) {
  const line = document.createElement('div');
  line.className = 'settings-row';
  line.dataset.key = 'ollamaConnection';

  const label = document.createElement('span');
  label.className = 'settings-row-label';
  label.textContent = `Ollama (${settings.ollamaHost}:${settings.ollamaPort})`;
  const hint = document.createElement('small');
  hint.className = 'settings-row-hint';
  if (ollamaStatus && ollamaStatus.checking) {
    hint.textContent = 'Checking…';
  } else if (ollamaStatus) {
    hint.textContent = ollamaStatus.reachable ? 'Reachable' : ollamaStatus.error;
    hint.classList.toggle('settings-row-warn', !ollamaStatus.reachable);
  }
  label.appendChild(hint);

  const value = document.createElement('button');
  value.className = 'settings-row-value settings-row-test-connection';
  value.textContent = 'Test connection';
  value.disabled = Boolean(ollamaStatus && ollamaStatus.checking);

  line.appendChild(label);
  line.appendChild(value);
  body.appendChild(line);
}

// Sends a typed key to the browser for the keychain to store. Not routed
// through setRow/settingsChanged - the key must never enter `settings` or the
// JSON blob that lands in settings.json.
function sendCustomEndpointApiKey(typed) {
  if (settings.aiEnabled === false) {
    return;  // The row that calls this is absent once off; belt and braces.
  }
  if (!typed) {
    return;  // An untouched blur, not a request to clear the stored key.
  }
  chrome.send('settingsCustomEndpointApiKeyChanged', [typed]);
}

// The only path that reaches the native empty-string branch: an untouched
// blur must not clear the key, so sendCustomEndpointApiKey('') is a no-op by
// design - this is the explicit "Remove key" request instead.
function clearCustomEndpointApiKey() {
  chrome.send('settingsCustomEndpointApiKeyChanged', ['']);
}

// Whether the keychain holds a custom endpoint API key, pushed by the browser
// - never the key itself. null before the first push.
let customEndpointApiKeyStatus = null;

// Called by the browser after a keychain read or write.
export function setCustomEndpointApiKeyStatus(hasKey) {
  customEndpointApiKeyStatus = {hasKey: Boolean(hasKey)};
  if (rootEl) {
    renderSettings(rootEl, currentPane);
  }
}

// A password input rather than a PANE_ROWS entry: the value must never come
// from `settings` - it always renders empty, so a
// re-open can never show back what was typed.
function renderCustomEndpointApiKey(body) {
  const line = document.createElement('div');
  line.className = 'settings-row';
  line.dataset.key = 'customEndpointApiKey';

  const label = document.createElement('span');
  label.className = 'settings-row-label';
  label.textContent = 'API key';
  const hint = document.createElement('small');
  hint.className = 'settings-row-hint';
  const hasUrl = typeof settings.customEndpointUrl === 'string' &&
      URL.canParse(settings.customEndpointUrl) &&
      new URL(settings.customEndpointUrl).protocol === 'https:';
  hint.textContent = !hasUrl ? 'Save the endpoint URL first' :
      customEndpointApiKeyStatus && customEndpointApiKeyStatus.hasKey ?
      'A key is saved in the keychain' : 'No key saved';
  label.appendChild(hint);

  const value = document.createElement('input');
  value.disabled = !hasUrl;
  value.type = 'password';
  value.autocomplete = 'off';
  value.value = '';
  value.placeholder = customEndpointApiKeyStatus && customEndpointApiKeyStatus.hasKey ?
      '••••••••' : 'Paste your API key';
  value.className = 'settings-row-value settings-row-text settings-row-api-key';

  line.appendChild(label);
  line.appendChild(value);

  if (customEndpointApiKeyStatus && customEndpointApiKeyStatus.hasKey) {
    const clear = document.createElement('button');
    clear.className = 'settings-row-clear-api-key';
    clear.textContent = 'Remove key';
    line.appendChild(clear);
  }

  body.appendChild(line);
}

// Native probe, not a JS fetch() - the resolved-address
// refusal must hold against DNS rebinding after save time, which only a
// request-time check in the real network stack can do.
let customEndpointConnectionStatus = null;

function runCustomEndpointConnectionCheck(root) {
  if (settings.aiEnabled === false) {
    return;  // The row that calls this is absent once off; belt and braces.
  }
  customEndpointConnectionStatus = {checking: true};
  renderSettings(root, currentPane);
  chrome.send(
      'settingsCheckCustomEndpointConnection',
      [settings.customEndpointUrl, settings.customEndpointModel,
       settings.customEndpointTimeoutSeconds || 0]);
}

// Called by the browser once the probe resolves - refused, failed, or ok.
export function setCustomEndpointConnectionStatus(status) {
  customEndpointConnectionStatus = status;
  if (rootEl) {
    renderSettings(rootEl, currentPane);
  }
}

function renderCustomEndpointConnection(body) {
  const line = document.createElement('div');
  line.className = 'settings-row';
  line.dataset.key = 'customEndpointConnection';

  const label = document.createElement('span');
  label.className = 'settings-row-label';
  label.textContent = 'Custom endpoint';
  const hint = document.createElement('small');
  hint.className = 'settings-row-hint';
  if (customEndpointConnectionStatus && customEndpointConnectionStatus.checking) {
    hint.textContent = 'Checking…';
  } else if (customEndpointConnectionStatus) {
    hint.textContent = customEndpointConnectionStatus.reachable ?
        'Reachable' : customEndpointConnectionStatus.error;
    hint.classList.toggle(
        'settings-row-warn', !customEndpointConnectionStatus.reachable);
  }
  label.appendChild(hint);

  const value = document.createElement('button');
  value.className =
      'settings-row-value settings-row-test-custom-endpoint-connection';
  value.textContent = 'Test connection';
  value.disabled =
      Boolean(customEndpointConnectionStatus && customEndpointConnectionStatus.checking);

  line.appendChild(label);
  line.appendChild(value);
  body.appendChild(line);
}

// docs/lab/swipe-lab.html:5237-5241. blendFactor and holdSilenceGap have no
// swipe.js reader - sidebar.js's tuning map drops them - but persist anyway.
const SWIPE_PRESETS = {
  Default: {
    swipeCommitThreshold: 0.15, swipeBlendFactor: 6.0,
    swipeHoldSilenceGap: 0.5, swipeOvershootCap: 0.40, swipeSettleDur: 0.28,
    swipeSpringDamp: 0.86, swipeFollowTau: 0.15, swipeSlowCommit: 0.30,
  },
  Quick: {
    swipeCommitThreshold: 0.12, swipeBlendFactor: 4.0,
    swipeHoldSilenceGap: 0.35, swipeOvershootCap: 0.30, swipeSettleDur: 0.18,
    swipeSpringDamp: 0.95, swipeFollowTau: 0.08, swipeSlowCommit: 0.25,
  },
  Cozy: {
    swipeCommitThreshold: 0.20, swipeBlendFactor: 6.5,
    swipeHoldSilenceGap: 0.65, swipeOvershootCap: 0.40, swipeSettleDur: 0.42,
    swipeSpringDamp: 0.74, swipeFollowTau: 0.20, swipeSlowCommit: 0.38,
  },
};

// Which preset the live settings match, or null - the lab's own epsilon
// (swipe-lab.html:5246-5247). A field the browser has not pushed yet reads as
// not-a-number, so no preset claims it until a real push arrives.
export function activeSwipePreset(from = settings) {
  return Object.keys(SWIPE_PRESETS).find((name) =>
      Object.entries(SWIPE_PRESETS[name]).every(([key, value]) =>
          typeof from[key] === 'number' && Math.abs(from[key] - value) < 0.001)) ||
      null;
}

// Sets all eight keys as one edit, mirroring setRow's shape. Not setRow
// itself: these keys have no PANE_ROWS entry for it to look up.
function applySwipePreset(root, name) {
  const preset = SWIPE_PRESETS[name];
  if (!preset) {
    return;
  }
  const id = beginSettingsSave({...settings, ...preset});
  saveError = null;
  renderSettings(root, currentPane);
  chrome.send('settingsChanged', [settingsPayload(), id]);
}

function renderSwipeFeelRow(body) {
  const line = document.createElement('div');
  line.className = 'settings-row';
  line.dataset.key = 'swipeTuning';

  const label = document.createElement('span');
  label.className = 'settings-row-label';
  label.textContent = 'Swipe feel';
  const hint = document.createElement('small');
  hint.className = 'settings-row-hint';
  hint.textContent = 'Default is the tuned baseline · Quick snaps · Cozy floats';
  label.appendChild(hint);

  const value = document.createElement('div');
  value.className = 'settings-row-value settings-swipe-presets';
  const active = activeSwipePreset();
  for (const name of Object.keys(SWIPE_PRESETS)) {
    const button = document.createElement('button');
    button.className = 'settings-swipe-preset';
    button.dataset.preset = name;
    button.textContent = name;
    button.classList.toggle('on', name === active);
    value.appendChild(button);
  }

  line.appendChild(label);
  line.appendChild(value);
  body.appendChild(line);
}

// The tile src is root-relative on purpose: a cross-host chrome:// image
// fails before any data source runs (the request filter in atrium_ui.cc).
function renderAppIconRow(body) {
  const line = document.createElement('div');
  line.className = 'settings-row settings-row-appicon';
  line.dataset.key = 'appIconVariant';

  const label = document.createElement('span');
  label.className = 'settings-row-label';
  label.textContent = 'App icon';
  const hint = document.createElement('small');
  hint.className = 'settings-row-hint';
  hint.textContent = 'The Dock changes now · Finder and ⌘Tab follow a relaunch';
  label.appendChild(hint);

  const value = document.createElement('div');
  value.className = 'settings-row-value settings-app-icons';
  value.setAttribute('role', 'radiogroup');
  value.setAttribute('aria-label', 'App icon');
  for (const slug of APP_ICON_VARIANTS) {
    const button = document.createElement('button');
    button.className = 'settings-app-icon';
    button.dataset.variant = slug;
    button.setAttribute('role', 'radio');
    const chosen = slug === settings.appIconVariant;
    button.setAttribute('aria-checked', String(chosen));
    button.classList.toggle('on', chosen);
    const img = document.createElement('img');
    img.className = 'settings-app-icon-img';
    img.src = `/appicon/${slug}.png`;
    img.alt = slug;
    button.appendChild(img);
    value.appendChild(button);
  }

  line.appendChild(label);
  line.appendChild(value);
  body.appendChild(line);
}

// atrium_native_hotkey_map.mm's NAMED_KEYS - a keydown's `code` is layout-
// independent, unlike `key`, so this is the physical-key half of the combo
// the capture control can name without guessing at a layout.
const HOTKEY_NAMED_KEY_BY_CODE = {
  Tab: 'tab', Escape: 'escape', Space: 'space',
  ArrowLeft: 'left', ArrowRight: 'right', ArrowUp: 'up', ArrowDown: 'down',
};

// A keydown's combo string, or null while only a modifier is held, or
// {error} for a key this store has no spelling for (small_stores.rs's
// parse_combo: a named key or exactly one character).
function comboFromKeydown(e) {
  if (['Meta', 'Shift', 'Alt', 'Control'].includes(e.key)) {
    return null;
  }
  const named = HOTKEY_NAMED_KEY_BY_CODE[e.code];
  let key;
  if (named) {
    key = named;
  } else if (e.key.length === 1) {
    key = e.key.toLowerCase();
  } else {
    return {error: 'That key has no shortcut spelling.'};
  }
  const mods = [];
  if (e.metaKey) mods.push('cmd');
  if (e.shiftKey) mods.push('shift');
  if (e.altKey) mods.push('opt');
  if (e.ctrlKey) mods.push('ctrl');
  return {combo: [...mods, key].join('+')};
}

// atrium_native_hotkey_defaults.cc's IsReservedHotkeyCombo, mirrored so a
// combo the native map would drop on load is refused here instead.
function hotkeyComboIsReserved(mods, key) {
  if (mods.includes('opt')) {
    return false;
  }
  if (key === 'tab') {
    return mods.includes('ctrl') && !mods.includes('cmd');
  }
  if (key === 'w') {
    return mods.includes('cmd') && mods.includes('shift') && !mods.includes('ctrl');
  }
  return false;
}

// atrium_native_hotkey_map.mm's ParseCombo rule plus the reserved-combo
// check above, checked here so a chord the core would refuse is refused at
// the control instead. Returns a reason string, or null if usable.
function hotkeyComboRefusalReason(combo) {
  const segments = combo.split('+');
  const key = segments[segments.length - 1];
  const mods = segments.slice(0, -1);
  const hasModifier = mods.includes('cmd') || mods.includes('ctrl');
  if (!hasModifier && !(mods.length > 0 && (key === 'tab' || key === 'escape'))) {
    return combo + ' needs ⌘ or Ctrl.';
  }
  if (hotkeyComboIsReserved(mods, key)) {
    return combo + ' is reserved.';
  }
  return null;
}

// Display order is the macOS one, which is not the store's order: the store
// spells cmd first so its files sort stably, the keyboard shows ⌃ ⌥ ⇧ ⌘.
const KEYCAP_MODIFIER_ORDER = ['ctrl', 'opt', 'shift', 'cmd'];
const KEYCAP_GLYPHS = {
  ctrl: '⌃', opt: '⌥', shift: '⇧', cmd: '⌘',
  tab: '⇥', escape: '⎋', space: '␣',
  left: '←', right: '→', up: '↑', down: '↓',
};

export function comboKeycaps(combo) {
  const segments = combo.split('+');
  const key = segments[segments.length - 1];
  const mods = segments.slice(0, -1);
  const wrap = document.createElement('span');
  wrap.className = 'combo';
  for (const mod of KEYCAP_MODIFIER_ORDER) {
    if (mods.includes(mod)) {
      wrap.appendChild(keycap(KEYCAP_GLYPHS[mod]));
    }
  }
  wrap.appendChild(keycap(KEYCAP_GLYPHS[key] || key.toUpperCase()));
  return wrap;
}

function keycap(text) {
  const cap = document.createElement('kbd');
  cap.className = 'key ov-key';
  cap.textContent = text;
  return cap;
}

// Shape descriptors, not HTML strings: the WebUI CSP enforces Trusted Types
// with no policy registered, so buildIcon draws these via createElementNS.
const HOTKEY_ICONS = {
  record: [
    {tag: 'rect', x: 1.5, y: 4, width: 13, height: 8.5, rx: 2},
    {tag: 'path', d: 'M4.5 7h0M7 7h0M9.5 7h0M12 7h0M5 10h6'},
  ],
  clear: [
    {tag: 'path', d: 'M4 4l8 8M12 4l-8 8'},
  ],
  reset: [
    {tag: 'path', d: 'M3.5 6.5h6a3 3 0 0 1 0 6H6'},
    {tag: 'path', d: 'M6 4L3.5 6.5 6 9'},
  ],
};

function hotkeyIconButton(kind, className, label) {
  const button = document.createElement('button');
  button.className = className;
  button.type = 'button';
  button.setAttribute('aria-label', label);
  button.title = label;
  button.appendChild(buildIcon(HOTKEY_ICONS[kind]));
  return button;
}

// The Inputs pane: the swipe preset, then a row per hotkey action.
//
// The shortcut rows are built from the store rather than from a list here.
// The core fills in every default, so an absent file still yields the full
// map - and a user's own binding for an action that ships unassigned shows up
// without this file having to know the action exists.
function renderInputs(body, send) {
  const clashes = conflictingCombos(hotkeys);
  const actions = Object.keys(hotkeys).sort();
  if (!actions.length) {
    return;  // Nothing pushed yet; the heading stands alone rather than lying.
  }
  // A build that pushed no defaults cannot say which rows the user changed,
  // and a reset offered on every row would mostly do nothing.
  const shippedKnown = Object.keys(hotkeyDefaults()).length > 0;

  const sub = document.createElement('div');
  sub.className = 'settings-subhead';
  sub.textContent = 'Shortcuts';
  body.appendChild(sub);

  const ledger = document.createElement('div');
  ledger.className = 'hotkey-ledger';
  body.appendChild(ledger);

  if (Object.keys(hotkeyPresets).length) {
    ledger.appendChild(hotkeyPresetRow(send));
  }

  for (const action of actions) {
    const combos = hotkeys[action] || [];
    const line = document.createElement('div');
    line.className = 'settings-row';
    line.dataset.action = action;

    const label = document.createElement('span');
    label.className = 'settings-row-label';
    label.textContent = actionLabel(action);

    const value = document.createElement('span');
    value.className = 'settings-row-value';
    // An action can ship unassigned - createSpace does. Unassigned is a real
    // state, not a missing value.
    if (combos.length) {
      for (const c of combos) {
        const wrap = comboKeycaps(c);
        if (clashes.has(c)) {
          wrap.classList.add('conflict');
        }
        value.appendChild(wrap);
      }
    } else {
      value.textContent = 'Unassigned';
    }

    const clashing = combos.filter((c) => clashes.has(c));
    if (clashing.length) {
      line.classList.add('conflict');
      const warn = document.createElement('small');
      warn.className = 'settings-row-warn';
      warn.textContent = clashing
          .map((c) => `${c} also bound to ` +
               clashes.get(c).filter((a) => a !== action).map(actionLabel).join(', '))
          .join(' · ');
      label.appendChild(warn);
    }

    const error = document.createElement('div');
    error.className = 'hotkey-error';

    const record = hotkeyIconButton('record', 'hotkey-record', 'Record');

    record.addEventListener('click', () => {
      if (activeHotkeyCapture && activeHotkeyCapture.record === record) {
        stopHotkeyCapture();
        return;
      }
      stopHotkeyCapture();  // only one row records at a time
      error.textContent = '';
      record.classList.add('capturing');
      record.setAttribute('aria-label', 'Press a key, Escape cancels');
      record.title = 'Press a key, Escape cancels';
      const listener = (e) => {
        if (e.key === 'Escape' &&
            !(e.metaKey || e.ctrlKey || e.shiftKey || e.altKey)) {
          e.preventDefault();
          e.stopPropagation();
          stopHotkeyCapture();
          return;
        }
        const result = comboFromKeydown(e);
        if (!result) {
          return;  // A modifier alone - keep listening for the real key.
        }
        e.preventDefault();
        e.stopPropagation();  // the rail and the search field must not also act on this chord
        stopHotkeyCapture();
        if (result.error) {
          error.textContent = result.error;
          return;
        }
        const reason = hotkeyComboRefusalReason(result.combo);
        if (reason) {
          error.textContent = reason;
          return;
        }
        if (combos.includes(result.combo)) {
          return;  // Already bound to this action - nothing to send.
        }
        const owner = Object.keys(hotkeys).find(
            (a) => a !== action && (hotkeys[a] || []).includes(result.combo));
        if (owner) {
          error.textContent =
              result.combo + ' already used by ' + actionLabel(owner) + '.';
          return;
        }
        send({...hotkeys, [action]: [...combos, result.combo]});
      };
      activeHotkeyCapture = {
        listener,
        record,
        reset: () => {
          record.classList.remove('capturing');
          record.setAttribute('aria-label', 'Record');
          record.title = 'Record';
        },
      };
      document.addEventListener('keydown', listener, true);
    });
    record.addEventListener('blur', () => {
      if (activeHotkeyCapture && activeHotkeyCapture.record === record) {
        stopHotkeyCapture();
      }
    });

    const clear = hotkeyIconButton('clear', 'hotkey-clear', 'Clear');
    clear.disabled = combos.length === 0;
    clear.addEventListener('click', () => {
      send({...hotkeys, [action]: []});
    });

    const rowActions = document.createElement('span');
    rowActions.className = 'hotkey-actions';
    rowActions.appendChild(record);
    rowActions.appendChild(clear);
    if (shippedKnown && !hotkeyIsShipped(action, combos)) {
      rowActions.appendChild(hotkeyResetButton(action, send));
    }

    line.appendChild(label);
    line.appendChild(value);
    line.appendChild(rowActions);
    line.appendChild(error);
    ledger.appendChild(line);
  }

  if (shippedKnown) {
    ledger.appendChild(hotkeyResetAllRow(send));
  }
}

// Custom is not a table. It is what the rows say: the moment one of them
// differs from the named preset's chord, the pane is on the user's own map
// and no preset describes it any more.
export function hotkeyPresetInUse() {
  const edited = Object.keys(hotkeys).some(
      (action) => !hotkeyIsShipped(action, hotkeys[action] || []));
  return edited ? 'custom' : hotkeyPreset;
}

const HOTKEY_PRESET_LABELS = {arc: 'Arc', atrium: 'Atrium', custom: 'Custom'};

// The picker. Custom sits among the options because it is a state the pane
// can be IN, and is disabled while it is not - there is no table to apply.
function hotkeyPresetRow(send) {
  const inUse = hotkeyPresetInUse();
  const line = document.createElement('div');
  line.className = 'settings-row hotkey-preset-row';

  const label = document.createElement('span');
  label.className = 'settings-row-label';
  label.textContent = 'Preset';

  const slot = document.createElement('span');
  slot.className = 'hotkey-preset-slot';
  slot.setAttribute('role', 'radiogroup');

  const options = [...Object.keys(hotkeyPresets).sort(), 'custom'];
  for (const name of options) {
    const option = document.createElement('button');
    option.className = 'hotkey-preset';
    option.dataset.preset = name;
    option.type = 'button';
    option.setAttribute('role', 'radio');
    option.setAttribute('aria-checked', String(name === inUse));
    option.classList.toggle('on', name === inUse);
    option.textContent = HOTKEY_PRESET_LABELS[name] || name;
    // The option in use stays reachable, or the group loses its own answer to
    // the keyboard.
    option.disabled = name === 'custom' && inUse !== 'custom';
    option.addEventListener(
        'click', () => hotkeyPresetPicked(slot, name, inUse, send));
    slot.appendChild(option);
  }

  line.appendChild(label);
  line.appendChild(slot);
  return line;
}

// Leaving Custom throws the user's own rows away and nothing here remembers
// them, so that one direction asks first. Between two presets there is no
// user map to lose.
function hotkeyPresetPicked(slot, name, inUse, send) {
  if (name === inUse) {
    return;
  }
  const apply = () => send({...hotkeyPresets[name]}, {forgetAll: true,
                                                      preset: name});
  if (inUse !== 'custom') {
    apply();
    return;
  }

  const question = document.createElement('span');
  question.className = 'hotkey-preset-question';
  question.textContent = `Replace your shortcuts with ${
      HOTKEY_PRESET_LABELS[name] || name}?`;

  const yes = document.createElement('button');
  yes.className = 'hotkey-preset-confirm';
  yes.textContent = 'Replace';
  yes.type = 'button';

  const no = document.createElement('button');
  no.className = 'hotkey-preset-cancel';
  no.textContent = 'Cancel';
  no.type = 'button';

  const held = [...slot.childNodes];
  const standDown = () => {
    slot.removeEventListener('keydown', onEscape);
    slot.replaceChildren(...held);
    slot.querySelector(`.hotkey-preset[data-preset="${name}"]`).focus();
  };
  // Hoisted, so standDown can drop it again - a second ask on the same row
  // would otherwise leave two listeners behind.
  function onEscape(event) {
    if (event.key !== 'Escape') {
      return;
    }
    // Before the window's own Escape, which would clear the search field and
    // leave the question standing.
    event.preventDefault();
    event.stopPropagation();
    standDown();
  }

  slot.replaceChildren(question, yes, no);
  // Cancel takes the focus, not Replace - a held Enter repeats its click and
  // the repeat must not land on the destructive step.
  no.focus();
  no.addEventListener('click', standDown);
  yes.addEventListener('click', apply);
  slot.addEventListener('keydown', onEscape);
}

// Only on a row that differs from its shipped chord, so the control's presence
// is itself the answer to "have I changed this one".
function hotkeyResetButton(action, send) {
  const shipped = hotkeyDefaults()[action] || [];
  const reset = hotkeyIconButton('reset', 'hotkey-reset', 'Reset');
  reset.title = shipped.length ? `Back to ${shipped.join('  ')}`
                               : 'Back to unassigned';
  reset.addEventListener('click', () => {
    const next = {...hotkeys};
    // An action that ships unassigned has no row of its own once the user's
    // binding is gone, so the pane drops it rather than showing an empty one.
    if (shipped.length) {
      next[action] = shipped;
    } else {
      delete next[action];
    }
    send(next, {forget: action});
  });
  return reset;
}

// The whole pane at once. Confirmed first: every rebind in the file goes, and
// nothing in the pane records what they were.
function hotkeyResetAllRow(send) {
  const line = document.createElement('div');
  line.className = 'settings-row hotkey-reset-all-row';

  const label = document.createElement('span');
  label.className = 'settings-row-label';
  label.textContent = 'All shortcuts';

  const slot = document.createElement('span');
  slot.className = 'hotkey-reset-all-slot';

  const ask = document.createElement('button');
  ask.className = 'hotkey-reset-all';
  ask.textContent = 'Reset all';
  ask.type = 'button';

  const question = document.createElement('span');
  question.className = 'hotkey-reset-question';
  question.textContent = 'Reset every shortcut?';

  const yes = document.createElement('button');
  yes.className = 'hotkey-reset-all-confirm';
  yes.textContent = 'Reset all';
  yes.type = 'button';

  const no = document.createElement('button');
  no.className = 'hotkey-reset-all-cancel';
  no.textContent = 'Cancel';
  no.type = 'button';

  const standDown = () => {
    slot.replaceChildren(ask);
    ask.focus();
  };

  ask.addEventListener('click', () => {
    slot.replaceChildren(question, yes, no);
    // Cancel takes the focus, not Reset. A button click can come from a held
    // Enter, which repeats, and the second one must not land on the confirm.
    no.focus();
  });
  no.addEventListener('click', standDown);
  slot.addEventListener('keydown', (event) => {
    if (event.key !== 'Escape' || slot.contains(ask)) {
      return;
    }
    // Before the window's own Escape, which would clear the search field and
    // leave the question standing.
    event.preventDefault();
    event.stopPropagation();
    standDown();
  });
  yes.addEventListener('click', () => {
    send({...hotkeyDefaults()}, {forgetAll: true});
  });

  slot.appendChild(ask);
  line.appendChild(label);
  line.appendChild(slot);
  return line;
}

// The unsaved spec is held here, not inside the module, because every browser
// push rebuilds this whole pane - an edit elsewhere in the document would
// otherwise throw away a theme the user is still working on.
let themeStudio = null;
let themeStudioSpaceId = null;
let themeStudioDraft = null;

// Whether the window behind is painted with something themes.json does not
// hold. Only then is an undo worth sending.
let themePreviewLive = false;

// `revert` is false for a remount of the SAME space - the studio is rebuilt
// constantly and asking the browser to repaint each time would fight the
// preview it just applied.
function releaseThemeStudio(revert) {
  if (themeStudio) {
    themeStudio.destroy();
    themeStudio = null;
  }
  if (!revert) {
    return;
  }
  if (themePreviewLive && themeStudioSpaceId) {
    chrome.send('settingsThemePreview', [themeStudioSpaceId, null]);
  }
  themePreviewLive = false;
  themeStudioDraft = null;
  themeStudioSpaceId = null;
}

// The browser window behind this one is the preview surface, so nothing this
// section builds repaints on an edit.
function themeSection(space) {
  const wrap = document.createElement('div');
  wrap.className = 'settings-theme-section';
  const spaceId = space.id;
  // Before the early return below, or a space with no id would leave the last
  // studio's frame, picker and document listener alive on a detached tree.
  releaseThemeStudio(themeStudioSpaceId !== spaceId);
  if (!spaceId) {
    return wrap;  // Nothing to key the store by, so the section stays empty.
  }
  themeStudioSpaceId = spaceId;
  const stored = space.themeDraft || space.theme || null;
  themeStudio = mountThemeStudio(wrap, {
    spec: themeStudioDraft || stored,
    spaceName: space.name || '',
    // The draft's own first stop, never space.tint - the tint may be a name
    // from tint.rs, and the studio wants a hex.
    accent: stored?.stops?.[0]?.color || null,
    onPreview: (next) => {
      themeStudioDraft = next;
      themePreviewLive = true;
      chrome.send('settingsThemePreview', [spaceId, next]);
    },
    onCommit: (next) => {
      themeStudioDraft = null;
      themePreviewLive = false;
      chrome.send('settingsThemeChanged', [spaceId, next]);
    },
    onRemove: () => {
      themeStudioDraft = null;
      themePreviewLive = false;
      chrome.send('settingsThemeChanged', [spaceId, null]);
    },
  });
  return wrap;
}

// The Spaces pane: a list of spaces beside a detail panel, which is the lab's
// master-detail shape rather than a row list.
function renderSpaces(body) {
  if (!spaces.length) {
    // Released here too, or the studio's colour picker keeps a body node and a
    // document listener alive for a draft with no space left to save it to.
    releaseThemeStudio(true);
    return;  // Nothing pushed yet, so the heading stands alone rather than lying.
  }
  const wrap = document.createElement('div');
  wrap.className = 'settings-spaces';

  const list = document.createElement('div');
  list.className = 'settings-space-list';
  spaces.forEach((space, index) => {
    const item = document.createElement('button');
    item.className = 'settings-space-item';
    item.dataset.space = String(index);
    if (index === selectedSpace) {
      item.classList.add('selected');
    }
    const dot = document.createElement('span');
    dot.className = 'settings-space-dot';
    // The tint is the space's own colour and comes from the document. Set as a
    // style property rather than interpolated into markup - it is user data.
    if (space.tint) {
      dot.style.background = space.tint;
    }
    dot.appendChild(spaceGlyph(space.emoji || '', space.name || ''));
    const name = document.createElement('span');
    name.textContent = space.name || 'Untitled';
    item.appendChild(dot);
    item.appendChild(name);
    list.appendChild(item);
  });
  const newSpace = document.createElement('button');
  newSpace.className = 'settings-space-item settings-space-new';
  newSpace.textContent = '+ New Space';
  list.appendChild(newSpace);

  const detail = document.createElement('div');
  detail.className = 'settings-space-detail';
  const space = spaces[selectedSpace];
  detail.appendChild(spaceNameRow(space, selectedSpace));
  detail.appendChild(spaceProfileRow(space, selectedSpace, profiles));
  detail.appendChild(themeSection(space));

  const openLine = document.createElement('div');
  openLine.className = 'settings-row';
  const openLabel = document.createElement('span');
  openLabel.className = 'settings-row-label';
  const open = document.createElement('button');
  open.className = 'settings-row-value settings-space-open';
  open.textContent = 'Open';
  openLine.appendChild(openLabel);
  openLine.appendChild(open);
  detail.appendChild(openLine);

  for (const row of spaceDetailRows(space)) {
    const line = document.createElement('div');
    line.className = 'settings-row';
    const label = document.createElement('span');
    label.className = 'settings-row-label';
    label.textContent = row.label;
    const value = document.createElement('span');
    value.className = 'settings-row-value';
    value.textContent = row.value;
    line.appendChild(label);
    line.appendChild(value);
    detail.appendChild(line);
  }
  detail.appendChild(archiveTtlRow(space, selectedSpace));

  wrap.appendChild(list);
  wrap.appendChild(detail);
  body.appendChild(wrap);
}

// The six data-link rows: a fixed enum name, never a URL - the
// browser maps it to a chrome:// page (atrium_profile_page_url.cc) so this
// file can never name an arbitrary destination.
export const PROFILE_PAGES = [
  {key: 'privacy', label: 'Privacy and security'},
  {key: 'passwords', label: 'Passwords'},
  {key: 'payments', label: 'Credit cards'},
  {key: 'notifications', label: 'Notifications'},
  {key: 'clearBrowsingData', label: 'Clear browsing data'},
  {key: 'extensions', label: 'Manage extensions'},
];

// Words rather than a bare count - 0 reads as a number the user has to
// interpret, and the words say it outright.
export function profileCountLabel(profile) {
  const count = profile.spaceCount || 0;
  if (count === 0) {
    return 'Not used';
  }
  if (count === 1) {
    return '1 space';
  }
  return `${count} spaces`;
}

// The static fallback for Search engine and the two hibernate rows when the
// profile's own Chromium profile did not load. Downloads and Suggestions are
// apart - each renders its own always-present row instead.
export function profileDetailRows(profile) {
  if (!profile || profile.available) {
    return [];
  }
  return [
    {label: 'Search engine', value: 'Unavailable'},
    {label: 'Hibernate background tabs', value: 'Unavailable'},
    {label: 'Hibernation timeout', value: 'Unavailable'},
  ];
}

// The Name row: a real text input over `profile.name`, sent back by id -
// same convention as spaceNameRow, but a profile's id is the stable Chromium
// profile directory name rather than a sidebar position.
function profileNameRow(profile) {
  const line = document.createElement('div');
  line.className = 'settings-row';

  const label = document.createElement('span');
  label.className = 'settings-row-label';
  label.textContent = 'Name';

  const value = document.createElement('input');
  value.type = 'text';
  value.className = 'settings-row-value settings-row-text settings-profile-name';
  value.value = profile.name || '';

  line.appendChild(label);
  line.appendChild(value);
  return line;
}

// Routed through commitControl, not its own listener - this select has no
// PANE_ROWS key for setRow to find.
function profileEngineRow(profile) {
  const line = document.createElement('div');
  line.className = 'settings-row';

  const label = document.createElement('span');
  label.className = 'settings-row-label';
  label.textContent = 'Search engine';

  const enabledEngines = (settings.searchEngines || []).filter((e) => e.enabled);
  const current = profile.searchEngine || 'google';

  const value = document.createElement('select');
  value.className = 'settings-row-value settings-row-choice settings-profile-engine';
  for (const engine of enabledEngines) {
    const option = document.createElement('option');
    option.value = engine.id;
    option.textContent = engine.name;
    value.appendChild(option);
  }
  if (!enabledEngines.some((e) => e.id === current)) {
    const known = (settings.searchEngines || []).find((e) => e.id === current);
    const option = document.createElement('option');
    option.value = current;
    option.textContent = `${known ? known.name : current} (disabled)`;
    value.appendChild(option);
  }
  value.value = current;

  if (profileEngineApplyFailedId === profile.id) {
    const hint = document.createElement('small');
    hint.className = 'settings-row-hint settings-profile-engine-hint';
    hint.textContent = 'Saved, but could not switch this profile\'s searches yet';
    label.appendChild(hint);
  }

  line.appendChild(label);
  line.appendChild(value);
  return line;
}

// Reuses hibernateSelect and its two choice lists - a per-profile override of
// the same General ladder, not a third one.
function profileHibernateRows(profile) {
  const modeSelect = hibernateSelect(HIBERNATE_MODE_CHOICES, profile.hibernate);
  modeSelect.classList.add('settings-profile-hibernate-mode');
  const thresholdSelect =
      hibernateSelect(HIBERNATE_THRESHOLD_CHOICES, profile.hibThreshold);
  thresholdSelect.classList.add('settings-profile-hibernate-threshold');

  const modeLine = document.createElement('div');
  modeLine.className = 'settings-row';
  const modeLabel = document.createElement('span');
  modeLabel.className = 'settings-row-label';
  modeLabel.textContent = 'Hibernate background tabs';
  modeLine.appendChild(modeLabel);
  modeLine.appendChild(modeSelect);

  const thresholdLine = document.createElement('div');
  thresholdLine.className = 'settings-row';
  const thresholdLabel = document.createElement('span');
  thresholdLabel.className = 'settings-row-label';
  thresholdLabel.textContent = 'Hibernation timeout';
  thresholdLine.appendChild(thresholdLabel);
  thresholdLine.appendChild(thresholdSelect);

  return [modeLine, thresholdLine];
}

// The Suggestions toggle, backed by the profile's own search.suggest_enabled
// pref rather than a settings.json field - disabled and unlabeled when the
// profile did not load, since there is no live pref underneath it.
function profileSuggestRow(profile) {
  const line = document.createElement('div');
  line.className = 'settings-row';

  const label = document.createElement('span');
  label.className = 'settings-row-label';
  label.textContent = 'Search suggestions';

  const value = document.createElement('button');
  value.className = 'settings-row-value settings-row-toggle settings-profile-suggest';
  value.disabled = !profile.available;
  value.setAttribute('aria-pressed', String(Boolean(profile.suggestEnabled)));
  const knob = document.createElement('span');
  knob.className = 'settings-row-toggle-knob';
  value.appendChild(knob);
  const state = document.createElement('span');
  state.className = 'settings-row-toggle-state';
  state.textContent = profile.available ?
      (profile.suggestEnabled ? 'On' : 'Off') : 'Unavailable';
  value.appendChild(state);

  line.appendChild(label);
  line.appendChild(value);
  return line;
}

// The download row, backed by the profile's own download.default_directory
// pref. A button rather than a path field - the value only ever comes from
// the native folder picker (settingsProfileDownloadDirPick), never typed.
function profileDownloadRow(profile) {
  const line = document.createElement('div');
  line.className = 'settings-row';

  const label = document.createElement('span');
  label.className = 'settings-row-label';
  label.textContent = 'Download location';

  const value = document.createElement('button');
  value.className = 'settings-row-value settings-profile-download';
  value.disabled = !profile.available;
  value.textContent =
      profile.available ? (profile.downloadDir || '—') : 'Unavailable';

  line.appendChild(label);
  line.appendChild(value);
  return line;
}

// A row of its own rather than a seventh label: the sentence answers all six
// links, and settings-row is what supplies the padding and the seam.
function profilePagesHintRow(text) {
  const line = document.createElement('div');
  line.className = 'settings-row';
  const label = document.createElement('span');
  label.className = 'settings-row-label';
  const hint = document.createElement('small');
  hint.className = 'settings-row-hint settings-profile-pages-hint';
  hint.textContent = text;
  label.appendChild(hint);
  line.appendChild(label);
  return line;
}

// The six data links. Disabled when the profile did not load - the spec's
// own rule is that they do nothing rather than open on the wrong profile.
function profilePageRows(profile) {
  const wrap = document.createElement('div');
  wrap.className = 'settings-profile-pages';
  for (const page of PROFILE_PAGES) {
    const line = document.createElement('div');
    line.className = 'settings-row';
    const label = document.createElement('span');
    label.className = 'settings-row-label';
    label.textContent = page.label;
    const value = document.createElement('button');
    value.className = 'settings-row-value settings-profile-page';
    value.dataset.page = page.key;
    value.disabled = !profile.available;
    value.textContent = 'Open';
    line.appendChild(label);
    line.appendChild(value);
    wrap.appendChild(line);
  }
  if (!profile.available) {
    wrap.appendChild(profilePagesHintRow(
        'This profile is not loaded, so its pages cannot open'));
  } else if (profilePageRefusedId === profile.id) {
    wrap.appendChild(profilePagesHintRow(
        'Atrium could not open a window for this profile'));
  }
  return wrap;
}

// Delete refuses while a space still uses the profile - the hint says why
// rather than leaving a disabled button unexplained.
function profileDeleteRow(profile) {
  const line = document.createElement('div');
  line.className = 'settings-row';

  const label = document.createElement('span');
  label.className = 'settings-row-label';
  label.textContent = 'Delete profile';
  if (profile.spaceCount > 0) {
    const hint = document.createElement('small');
    hint.className = 'settings-row-hint settings-profile-delete-hint';
    hint.textContent = `Used by ${profileCountLabel(profile)} - move them first`;
    label.appendChild(hint);
  }

  const value = document.createElement('button');
  value.className = 'settings-row-value settings-profile-delete';
  value.disabled = profile.spaceCount > 0;
  value.textContent = 'Delete';

  line.appendChild(label);
  line.appendChild(value);
  return line;
}

// The Profiles pane: a list of profiles beside a detail panel,
// the same master-detail shape renderSpaces uses.
export function renderProfiles(body) {
  if (!profiles.length) {
    return;  // Nothing pushed yet; the heading stands alone rather than lying.
  }
  const wrap = document.createElement('div');
  wrap.className = 'settings-profiles';

  const list = document.createElement('div');
  list.className = 'settings-profile-list';
  profiles.forEach((profile, index) => {
    const item = document.createElement('button');
    item.className = 'settings-profile-item';
    item.dataset.profile = String(index);
    if (index === selectedProfile) {
      item.classList.add('selected');
    }
    const name = document.createElement('span');
    name.textContent = profile.name || 'Untitled';
    const count = document.createElement('span');
    count.className = 'settings-profile-count';
    count.textContent = profileCountLabel(profile);
    item.appendChild(name);
    item.appendChild(count);
    list.appendChild(item);
  });
  const newProfile = document.createElement('button');
  newProfile.className = 'settings-profile-item settings-profile-new';
  newProfile.textContent = '+ New Profile';
  list.appendChild(newProfile);

  const detail = document.createElement('div');
  detail.className = 'settings-profile-detail';
  const profile = profiles[selectedProfile];
  detail.appendChild(profileNameRow(profile));

  if (profile.available) {
    detail.appendChild(profileEngineRow(profile));
    for (const row of profileHibernateRows(profile)) {
      detail.appendChild(row);
    }
  } else {
    for (const row of profileDetailRows(profile)) {
      const line = document.createElement('div');
      line.className = 'settings-row';
      const label = document.createElement('span');
      label.className = 'settings-row-label';
      label.textContent = row.label;
      const value = document.createElement('span');
      value.className = 'settings-row-value';
      value.textContent = row.value;
      line.appendChild(label);
      line.appendChild(value);
      detail.appendChild(line);
    }
  }

  detail.appendChild(profileSuggestRow(profile));
  detail.appendChild(profileDownloadRow(profile));
  detail.appendChild(profilePageRows(profile));
  detail.appendChild(profileDeleteRow(profile));

  wrap.appendChild(list);
  wrap.appendChild(detail);
  body.appendChild(wrap);
}

// Query/fragment names an Azure or Google-style endpoint puts an API key
// under. A heads-up match only - Rust's `custom_endpoint_url_is_legal` is the
// actual gate and refuses a matching URL even if this list misses a name.
const CUSTOM_ENDPOINT_CREDENTIAL_PARAMS = [
  'key', 'api-key', 'api_key', 'apikey', 'subscription-key', 'token',
  'access_token', 'secret', 'password', 'auth', 'authorization',
];

// Whether `url`'s query string or fragment names one of the params above.
function customEndpointUrlLooksCredentialBearing(url) {
  const afterScheme = url.split(/[?#]/).slice(1).join('&');
  if (!afterScheme) {
    return false;
  }
  return afterScheme.split(/[&;]/).some((pair) => {
    const name = decodeURIComponent(pair.split('=')[0] || '').toLowerCase();
    return CUSTOM_ENDPOINT_CREDENTIAL_PARAMS.includes(name);
  });
}

// What travels on the wire, which is not what the pane holds: a half-typed
// engine belongs on screen and nowhere else.
function settingsPayload() {
  return JSON.stringify(
      settings.searchEngines
          ? {...settings, searchEngines: shippableEngines(settings.searchEngines)}
          : settings);
}

// Passed to renderRoutingPane as `send`, the same shape sendSettings gives
// renderEnginesPane - but the payload is routing.json's own object, not a
// key inside settings.
// Returns root: renderSettings's own focus-restore runs after this returns
// and can steal focus back to the rail, so a caller that needs to focus a
// freshly rendered field must re-query and refocus AFTER calling send.
function sendRouting(root, next, {rerenderOnly = false} = {}) {
  if (rerenderOnly) {
    routing = next;
    renderSettings(root, currentPane);
    return root;
  }
  routingPendingRevert = routing;
  const defaultTargetSpaceID = spaces.some((s) => s.id === next.defaultTargetSpaceID)
      ? next.defaultTargetSpaceID : '';
  routing = {...next, defaultTargetSpaceID};
  saveError = null;
  renderSettings(root, currentPane);
  chrome.send('settingsRoutingRulesChanged', [JSON.stringify({
    schemaVersion: Math.max(next.schemaVersion, 2),
    defaultTargetSpaceID,
    rules: shippableRules(next.rules, spaces),
  })]);
  return root;
}

// Called by the browser with what atrium_core_save_routing returned - the
// same codes atrium_core_save_settings uses, so setSaveResult's own message
// table answers for routing too.
export function setRoutingSaveResult(code) {
  if (code === 1) {
    saveError = null;
    if (rootEl) {
      syncSaveErrorBanner(rootEl);
    }
    return;
  }
  saveError = SAVE_FAILURES[code] || 'Atrium could not save that change.';
  if (routingPendingRevert) {
    routing = routingPendingRevert;
  }
  routingPendingRevert = null;
  if (rootEl) {
    renderSettings(rootEl, currentPane);
  }
}

// Passed to renderInputs as `send` - the whole actions map back, the same
// round trip sendRouting makes for routing.json. `next` already carries
// every action, so which row changed is the caller's business, not this
// function's.
// `forget` names the user entries the core drops before the file is written.
// The rows still travel: a reset is the browser's to apply, not this pane's
// to guess, and what it writes is what comes back on the next push.
function sendHotkeys(root, next, forget = {}) {
  hotkeysPendingRevert = {actions: hotkeys, preset: hotkeyPreset};
  hotkeys = next;
  // The picker travels here too: switching preset is forgetAll plus the name
  // the file should merge over from now on.
  hotkeyPreset = forget.preset || hotkeyPreset;
  saveError = null;
  renderSettings(root, currentPane);
  chrome.send('settingsHotkeysChanged', [JSON.stringify(
      {schemaVersion: hotkeysSchemaVersion, preset: hotkeyPreset,
       actions: next, ...forget})]);
}

// Passed to renderEnginesPane as `send`. rerenderOnly is a selection change
// within the pane (picking a row to edit), not an edit to save - the object
// still swaps so the detail panel reflects it, but nothing goes over the wire.
function sendSettings(root, next, {rerenderOnly = false} = {}) {
  if (rerenderOnly) {
    settings = next;
    renderSettings(root, currentPane);
    return;
  }
  const id = beginSettingsSave(next);
  saveError = null;
  renderSettings(root, currentPane);
  chrome.send('settingsChanged', [settingsPayload(), id]);
}

// Sets one row to a new value and sends the settings back for the browser to
// write. Shared by all three control shapes (generalized from
// the toggle-only toggleRow) - what differs between a flipped bool, a picked
// choice and a typed number is only the value each control hands in.
//
// The WHOLE object travels, not the one key. The core refuses a partial one
// rather than completing it from defaults: every key past the first four is
// optional, so a missing key and a default are the same bytes, and completing
// one would reset every field this surface did not send.
function setRow(root, key, nextValue) {
  const row = (PANE_ROWS[currentPane] || []).find((r) => r.key === key);
  if (!row || controlKind(row) === null) {
    return;
  }
  // Refused here rather than sent and normalized away silently: the core has
  // no round trip back to this window after a save (PushSettings only runs on
  // open), so a save that dropped the key on its own would leave the row
  // showing the very value that never reached disk.
  if (key === 'customEndpointUrl' && nextValue !== '' &&
      customEndpointUrlLooksCredentialBearing(nextValue)) {
    saveError = 'That endpoint URL looks like it carries an API key in its ' +
        'query string (a parameter such as key, api-key, or token). Atrium ' +
        'did not save it - put the key in the API key field above instead.';
    // `settings` never changed, so this only undoes the rejected text still
    // sitting in the input. A full render paints the banner on its own, so
    // only the narrow path also needs syncSaveErrorBanner.
    if (updateRowAndCompanions(root, key)) {
      syncSaveErrorBanner(root);
    } else {
      renderSettings(root, currentPane);
    }
    return;
  }
  const id = beginSettingsSave({...settings, [key]: nextValue});
  // A live check must not survive the switch that turned it off - the
  // rebuild below removes the row, but the fetch itself keeps running
  // without this.
  if (key === 'aiEnabled' && nextValue === false && ollamaAbort) {
    ollamaAbort.abort();
    ollamaAbort = null;
  }
  // A key with no endpoint left is dead weight, not paused config - unlike
  // aiEnabled, a reversible kill switch that leaves this row untouched.
  if (key === 'customEndpointUrl' && nextValue === '') {
    clearCustomEndpointApiKey();
  }
  // The previous failure is about the previous attempt; this one answers for
  // itself.
  saveError = null;
  if (updateRowAndCompanions(root, key)) {
    syncSaveErrorBanner(root);
  } else {
    renderSettings(root, currentPane);
  }
  chrome.send('settingsChanged', [settingsPayload(), id]);
}

function toggleRow(root, key) {
  const row = (PANE_ROWS[currentPane] || []).find((r) => r.key === key);
  if (!row || !isToggleable(row)) {
    return;
  }
  setRow(root, key, !settings[key]);
}

export function installSettings(root) {
  // Marks the root as this route's viewport, mirroring sidebar.js's 'sbroot':
  // without it settings.css's grid rule would be unreachable on this route.
  root.classList.add('settingsroot');

  // The settings push carries vimModeEnabled already, so this window reads the
  // toggle from the store it is editing rather than from a second route that
  // would have to be kept in step with it.
  installVimSurface({
    root,
    isEnabled: () => !!settings.vimModeEnabled,
    selector: '.settings-row',
    activate: (row) => row.click(),
    ownsKeys: yieldsToEditableOnly,
  });

  // The browser calls this with the core's settings once the route announces
  // itself. Global rather than passed in: chrome.send is one-way, so the push
  // has to land on something the page owns.
  window.atriumSetSettings = setSettings;
  window.atriumSetHotkeys = setHotkeys;
  window.atriumSetHotkeysSaveResult = setHotkeysSaveResult;
  window.atriumSetSpaces = setSpaces;
  window.atriumSetSaveResult = setSaveResult;
  window.atriumSetRoutingRules = setRoutingRules;
  window.atriumSetRoutingSaveResult = setRoutingSaveResult;
  window.atriumSetImportPreview = setImportPreview;
  window.atriumSetImportResult = setImportResult;
  window.atriumSetProfileEngineApplyFailed = setProfileEngineApplyFailed;
  window.atriumSettingsProfilePageRefused = setProfilePageRefused;
  window.atriumSetCustomEndpointApiKeyStatus = setCustomEndpointApiKeyStatus;
  window.atriumSetCustomEndpointConnectionStatus = setCustomEndpointConnectionStatus;
  window.atriumSetSettingsAlwaysOnTop = setSettingsAlwaysOnTop;
  // how the browser opens settings ON a pane. It routes through
  // selectPane, so a deep link and a rail click resolve and report the same
  // way - an unknown or retired name included.
  window.atriumSelectSettingsPane = (id) => {
    if (id) {
      selectPane(id);
    }
  };
  // How the sidebar's Change Theme lands on one space. selectPane no-ops when
  // the Spaces pane is already open, so the rebuild is asked for by hand.
  window.atriumSelectSettingsSpace = (index) => {
    const at = Number(index);
    if (!Number.isInteger(at) || at < 0 || at >= spaces.length) {
      return;
    }
    selectedSpace = at;
    if (currentPane === 'spaces') {
      renderSettings(root, currentPane);
    } else {
      selectPane('spaces');
    }
  };
  rootEl = root;

  let selected = renderSettings(root, PANES[0].id);
  currentPane = selected;

  root.addEventListener('click', (event) => {
    const searchResult = event.target.closest('.settings-search-result');
    if (searchResult) {
      chooseSearchResult(searchHits[Number(searchResult.dataset.index)]);
      return;
    }
    // Before the generic toggle below: this button shares its styling but
    // is keyed by profile id, not a PANE_ROWS key, so toggleRow would no-op.
    const profileSuggest = event.target.closest('.settings-profile-suggest');
    if (profileSuggest && !profileSuggest.disabled) {
      const profile = profiles[selectedProfile];
      chrome.send(
          'settingsProfileSuggestChanged', [profile.id, !profile.suggestEnabled]);
      return;
    }
    const hideSidebar = event.target.closest('.settings-row-hide-sidebar');
    if (hideSidebar) {
      const next = hideSidebar.getAttribute('aria-pressed') !== 'true';
      settings = {...settings, sidebarHidden: next};
      chrome.send('settingsSidebarHiddenChanged', [next]);
      renderSettings(root, currentPane);
      return;
    }
    if (event.target.closest('.settings-row-make-default')) {
      defaultBrowserAsked = true;
      chrome.send('settingsMakeDefaultBrowser', []);
      renderSettings(root, currentPane);
      return;
    }
    const toggle = event.target.closest('.settings-row-toggle');
    if (toggle) {
      toggleRow(root, toggle.closest('.settings-row').dataset.key);
      return;
    }
    if (event.target.closest('.settings-pin')) {
      chrome.send('settingsAlwaysOnTopChanged', [!alwaysOnTop]);
      return;
    }
    if (event.target.closest('.settings-row-credits')) {
      chrome.send('settingsOpenCredits', []);
      return;
    }
    if (event.target.closest('.settings-row-test-connection')) {
      // renderSettings can throw mid-check (e.g. the window closed and root
      // is detached) - catch it here so the rejection is logged, not lost.
      runOllamaConnectionCheck(root).catch(
          (err) => console.error('runOllamaConnectionCheck failed:', err));
      return;
    }
    if (event.target.closest('.settings-row-test-custom-endpoint-connection')) {
      runCustomEndpointConnectionCheck(root);
      return;
    }
    if (event.target.closest('.settings-row-clear-api-key')) {
      clearCustomEndpointApiKey();
      return;
    }
    const preset = event.target.closest('.settings-swipe-preset');
    if (preset) {
      applySwipePreset(root, preset.dataset.preset);
      return;
    }
    const segOption = event.target.closest('.settings-seg-option');
    if (segOption) {
      setRow(root, segOption.closest('.settings-row').dataset.key,
             segOption.dataset.choice);
      return;
    }
    const appIcon = event.target.closest('.settings-app-icon');
    if (appIcon) {
      setRow(root, 'appIconVariant', appIcon.dataset.variant);
      return;
    }
    if (event.target.closest('.settings-space-new')) {
      spaceCreatedLocally = true;
      chrome.send('settingsSpaceCreated', []);
      return;
    }
    if (event.target.closest('.settings-space-open')) {
      chrome.send('settingsSpaceOpened', [selectedSpace]);
      return;
    }
    const spaceItem = event.target.closest('.settings-space-item');
    if (spaceItem) {
      selectedSpace = Number(spaceItem.dataset.space);
      renderSettings(root, currentPane);
      return;
    }
    if (event.target.closest('.settings-profile-new')) {
      profileCreatedLocally = true;
      chrome.send('settingsProfileAdded', []);
      return;
    }
    const profileDelete = event.target.closest('.settings-profile-delete');
    if (profileDelete && !profileDelete.disabled) {
      chrome.send('settingsProfileDeleted', [profiles[selectedProfile].id]);
      return;
    }
    const profileDownload = event.target.closest('.settings-profile-download');
    if (profileDownload && !profileDownload.disabled) {
      chrome.send('settingsProfileDownloadDirPick', [profiles[selectedProfile].id]);
      return;
    }
    const profilePage = event.target.closest('.settings-profile-page');
    if (profilePage && !profilePage.disabled) {
      // Read before the re-render below, which replaces this button.
      const page = profilePage.dataset.page;
      if (profilePageRefusedId !== null) {
        profilePageRefusedId = null;
        renderSettings(root, currentPane);
      }
      chrome.send(
          'settingsProfileOpenPage', [profiles[selectedProfile].id, page]);
      return;
    }
    const profileItem = event.target.closest('.settings-profile-item');
    if (profileItem) {
      selectedProfile = Number(profileItem.dataset.profile);
      renderSettings(root, currentPane);
      return;
    }
    const item = event.target.closest('.settings-rail-item');
    if (!item) {
      return;
    }
    selectPane(item.dataset.pane);
  });

  // The Glass opacity slider's drag or keyboard step - live-repaints the
  // fill and the paired num field, same as a native range's own thumb would,
  // without committing until 'change' below.
  root.addEventListener('input', (event) => {
    const range = event.target.closest('.settings-slider-range');
    if (!range) {
      return;
    }
    paintSliderFill(range);
    const num = range.closest('.settings-slider')?.querySelector('.settings-slider-num');
    if (num) {
      num.value = range.value;
    }
  });

  // Shared by the click and the arrow keys, so a keyboard selection is the
  // same event on the wire as a mouse one.
  //
  // keepFocus: renderSettings() rebuilds the tree, so the element the key was
  // pressed on no longer exists. Without moving focus onto its replacement the
  // window drops focus to the body and the NEXT arrow key does nothing. A
  // click needs no such repair - the pointer is still where it was.
  function selectPane(id, {keepFocus = false} = {}) {
    const next = paneOrDefault(id);
    if (next === selected) {
      return;  // Re-selecting the open pane is not a change to report.
    }
    selected = renderSettings(root, next);
    currentPane = selected;
    if (keepFocus) {
      root.querySelector(`.settings-rail-item[data-pane="${selected}"]`)
          .focus();
    }
    chrome.send('settingsPaneSelected', [selected]);
  }

  // selectPane no-ops when the target pane is already open, so a mark from
  // an earlier choice on that same pane survives its rebuild unless cleared.
  function chooseSearchResult(hit) {
    if (!hit) {
      return;
    }
    selectPane(hit.pane);
    root.querySelectorAll('.settings-row.found')
        .forEach((row) => row.classList.remove('found'));
    root.querySelector(searchResultSelector(hit))?.classList.add('found');
  }

  // Moves the highlighted search result by `delta`, wrapping - the field's
  // own ArrowUp/ArrowDown, kept apart from the rail's arrows below.
  function moveSearchActive(delta) {
    const results = root.querySelectorAll('.settings-search-result');
    if (!results.length) {
      return;
    }
    results[searchActiveIndex]?.classList.remove('active');
    searchActiveIndex = (searchActiveIndex + delta + results.length) % results.length;
    results[searchActiveIndex].classList.add('active');
  }

  // Clears the field before anything answers Escape. Global, not on
  // the field alone - a query can be cleared from wherever focus landed.
  function clearSearch() {
    searchQuery = '';
    const field = root.querySelector('.settings-search-field');
    if (field) {
      field.value = '';
    }
    refreshSearchResults(root.querySelector('.settings-search-results'));
  }

  // Both axes: the rail reads as a vertical list, but the surface
  // it replaced was navigated with left/right and either reading is defensible
  // from the shape alone. Wrapping rather than dead-ending, so a held arrow key
  // never looks stuck.
  const RAIL_STEP = {ArrowDown: 1, ArrowRight: 1, ArrowUp: -1, ArrowLeft: -1};

  function railKey(event) {
    const item = event.target.closest('.settings-rail-item');
    if (!item) {
      return false;
    }
    const from = PANES.findIndex((p) => p.id === paneOrDefault(item.dataset.pane));
    let to;
    if (event.key in RAIL_STEP) {
      to = (from + RAIL_STEP[event.key] + PANES.length) % PANES.length;
    } else if (event.key === 'Home') {
      to = 0;
    } else if (event.key === 'End') {
      to = PANES.length - 1;
    } else {
      return false;
    }
    selectPane(PANES[to].id, {keepFocus: true});
    return true;
  }

  // The choice and number control shapes are native form controls, not
  // buttons - a pick or a typed value commits on 'change', not 'click'.
  // Shared with the keydown handler below, so both take the same route to
  // setRow.
  function commitControl(target) {
    // Before the generic choice branch: this select shares its styling but
    // has no PANE_ROWS key for setRow to find.
    const aiModel = target.closest('.settings-row-ai-model');
    if (aiModel) {
      setSelectedAiModel(root, aiModel.value);
      return;
    }
    // Before the generic choice branch, same reason as aiModel above: keyed
    // by profile id, not a PANE_ROWS entry.
    const profileEngine = target.closest('.settings-profile-engine');
    if (profileEngine) {
      // Read before the re-render below, which replaces this select.
      const engine = profileEngine.value;
      if (profileEngineApplyFailedId !== null) {
        profileEngineApplyFailedId = null;
        renderSettings(root, currentPane);
      }
      chrome.send(
          'settingsProfileSearchEngineChanged',
          [profiles[selectedProfile].id, engine]);
      return;
    }
    // Sent together: the mode and the threshold travel as one message
    // however either was changed.
    const profileHibernate = target.closest(
        '.settings-profile-hibernate-mode, .settings-profile-hibernate-threshold');
    if (profileHibernate) {
      const mode = root.querySelector('.settings-profile-hibernate-mode').value;
      const threshold =
          root.querySelector('.settings-profile-hibernate-threshold').value;
      chrome.send(
          'settingsProfileHibernateChanged',
          [profiles[selectedProfile].id, mode, threshold]);
      return;
    }
    // Before the generic choice branch, same reason as profileEngine above:
    // keyed by sidebar position, not a PANE_ROWS entry.
    const spaceProfile = target.closest('.settings-space-profile');
    if (spaceProfile) {
      chrome.send('settingsSpaceProfileChanged', [selectedSpace, spaceProfile.value]);
      return;
    }
    const choice = target.closest('.settings-row-choice');
    if (choice) {
      setRow(root, choice.closest('.settings-row').dataset.key, choice.value);
      return;
    }
    // A separate branch from .settings-row-number below: a range's own value
    // is always a valid in-range number, so it skips that branch's blank/NaN
    // guard for a typed field.
    const range = target.closest('.settings-slider-range');
    if (range) {
      setRow(root, range.closest('.settings-row').dataset.key, Number(range.value));
      return;
    }
    const number = target.closest('.settings-row-number');
    if (number) {
      // Number('') is 0, not NaN - an emptied field is a real input a user
      // can produce (select-all then Delete, then Enter) and must not commit
      // as if they had typed a valid 0. Reject blank text explicitly rather
      // than trusting Number() alone.
      const trimmed = number.value.trim();
      const parsed = Number(trimmed);
      if (trimmed !== '' && !Number.isNaN(parsed)) {
        setRow(
            root, number.closest('.settings-row').dataset.key, parsed);
      }
      return;
    }
    // Before the generic text branch: this input reuses .settings-row-text's
    // border/padding, but has no PANE_ROWS entry for setRow to find.
    const apiKey = target.closest('.settings-row-api-key');
    if (apiKey) {
      sendCustomEndpointApiKey(apiKey.value);
      apiKey.value = '';  // Never let the typed plaintext linger in the DOM.
      return;
    }
    // Before the generic text branch, same reason as the API key field: no
    // PANE_ROWS entry. An empty commit is dropped - a space's dot always
    // needs a name.
    const spaceName = target.closest('.settings-space-name');
    if (spaceName) {
      const trimmed = spaceName.value.trim();
      if (trimmed && trimmed !== spaces[selectedSpace].name) {
        spaces[selectedSpace].name = trimmed;
        chrome.send('settingsSpaceRenamed', [selectedSpace, trimmed]);
        renderSettings(root, currentPane);
      }
      return;
    }
    // Before the generic text branch, same reason as spaceName above. An
    // empty commit is dropped - a profile always needs a name.
    const profileName = target.closest('.settings-profile-name');
    if (profileName) {
      const trimmed = profileName.value.trim();
      const profile = profiles[selectedProfile];
      if (profile && trimmed && trimmed !== profile.name) {
        profile.name = trimmed;
        chrome.send('settingsProfileRenamed', [profile.id, trimmed]);
        renderSettings(root, currentPane);
      }
      return;
    }
    // The core re-validates a host on its own load path (settings.rs's
    // valid_ollama_host), so an empty or malformed commit is safe to send -
    // a rejected value falls to the default rather than corrupting the file.
    const text = target.closest('.settings-row-text');
    if (text) {
      setRow(root, text.closest('.settings-row').dataset.key, text.value.trim());
    }
  }

  // Only the results list is repainted, never the rail - a full renderSettings
  // per keystroke would reset the field's own caret position.
  root.addEventListener('input', (event) => {
    if (!event.target.classList.contains('settings-search-field')) {
      return;
    }
    searchQuery = event.target.value;
    refreshSearchResults(root.querySelector('.settings-search-results'));
  });

  root.addEventListener('change', (event) => commitControl(event.target));

  // Select the number field's whole value on focus, so typing
  // replaces it without the user having to reach for Cmd+A first. 'focusin'
  // rather than 'focus' - focus does not bubble, and this listener lives on
  // root the same way every other one here does. select() is a real no-op
  // fix now that the field is type=text (see the comment at its creation
  // above); it silently did nothing on the old type=number input.
  root.addEventListener('focusin', (event) => {
    const number = event.target.closest('.settings-row-number');
    if (number) {
      number.select();
    }
  });

  // 'change' fires on a mouse pick or on blur, but Return alone does neither
  // reliably: a <select> driven by arrow-then-Return does not always raise
  // 'change' without an intervening blur, and a number input committed the
  // same way has the identical gap - Return does not blur the field. This
  // adds the keyboard commit both controls were missing, on Enter only, so
  // the number input still does not save on every keystroke the way an
  // 'input' listener would.
  root.addEventListener('keydown', (event) => {
    if (event.target.closest('.settings-search-field')) {
      if ((event.key === 'ArrowDown' || event.key === 'ArrowUp') &&
          searchHits.length) {
        moveSearchActive(event.key === 'ArrowDown' ? 1 : -1);
        event.preventDefault();
        return;
      }
      if (event.key === 'Enter' && searchHits.length) {
        chooseSearchResult(searchHits[searchActiveIndex]);
        event.preventDefault();
        return;
      }
    }
    // Global, and before the rail: a query can be cleared from anywhere in
    // the window, and only an already-empty field lets Escape fall through
    // to whatever closes the window.
    if (event.key === 'Escape' && searchQuery) {
      clearSearch();
      event.preventDefault();
      return;
    }
    // The rail first, and only when the key landed ON a rail item - a <select>
    // row uses the same arrows to change its own value.
    if (railKey(event)) {
      event.preventDefault();  // Arrows must not also scroll the rail.
      return;
    }
    if (event.key === 'Enter') {
      commitControl(event.target);
    }
  });

  // On the rail rather than nowhere: the window opens with no text
  // field to claim focus, and a body-focused window answers the first arrow
  // key with silence.
  root.querySelector(`.settings-rail-item[data-pane="${selected}"]`).focus();

  chrome.send('settingsReady', [selected]);
}
