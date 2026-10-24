// Copyright 2026 The Atrium Authors
//
// One origin, six path routes. This module picks the route from
// location.pathname and hands off; each route owns its own DOM.
//
// No innerHTML anywhere in this tree. The default WebUI CSP sets
// require-trusted-types-for 'script' with an EMPTY trusted-types allowlist, so
// createPolicy() throws for every name and any injection-sink write is blocked
// outright.

import {installQuickMenu} from './quickmenu.js';
import {installSpaceIconPicker} from './space-icon-picker.js';
import {installFoyer} from './foyer.js';
import {installSidebar} from './sidebar.js';
import {installCommandBar} from './commandbar.js';
import {installSettings} from './settings.js';
import {installSkylight} from './skylight.js';
import {installCourtyard} from './courtyard-view.js';
import {installNotes} from './notes.js';
import {installAiPanel, createNativeAiBackend} from './ai-panel.js';
import {installDevBar} from './devbar.js';
import {installPermissionSheet} from './permsheet.js';
import {installPeekControls} from './peek-controls.js';
import {installHintOverlay} from './hint.js';
import {installToastSurface} from './toast-surface.js';
import {installFind} from './find.js';
import {installThemeWindow} from './theme-window.js';
import {installVimSurface} from './vim-surface.js';

const ROUTES = {
  '/quickmenu': installQuickMenu,
  '/spaceicon': installSpaceIconPicker,
  '/sidebar': installSidebar,
  '/foyer': installFoyer,
  '/commandbar': installCommandBar,
  '/settings': installSettings,
  '/courtyard': installCourtyard,
  '/notes': installNotes,
  '/ai-panel': (root) => installAiPanel(root, {backend: createNativeAiBackend()}),
  '/devbar': installDevBar,
  '/permsheet': installPermissionSheet,
  '/peekctl': installPeekControls,
  '/hint': installHintOverlay,
  '/toast': installToastSurface,
  '/find': installFind,
  '/theme': installThemeWindow,
};

// Only the sidebar renders a per-space background today.
const SKYLIGHT_ROUTES = new Set(['/sidebar']);

// The command bar and settings are out on purpose - the surface
// table in docs/vim-mode-design.md says what each one costs.
const VIM_ROUTES = new Map([
  ['/sidebar', {selector: '.row.tabrow'}],
]);

function main() {
  const root = document.getElementById('atrium-root');
  const install = ROUTES[location.pathname];
  if (install) {
    install(root);
    if (SKYLIGHT_ROUTES.has(location.pathname)) {
      // the state arrives after the first paint, so the surface
      // installs with the defaults and re-installs when the push lands.
      installSkylight(root);
      window.atriumSetSkylight = (weather, nightShift) => {
        installSkylight(root, {weather, nightShift});
      };
    }
    const vim = VIM_ROUTES.get(location.pathname);
    if (vim) {
      // Installed either way, because the toggle arrives after the first
      // paint and a surface that installed only when it was already on would
      // stay dead until the next reload.
      let vimOn = false;
      installVimSurface({
        root,
        isEnabled: () => vimOn,
        selector: vim.selector,
        scroller: vim.scroller && (() => vim.scroller(root)),
      });
      window.atriumSetVimModeEnabled = (on) => {
        vimOn = !!on;
      };
    }
    return;
  }
  // Unrouted paths are a development aid, not a surface. Kept as text so a
  // typo in a route name is visible rather than a blank window.
  root.appendChild(
      document.createTextNode(`atrium: no route for ${location.pathname}`));
  chrome.send('atriumReady', [location.pathname]);
}

main();
