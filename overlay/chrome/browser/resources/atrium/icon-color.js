// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

// The colour a favicon reads as, for the ring around its row and tile.

// Chroma below this is a grey or white mark, which says nothing about its site.
export const MIN_ICON_CHROMA = 40;

// Weighted by chroma plus one, so a coloured mark wins over its white or
// grey ground. Null when the mark carries too little colour to name one.
export function dominantColor(rgba) {
  let r = 0, g = 0, b = 0, n = 0;
  for (let i = 0; i + 3 < rgba.length; i += 4) {
    if (rgba[i + 3] < 128) {
      continue;
    }
    const cr = rgba[i], cg = rgba[i + 1], cb = rgba[i + 2];
    const w = Math.max(cr, cg, cb) - Math.min(cr, cg, cb) + 1;
    r += cr * w;
    g += cg * w;
    b += cb * w;
    n += w;
  }
  if (n === 0) {
    return null;
  }
  const out = [Math.round(r / n), Math.round(g / n), Math.round(b / n)];
  if (Math.max(...out) - Math.min(...out) < MIN_ICON_CHROMA) {
    return null;
  }
  return out;
}

// What the ring has to clear against the row behind it. WCAG's bar for a
// graphical control, and the ring is exactly that: a shape with no text in it.
export const RING_MIN_CONTRAST = 3;

// The readable-text contrast threshold, reused here to tell a dark row from a light one:
// InkForBackdrop (atrium_page_color.cc) keeps white ink while white clears
// this, and swaps to black where it does not.
export const READABLE_CONTRAST = 4.5;

const WHITE = [255, 255, 255];
const BLACK = [0, 0, 0];

function channelLuminance(value) {
  const c = value / 255;
  return c <= 0.03928 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4);
}

export function relativeLuminance([r, g, b]) {
  return 0.2126 * channelLuminance(r) + 0.7152 * channelLuminance(g) +
      0.0722 * channelLuminance(b);
}

export function contrastRatio(a, b) {
  const la = relativeLuminance(a);
  const lb = relativeLuminance(b);
  return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05);
}

