// Copyright 2026 The Atrium Authors
//
// shares one .bglayer between day-cycle and ambient.
// day-cycle's paint() stays the sole background writer -
// only its composer changes, to weather's math (which reuses dayCycleLayers).

import {setSkylightDayCycle} from './skylight-day-cycle.js';
import {setSkylightAmbient} from './skylight-ambient.js';
import {spaceWeatherBackground} from './skylight-weather.js';

// TRANSPARENT, and that is the whole point of it.
//
// The lab paints this base because its sidebar sits in a mock window that has
// no backdrop of its own (swipe-lab.html). Atrium's does: the window frosts
// the desktop behind it and paints the space tint over that, and the sidebar
// reads through because its WebContents is transparent -
// a surface hosted IN the main window gets the glass for free.
//
// So an opaque base here covers the very thing it is supposed to sit on. The
// reporter's own screenshot measured rgb(12, 14, 18) at the foot of the
// sidebar against this gradient's #0B0D10, with the surround beside it at
// rgb(52, 53, 54) - the glass reaching everywhere except the one column this
// layer covers. No glass-opacity setting can win against it, which is why
// dropping that setting from 0.9 to 0.22 changed nothing.
//
// The sky layers themselves stay: day-cycle and weather both compose in rgba,
// so they tint what is behind them instead of replacing it.
const BASE = 'linear-gradient(rgba(20, 24, 29, 0), rgba(11, 13, 16, 0))';

/** Wires all three Skylight effects onto `root`'s .bglayer, creating it if absent. */
export function installSkylight(
    root, {ambient = true, weather = 'clear', nightShift = true} = {}) {
  let layer = root.querySelector('.bglayer');
  if (!layer) {
    layer = document.createElement('div');
    layer.className = 'bglayer';
    root.insertBefore(layer, root.firstChild);
  }
  setSkylightDayCycle(BASE, {
    host: layer,
    nightShift,
    background: (b, opts) => spaceWeatherBackground(b, {...opts, weather}),
  });
  setSkylightAmbient(ambient, {host: root});
  return layer;
}
