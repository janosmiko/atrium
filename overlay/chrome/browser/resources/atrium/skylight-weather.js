// Copyright 2026 The Atrium Authors
//
// The weather-wash half of skyLayers(), ported from swipe-lab.html:2089-2098.

import {dayCycleLayers} from './skylight-day-cycle.js';

/** Weather wash layers for `weather` ('clear'|'overcast'|'rain'). */
export function weatherLayers(weather) {
  const layers = [];
  if (weather === 'rain') {
    layers.push(
        'repeating-linear-gradient(100deg, rgba(160,190,220,.07) 0 1px, transparent 1px 9px)');
  }
  if (weather !== 'clear') {
    const wa = weather === 'rain' ? '.22' : '.16';
    layers.push(`linear-gradient(rgba(110,122,134,${wa}),rgba(110,122,134,${wa}))`);
  }
  return layers;
}

function liveHour() {
  const now = new Date();
  return now.getHours() + now.getMinutes() / 60;
}

// Weather washes push first in the lab's own skyLayers(), so they paint on
// top of the day-cycle warm/dark tint - same layer order, ported verbatim.
export function spaceWeatherBackground(base, {weather = 'clear', nightShift = true, hour} = {}) {
  const day = nightShift ? dayCycleLayers(hour ?? liveHour()) : [];
  const layers = [...weatherLayers(weather), ...day];
  return layers.length ? layers.join(',') + ',' + base : base;
}

/** Paints `host` (default #atrium-root) with the weather-aware sky background. */
export function setSkylightWeather(base, {host, weather = 'clear', nightShift = true, hour} = {}) {
  const el = host || document.getElementById('atrium-root');
  if (!el) {
    return null;
  }
  el.style.background = spaceWeatherBackground(base, {weather, nightShift, hour});
  return el;
}