// A CSS colour as [r, g, b], or null. A tint that never resolved to a colour -
// a bare preset name - has to answer null, so the ring is left where it is
// rather than adjusted against a guess.
export function parseRgb(css) {
  if (!css) {
    return null;
  }
  const text = String(css).trim();
  const hex = /^#([0-9a-f]{3}|[0-9a-f]{6})$/i.exec(text);
  if (hex) {
    const digits = hex[1];
    const wide = digits.length === 6;
    return [0, 1, 2].map((i) => parseInt(
        wide ? digits.slice(i * 2, i * 2 + 2) : digits[i] + digits[i], 16));
  }
  // hsl() as well: the core names a host's hue in that form.
  const parts = /^(rgba?|hsla?)\(/i.exec(text);
  const numbers = parts ? text.match(/\d+(\.\d+)?/g) : null;
  if (!numbers || numbers.length < 3) {
    return null;
  }
  const values = numbers.slice(0, 3).map(Number);
  const rgb = parts[1].toLowerCase().startsWith('hsl') ? hslToRgb(values) : values;
  return rgb.map((n) => Math.max(0, Math.min(255, Math.round(n))));
}

function hslToRgb([h, s, l]) {
  const sat = s / 100;
  const light = l / 100;
  const chroma = (1 - Math.abs(2 * light - 1)) * sat;
  const hue = (((h % 360) + 360) % 360) / 60;
  const x = chroma * (1 - Math.abs((hue % 2) - 1));
  const sextant = Math.floor(hue);
  const [r, g, b] = [
    [chroma, x, 0], [x, chroma, 0], [0, chroma, x],
    [0, x, chroma], [x, 0, chroma], [chroma, 0, x],
  ][sextant] || [0, 0, 0];
  const m = light - chroma / 2;
  return [r + m, g + m, b + m].map((v) => v * 255);
}

export function cssRgb([r, g, b]) {
  return `rgb(${r} ${g} ${b})`;
}

// --selfill's alpha in sidebar.css. The window pushes .6 instead when the ink
// under it turns black, which only happens on a light tint - where white over
// near-white moves the ground by a couple of units.
export const SEL_WASH_ALPHA = 0.3;

// --selfill's alpha under html.atrium-native-chrome in sidebar.css, where the
// wash is the surround's ink rather than white.
export const NATIVE_SEL_ALPHA = 0.22;

// The row as it is PAINTED: a selected row's wash over the ground it sits on.
// Only the focused row rings, so this is the ring's real ground - a
// red icon reads 3.01:1 on #12282E and 1.12:1 on the row that tint paints.
export function rowBackdrop(tint, alpha = SEL_WASH_ALPHA, wash = WHITE) {
  if (!tint) {
    return null;
  }
  const a = Math.max(0, Math.min(1, Number(alpha)));
  return tint.map((c, i) => Math.round(c * (1 - a) + wash[i] * a));
}

// How finely the ramp below is walked. Each step is 1.6% of the distance to
// white or black, which is close enough that the answer keeps nearly all of
// the icon's own colour.
export const RAMP_STEPS = 64;

// The favicon colour moved just far enough toward white (on a dark row) or
// black (on a light row) to be seen. Every channel-to-channel difference
// shrinks by the same (1 - t), which keeps the hue and cannot clip.
export function ringColorOn(rgb, backdrop,
                            {min = RING_MIN_CONTRAST,
                             steps = RAMP_STEPS} = {}) {
  if (!rgb || !backdrop) {
    return rgb;
  }
  // Always reachable: a row too light for white ink is light enough that black
  // clears the bar on it, and a row dark enough for white ink clears it too.
  const toward =
      contrastRatio(WHITE, backdrop) >= READABLE_CONTRAST ? WHITE : BLACK;
  let out = rgb;
  for (let step = 0; step <= steps; step++) {
    const t = step / steps;
    out = rgb.map((c, i) => Math.round(c + (toward[i] - c) * t));
    if (contrastRatio(out, backdrop) >= min) {
      break;
    }
  }
  return out;
}

// Hue buckets this wide read as one colour: a red that straddles a bucket edge
// is one run of buckets, not two entries.
export const PALETTE_HUE_STEP = 30;
// A hue below this share of the coloured pixels is an edge, not a colour.
export const PALETTE_MIN_SHARE = 0.08;
export const PALETTE_MAX = 4;

function hueOf(r, g, b) {
  const max = Math.max(r, g, b);
  const min = Math.min(r, g, b);
  const d = max - min;
  if (d === 0) {
    return 0;
  }
  let h;
  if (max === r) {
    h = ((g - b) / d) % 6;
  } else if (max === g) {
    h = (b - r) / d + 2;
  } else {
    h = (r - g) / d + 4;
  }
  return ((h * 60) + 360) % 360;
}

// [{rgb, angle}] by angle clockwise from the top, or null under two colours,
// where the plain ring is the right answer.
export function iconPalette(rgba, width, {minShare = PALETTE_MIN_SHARE,
                                          max = PALETTE_MAX} = {}) {
  if (!(width > 0)) {
    return null;
  }
  const count = 360 / PALETTE_HUE_STEP;
  const buckets = Array.from({length: count}, () => null);
  let total = 0;
  for (let i = 0, p = 0; i + 3 < rgba.length; i += 4, p++) {
    if (rgba[i + 3] < 128) {
      continue;
    }
    const cr = rgba[i], cg = rgba[i + 1], cb = rgba[i + 2];
    const w = Math.max(cr, cg, cb) - Math.min(cr, cg, cb);
    if (w < MIN_ICON_CHROMA) {
      continue;
    }
    const key = Math.floor(hueOf(cr, cg, cb) / PALETTE_HUE_STEP) % count;
    const b = buckets[key] || (buckets[key] = {r: 0, g: 0, b: 0, x: 0, y: 0, n: 0});
    b.r += cr * w;
    b.g += cg * w;
    b.b += cb * w;
    b.x += (p % width) * w;
    b.y += Math.floor(p / width) * w;
    b.n += w;
    total += w;
  }
  if (total === 0) {
    return null;
  }
  // Runs start at an empty bucket so a hue that wraps past red stays one run.
  let start = 0;
  while (start < count && buckets[start] && buckets[(start + count - 1) % count]) {
    start++;
  }
  if (start === count) {
    return null;
  }
  // Neighbouring buckets join one run only when their mean hues are closer
  // than a bucket: a red next to a yellow stays two colours.
  const meanHue = (b) => hueOf(b.r / b.n, b.g / b.n, b.b / b.n);
  const hueGap = (a, b) => {
    const d = Math.abs(meanHue(a) - meanHue(b)) % 360;
    return Math.min(d, 360 - d);
  };
  const runs = [];
  let run = null;
  let prev = null;
  for (let k = 0; k < count; k++) {
    const b = buckets[(start + k) % count];
    if (!b) {
      run = null;
      prev = null;
      continue;
    }
    if (!run || hueGap(prev, b) >= PALETTE_HUE_STEP) {
      run = {r: 0, g: 0, b: 0, x: 0, y: 0, n: 0};
      runs.push(run);
    }
    for (const c of ['r', 'g', 'b', 'x', 'y', 'n']) {
      run[c] += b[c];
    }
    prev = b;
  }
  const height = rgba.length / 4 / width;
  const out = runs.filter((r) => r.n >= total * minShare)
                  .sort((a, b) => b.n - a.n)
                  .slice(0, max)
                  .map((r) => {
                    const dx = r.x / r.n - (width - 1) / 2;
                    const dy = r.y / r.n - (height - 1) / 2;
                    const angle = ((Math.atan2(dx, -dy) * 180 / Math.PI) + 360) % 360;
                    return {rgb: [r.r, r.g, r.b].map((v) => Math.round(v / r.n)),
                            angle: Math.round(angle)};
                  })
                  .sort((a, b) => a.angle - b.angle);
  return out.length >= 2 ? out : null;
}

// The wheel is closed with the last and first stops repeated, so the ring
// has no seam at the top.
export function paletteGradient(palette, ground = null) {
  const stops = palette.map(({rgb, angle}) => ({angle, css: cssRgb(ringColorOn(rgb, ground))}));
  const first = stops[0];
  const last = stops[stops.length - 1];
  const all = [{angle: last.angle - 360, css: last.css}, ...stops,
               {angle: first.angle + 360, css: first.css}];
  return 'conic-gradient(from 0deg, ' +
      all.map((s) => `${s.css} ${s.angle}deg`).join(', ') + ')';
}

// The favicon routes are same-origin with the sidebar, so the canvas stays
// readable, and a shim or a tainted canvas throws - which reads as no icon.
function iconPixels(img, doc) {
  try {
    const size = 16;
    const canvas = doc.createElement('canvas');
    canvas.width = size;
    canvas.height = size;
    const ctx = canvas.getContext('2d');
    if (!ctx) {
      return null;
    }
    ctx.drawImage(img, 0, 0, size, size);
    return {data: ctx.getImageData(0, 0, size, size).data, size};
  } catch (e) {
    return null;
  }
}

// ponytail: flat cap, raise it if a session with more distinct icons than
// this starts missing the cache.
const RING_LOOK_CACHE_MAX = 256;
const ringLookCache = new Map();

// Memoised by icon source and backdrop: buildRail re-renders every space
// pane on each push, and the bitmap behind a given src has not changed.
export function ringLookFor(img, {doc = document, backdrop = null} = {}) {
  const ground = Array.isArray(backdrop) ? backdrop : parseRgb(backdrop);
  const key = `${img && img.src || ''}|${ground}`;
  const cached = ringLookCache.get(key);
  if (cached) {
    return cached;
  }
  const read = iconPixels(img, doc);
  const rgb = read ? dominantColor(read.data) : null;
  const palette = read ? iconPalette(read.data, read.size) : null;
  const look = {color: rgb ? cssRgb(ringColorOn(rgb, ground)) : null,
                gradient: palette ? paletteGradient(palette, ground) : null};
  if (ringLookCache.size >= RING_LOOK_CACHE_MAX) {
    ringLookCache.delete(ringLookCache.keys().next().value);
  }
  ringLookCache.set(key, look);
  return look;
}
