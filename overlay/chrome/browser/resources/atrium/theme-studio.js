// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

// Theme Studio: the settings-pane editor for a space's ThemeSpec.
import {mountColorPicker} from './theme-picker.js';
import {clamp} from './clamp.js';

const RASTER_WIDTH = 192;
const RASTER_HEIGHT = 108;
const FALLBACK_HEX = '#12282E';
// theme_math.rs::MAX_NAME_CHARS - the store enforces the same cap.
const MAX_NAME_CHARS = 64;
const STOP_PALETTE = ['#d98e6b', '#7fa8a0', '#8e7cc3', '#c9b458', '#6b8fd9'];
const ADD_STOP_SLOTS = [[0.25, 0.25], [0.75, 0.30], [0.30, 0.75], [0.78, 0.75], [0.50, 0.50]];
const PRESET_DOT_POSITIONS = [[0.18, 0.12], [0.82, 0.85], [0.5, 0.5]];

function hexToRgb(hex) {
  const trimmed = (hex || '').replace('#', '');
  if (trimmed.length !== 6 || !/^[0-9a-fA-F]{6}$/.test(trimmed)) {
    return [0, 0, 0];
  }
  const n = parseInt(trimmed, 16);
  return [((n >> 16) & 255) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255];
}

function byteValue(v) {
  return Math.round(clamp(v, 0, 1) * 255);
}

function rgbToHex([r, g, b]) {
  return '#' + [r, g, b].map((v) => byteValue(v).toString(16).padStart(2, '0'))
      .join('').toUpperCase();
}

function rgbToHsl([r, g, b]) {
  const max = Math.max(r, g, b), min = Math.min(r, g, b);
  const l = (max + min) / 2;
  if (max === min) {
    return [0, 0, l];
  }
  const d = max - min;
  const s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
  let h;
  if (max === r) {
    h = (g - b) / d + (g < b ? 6 : 0);
  } else if (max === g) {
    h = (b - r) / d + 2;
  } else {
    h = (r - g) / d + 4;
  }
  return [h / 6, s, l];
}

function hue2rgb(p, q, t) {
  if (t < 0) t += 1;
  if (t > 1) t -= 1;
  if (t < 1 / 6) return p + (q - p) * 6 * t;
  if (t < 1 / 2) return q;
  if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6;
  return p;
}

function hslToRgb(h, s, l) {
  if (s === 0) return [l, l, l];
  const q = l < 0.5 ? l * (1 + s) : l + s - l * s;
  const p = 2 * l - q;
  return [hue2rgb(p, q, h + 1 / 3), hue2rgb(p, q, h), hue2rgb(p, q, h - 1 / 3)];
}

// One source gradient, never two: dims and desaturates in HSL space so the
// dark variant reads as the same theme (theme_math.rs's adapt_dark).
function adaptDark(rgb, k) {
  const [h, s, l] = rgbToHsl(rgb);
  return hslToRgb(h, s * (1 - 0.25 * k), l * (1 - 0.7 * k));
}

// Render-time-only stop positions: orbits the composition centroid so stops
// slowly trade places (theme_math.rs's ambient_positions). Never mutates the
// authored stops.
function ambientPositions(stops, t) {
  if (!stops.length) return [];
  const n = stops.length;
  const cx = stops.reduce((sum, s) => sum + s.x, 0) / n;
  const cy = stops.reduce((sum, s) => sum + s.y, 0) / n;
  const th = t * 0.042;
  const wob = n === 1 ? 0.12 : 0.03;
  return stops.map((stop, i) => {
    const a = th + Math.sin(0.05 * t + 1.9 * i) * 0.15;
    const rr = 1 + 0.25 * Math.sin(0.037 * t + 2.4 * i);
    const dx = stop.x - cx, dy = stop.y - cy;
    const rx = (dx * Math.cos(a) - dy * Math.sin(a)) * rr;
    const ry = (dx * Math.sin(a) + dy * Math.cos(a)) * rr;
    const x = cx + rx + Math.sin(0.06 * t + 2.7 * i) * wob;
    const y = cy + ry + Math.cos(0.05 * t + 3.3 * i) * wob;
    return [clamp(x, 0.04, 0.96), clamp(y, 0.04, 0.96)];
  });
}

function boxBlurPass(buffer, width, height, radius, horizontal) {
  const out = buffer.slice();
  const length = horizontal ? width : height;
  const lines = horizontal ? height : width;
  for (let line = 0; line < lines; line++) {
    for (let pos = 0; pos < length; pos++) {
      let sr = 0, sg = 0, sb = 0, sa = 0, count = 0;
      for (let offset = -radius; offset <= radius; offset++) {
        const p = clamp(pos + offset, 0, length - 1);
        const [x, y] = horizontal ? [p, line] : [line, p];
        const idx = (y * width + x) * 4;
        sr += buffer[idx]; sg += buffer[idx + 1];
        sb += buffer[idx + 2]; sa += buffer[idx + 3];
        count++;
      }
      const [x, y] = horizontal ? [pos, line] : [line, pos];
      const idx = (y * width + x) * 4;
      out[idx] = Math.round(sr / count);
      out[idx + 1] = Math.round(sg / count);
      out[idx + 2] = Math.round(sb / count);
      out[idx + 3] = Math.round(sa / count);
    }
  }
  return out;
}

// Three sequential horizontal+vertical box-blur passes approximating a
// Gaussian frost, cheap enough for a raster this small (theme_math.rs).
function boxBlur(buffer, width, height, radius) {
  let current = buffer;
  for (let i = 0; i < 3; i++) {
    current = boxBlurPass(current, width, height, radius, true);
    current = boxBlurPass(current, width, height, radius, false);
  }
  return current;
}

// Radial falloff centered above-and-left of the raster's true center.
// Darkens rgb toward black only, alpha untouched (theme_math.rs).
function applyVignette(buffer, width, height, strength) {
  for (let y = 0; y < height; y++) {
    const ny = height > 1 ? y / (height - 1) : 0;
    for (let x = 0; x < width; x++) {
      const nx = width > 1 ? x / (width - 1) : 0;
      const dist = Math.hypot(nx - 0.5, ny - 0.42);
      const v = clamp((dist - 0.52) / 0.6, 0, 1);
      const factor = clamp(strength * v, 0, 1);
      if (factor <= 0) continue;
      const idx = (y * width + x) * 4;
      buffer[idx] = Math.round(buffer[idx] * (1 - factor));
      buffer[idx + 1] = Math.round(buffer[idx + 1] * (1 - factor));
      buffer[idx + 2] = Math.round(buffer[idx + 2] * (1 - factor));
    }
  }
}

// Inverse-distance-weighted gradient mesh, ported verbatim from
// theme_math.rs::rasterize. `ambientTime < 0` keeps the authored stop
// positions. `ambientTime >= 0` orbits them.
export function rasterizeMesh(spec, width, height, {dark = false, warmth = 0, ambientTime = -1} = {}) {
  const stops = spec.stops || [];
  const effects = spec.effects || {};
  const opacity = spec.opacity ?? 1;
  const wf = clamp((effects.warmth || 0) + warmth, -1, 1) * 0.12;
  const colors = stops.map((stop) => {
    let rgb = hexToRgb(stop.color);
    if (dark) rgb = adaptDark(rgb, spec.darkAdaptation ?? 0);
    return [clamp(rgb[0] * (1 + wf), 0, 1), rgb[1], clamp(rgb[2] * (1 - wf), 0, 1)];
  });
  const alphas = stops.map((stop) => stop.alpha * opacity);
  const positions = ambientTime >= 0 ?
      ambientPositions(stops, ambientTime) :
      stops.map((stop) => [stop.x, stop.y]);

  const aspect = width / height;
  const buffer = new Uint8ClampedArray(width * height * 4);

  for (let y = 0; y < height; y++) {
    const ny = height > 1 ? y / (height - 1) : 0;
    for (let x = 0; x < width; x++) {
      const nx = width > 1 ? x / (width - 1) : 0;
      let sumR = 0, sumG = 0, sumB = 0, sa = 0, sw = 0;
      for (let i = 0; i < stops.length; i++) {
        const dx = (nx - positions[i][0]) * aspect;
        const dy = ny - positions[i][1];
        const d = Math.hypot(dx, dy);
        const weight = 1 / (Math.pow(d, 2.4) + 1e-4);
        const a = alphas[i];
        sumR += weight * a * colors[i][0];
        sumG += weight * a * colors[i][1];
        sumB += weight * a * colors[i][2];
        sa += weight * a;
        sw += weight;
      }
      const idx = (y * width + x) * 4;
      buffer[idx] = byteValue(sa > 0 ? sumR / sa : 0);
      buffer[idx + 1] = byteValue(sa > 0 ? sumG / sa : 0);
      buffer[idx + 2] = byteValue(sa > 0 ? sumB / sa : 0);
      buffer[idx + 3] = byteValue(sw > 0 ? sa / sw : 0);
    }
  }

  const radius = Math.floor((effects.frost || 0) / 4);
  let out = buffer;
  if (radius > 0) {
    out = boxBlur(out, width, height, radius);
  }
  if ((effects.vignette || 0) > 0) {
    applyVignette(out, width, height, effects.vignette);
  }
  return out;
}

function normalizedHex(raw) {
  const trimmed = (raw || '').trim();
  return /^#[0-9a-fA-F]{6}$/.test(trimmed) ? trimmed.toUpperCase() : FALLBACK_HEX;
}

// Code points, not UTF-16 units - matches theme_math.rs::sanitized_name, and
// slicing a surrogate pair in half would leave a lone half behind.
function sanitizedName(raw) {
  return [...String(raw == null ? '' : raw).trim()].slice(0, MAX_NAME_CHARS).join('');
}

function sanitizeStop(stop) {
  return {
    x: clamp(stop.x, 0, 1),
    y: clamp(stop.y, 0, 1),
    color: normalizedHex(stop.color),
    alpha: clamp(stop.alpha, 0, 1),
  };
}

// Clamps every numeric field to its documented range, trims and caps the
// name, drops stops beyond 5, and repairs an empty stop list or a malformed
// hex - mirrors theme_math.rs::sanitized exactly. Idempotent.
function sanitizeSpec(spec) {
  const source = (spec && spec.stops && spec.stops.length) ?
      spec.stops :
      [{x: 0.5, y: 0.45, color: FALLBACK_HEX, alpha: 0.9}];
  const effects = (spec && spec.effects) || {};
  return {
    name: sanitizedName(spec && spec.name),
    stops: source.slice(0, 5).map(sanitizeStop),
    opacity: clamp(spec && spec.opacity != null ? spec.opacity : 1, 0, 1),
    effects: {
      grain: clamp(effects.grain || 0, 0, 1),
      vignette: clamp(effects.vignette || 0, 0, 1),
      frost: clamp(effects.frost || 0, 0, 30),
      warmth: clamp(effects.warmth || 0, -1, 1),
    },
    darkAdaptation: clamp(spec && spec.darkAdaptation != null ? spec.darkAdaptation : 0.45, 0, 1),
    nightShiftEnabled: !!(spec && spec.nightShiftEnabled),
    ambientEnabled: !!(spec && spec.ambientEnabled),
    version: spec && spec.version != null ? spec.version : 1,
  };
}

// The 12 built-in themes, ported verbatim from ThemeSpec.swift's `presets`
// (itself ported from the prototype's PRESETS, grain/vignette rescaled from
// 0-100 to 0-1 and warmth from -50...50 to -1...1; frost stays in px).
export const THEME_PRESETS = [
  {name: 'Atrium', stops: [
    {x: 0.24, y: 0.30, color: '#268BD2', alpha: 0.90},
    {x: 0.78, y: 0.68, color: '#CB4B16', alpha: 0.45},
    {x: 0.60, y: 0.10, color: '#123842', alpha: 1.00},
  ], opacity: 1, effects: {grain: 0.35, vignette: 0.22, frost: 0, warmth: 0}, darkAdaptation: 0.45},
  {name: 'Nocturne', stops: [
    {x: 0.30, y: 0.22, color: '#1B1F3A', alpha: 1.00},
    {x: 0.76, y: 0.55, color: '#7A3B5E', alpha: 0.90},
    {x: 0.14, y: 0.86, color: '#0C0E16', alpha: 1.00},
  ], opacity: 1, effects: {grain: 0.55, vignette: 0.34, frost: 0, warmth: -0.16}, darkAdaptation: 0.45},
  {name: 'Glacier', stops: [
    {x: 0.50, y: 0.42, color: '#AFC8D2', alpha: 0.90},
  ], opacity: 1, effects: {grain: 0.20, vignette: 0.12, frost: 0, warmth: -0.28}, darkAdaptation: 0.45},
  {name: 'Ember', stops: [
    {x: 0.70, y: 0.30, color: '#C2452D', alpha: 0.95},
    {x: 0.20, y: 0.78, color: '#2A1214', alpha: 1.00},
  ], opacity: 1, effects: {grain: 0.60, vignette: 0.30, frost: 0, warmth: 0.36}, darkAdaptation: 0.45},
  {name: 'Meadow', stops: [
    {x: 0.26, y: 0.30, color: '#5E8C61', alpha: 0.90},
    {x: 0.78, y: 0.26, color: '#E9E2C8', alpha: 0.80},
    {x: 0.55, y: 0.85, color: '#29472F', alpha: 1.00},
  ], opacity: 1, effects: {grain: 0.30, vignette: 0.18, frost: 0, warmth: 0.12}, darkAdaptation: 0.45},
  {name: 'Prism', stops: [
    {x: 0.14, y: 0.24, color: '#E4A19B', alpha: 0.80},
    {x: 0.50, y: 0.12, color: '#EED6A8', alpha: 0.80},
    {x: 0.86, y: 0.30, color: '#9BC4B8', alpha: 0.80},
    {x: 0.30, y: 0.80, color: '#8FA6C8', alpha: 0.80},
    {x: 0.74, y: 0.82, color: '#C3A1C9', alpha: 0.80},
  ], opacity: 1, effects: {grain: 0.25, vignette: 0.14, frost: 6, warmth: 0}, darkAdaptation: 0.45},
  {name: 'Dawn', stops: [
    {x: 0.28, y: 0.24, color: '#F2C4AE', alpha: 0.85},
    {x: 0.76, y: 0.42, color: '#A98FBF', alpha: 0.80},
    {x: 0.52, y: 0.86, color: '#5C5470', alpha: 0.90},
  ], opacity: 1, effects: {grain: 0.25, vignette: 0.16, frost: 0, warmth: 0.20}, darkAdaptation: 0.45},
  {name: 'Abyss', stops: [
    {x: 0.42, y: 0.30, color: '#0E3A4C', alpha: 1.00},
    {x: 0.78, y: 0.68, color: '#14647A', alpha: 0.90},
    {x: 0.14, y: 0.84, color: '#071219', alpha: 1.00},
  ], opacity: 1, effects: {grain: 0.45, vignette: 0.38, frost: 0, warmth: -0.20}, darkAdaptation: 0.45},
  {name: 'Sakura', stops: [
    {x: 0.30, y: 0.28, color: '#F4D5DE', alpha: 0.85},
    {x: 0.78, y: 0.50, color: '#D98BA3', alpha: 0.80},
    {x: 0.46, y: 0.88, color: '#9C6B7F', alpha: 0.70},
  ], opacity: 1, effects: {grain: 0.20, vignette: 0.12, frost: 4, warmth: 0.16}, darkAdaptation: 0.45},
  {name: 'Copper', stops: [
    {x: 0.70, y: 0.32, color: '#B0714A', alpha: 0.95},
    {x: 0.24, y: 0.68, color: '#4E8577', alpha: 0.85},
    {x: 0.86, y: 0.86, color: '#241A16', alpha: 1.00},
  ], opacity: 1, effects: {grain: 0.50, vignette: 0.26, frost: 0, warmth: 0.24}, darkAdaptation: 0.45},
  {name: 'Ink', stops: [
    {x: 0.34, y: 0.30, color: '#2A2D34', alpha: 1.00},
    {x: 0.76, y: 0.52, color: '#6B7280', alpha: 0.70},
    {x: 0.20, y: 0.84, color: '#14161A', alpha: 1.00},
  ], opacity: 1, effects: {grain: 0.55, vignette: 0.30, frost: 0, warmth: 0}, darkAdaptation: 0.45},
  {name: 'Neon Dusk', stops: [
    {x: 0.50, y: 0.78, color: '#12081F', alpha: 1.00},
    {x: 0.24, y: 0.20, color: '#E4417B', alpha: 0.85},
    {x: 0.80, y: 0.24, color: '#2BC6C8', alpha: 0.80},
  ], opacity: 1, effects: {grain: 0.40, vignette: 0.28, frost: 0, warmth: -0.10}, darkAdaptation: 0.45},
].map((preset) => sanitizeSpec({...preset, nightShiftEnabled: false, ambientEnabled: false, version: 1}));

function hslToHex(h, s, l) {
  const hue = ((h % 360) + 360) % 360;
  return rgbToHex(hslToRgb(hue / 360, s, l));
}

// The lab popover's harmony randomizer, ported verbatim: a hue plus a scheme
// offset on a fixed lightness ladder, never raw random RGB.
export function harmonyStops() {
  const base = Math.floor(Math.random() * 360);
  const schemes = [[-30, 0, 30], [0, 180, 0], [0, 120, 240]];
  const scheme = schemes[Math.floor(Math.random() * schemes.length)];
  const ladder = [0.62, 0.45, 0.30];
  return scheme.map((offset, i) => ({
    x: PRESET_DOT_POSITIONS[i][0],
    y: PRESET_DOT_POSITIONS[i][1],
    color: hslToHex((base + offset + 360) % 360, 0.6, ladder[i]),
    alpha: i === 2 ? 0.55 : 0.8,
  }));
}

function isDarkScheme() {
  try {
    return !!(globalThis.matchMedia &&
        globalThis.matchMedia('(prefers-color-scheme: dark)').matches);
  } catch (e) {
    return false;
  }
}

// icon-color.js's precedent: a canvas draw call throws in the test shim
// (no real 2D context there), which reads as "nothing to paint" rather than
// a crash.
function paintCanvas(canvas, spec) {
  try {
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    const buf = rasterizeMesh(spec, RASTER_WIDTH, RASTER_HEIGHT, {dark: isDarkScheme()});
    const imageData = ctx.createImageData(RASTER_WIDTH, RASTER_HEIGHT);
    imageData.data.set(buf);
    ctx.putImageData(imageData, 0, 0);
  } catch (e) {
    // No 2D canvas in this environment - the surrounding DOM still renders.
  }
}

const SLIDERS = [
  {key: 'opacity', label: 'Opacity', min: 0, max: 1, step: 0.01, path: ['opacity']},
  {key: 'grain', label: 'Grain', min: 0, max: 1, step: 0.01, path: ['effects', 'grain']},
  {key: 'vignette', label: 'Vignette', min: 0, max: 1, step: 0.01, path: ['effects', 'vignette']},
  {key: 'frost', label: 'Frost', min: 0, max: 30, step: 1, path: ['effects', 'frost']},
  {key: 'warmth', label: 'Warmth', min: -1, max: 1, step: 0.05, path: ['effects', 'warmth']},
  {key: 'darkAdaptation', label: 'Dark adapt', min: 0, max: 1, step: 0.01, path: ['darkAdaptation']},
];

const TOGGLES = [
  {key: 'nightShiftEnabled', label: 'Night Shift'},
  {key: 'ambientEnabled', label: 'Ambient'},
];

function readPath(obj, path) {
  return path.reduce((v, k) => v[k], obj);
}

function writePath(obj, path, value) {
  const next = {...obj};
  if (path.length === 1) {
    next[path[0]] = value;
    return next;
  }
  next[path[0]] = {...obj[path[0]], [path[1]]: value};
  return next;
}

// Renders the studio into `container` and returns {destroy(), spec()}.
// onPreview(spec) fires on every draft change, onCommit(spec) on Save,
// onRemove() on Remove theme.
export function mountThemeStudio(container, opts) {
  const {
    spec: initialSpec,
    spaceName = '',
    accent = null,
    presets = THEME_PRESETS,
    onPreview = () => {},
    onCommit = () => {},
    onRemove = () => {},
  } = opts || {};

  let draft = sanitizeSpec(initialSpec);
  let selected = 0;
  const cleanups = [];
  let pendingFrame = 0;
  let pendingPreviewFrame = 0;

  const root = document.createElement('div');
  root.className = 'theme-studio';
  if (accent) {
    root.style.setProperty('--theme-studio-accent', normalizedHex(accent));
  }
  container.appendChild(root);

  const picker = mountColorPicker({
    onChange: ({hex, alpha}) => setStop(selected, hex, alpha),
  });
  cleanups.push(() => picker.destroy());

  // --- header ---------------------------------------------------------

  const header = document.createElement('div');
  header.className = 'theme-studio-header';
  const title = document.createElement('span');
  title.className = 'theme-studio-title';
  title.textContent = 'Theme';
  const name = document.createElement('span');
  name.className = 'theme-studio-space-name';
  name.textContent = spaceName;
  header.appendChild(title);
  header.appendChild(name);
  root.appendChild(header);

  // --- mini canvas + stop dots -----------------------------------------

  const canvasWrap = document.createElement('div');
  canvasWrap.className = 'theme-studio-canvas-wrap';
  const canvas = document.createElement('canvas');
  canvas.className = 'theme-studio-canvas';
  canvas.width = RASTER_WIDTH;
  canvas.height = RASTER_HEIGHT;
  canvasWrap.appendChild(canvas);
  const dotsLayer = document.createElement('div');
  dotsLayer.className = 'theme-studio-dots';
  canvasWrap.appendChild(dotsLayer);
  root.appendChild(canvasWrap);

  const hint = document.createElement('p');
  hint.className = 'theme-studio-hint';
  hint.textContent = 'Drag a dot to move it, double-click to add one, right-click to remove it.';
  root.appendChild(hint);

  canvasWrap.addEventListener('dblclick', (event) => {
    const onDot = typeof event.target?.closest === 'function' &&
        event.target.closest('.theme-studio-dot');
    if (onDot || draft.stops.length >= 5) {
      return;
    }
    const rect = canvasWrap.getBoundingClientRect();
    const x = rect.width ? clamp((event.clientX - rect.left) / rect.width, 0, 1) : 0.5;
    const y = rect.height ? clamp((event.clientY - rect.top) / rect.height, 0, 1) : 0.5;
    addStop(x, y);
  });

  // --- presets row ------------------------------------------------------

  const presetsRow = document.createElement('div');
  presetsRow.className = 'theme-studio-row';
  const presetsLabel = document.createElement('span');
  presetsLabel.className = 'settings-row-label';
  presetsLabel.textContent = 'Presets';
  const presetList = document.createElement('div');
  presetList.className = 'theme-studio-preset-list';
  presets.forEach((preset, index) => {
    const dot = document.createElement('button');
    dot.className = 'theme-studio-preset';
    dot.dataset.presetIndex = String(index);
    dot.title = preset.name;
    const stopColors = preset.stops.map((s) => normalizedHex(s.color));
    dot.style.background = stopColors.length > 1 ?
        `linear-gradient(135deg, ${stopColors.join(', ')})` :
        stopColors[0];
    dot.addEventListener('click', () => loadPreset(preset));
    presetList.appendChild(dot);
  });
  const randomizeBtn = document.createElement('button');
  randomizeBtn.className = 'theme-studio-randomize';
  randomizeBtn.title = 'Randomize (harmony)';
  randomizeBtn.textContent = '⟳';
  randomizeBtn.addEventListener('click', randomize);
  presetList.appendChild(randomizeBtn);
  presetsRow.appendChild(presetsLabel);
  presetsRow.appendChild(presetList);
  root.appendChild(presetsRow);

  // --- stop swatches ------------------------------------------------------

  const swatchRow = document.createElement('div');
  swatchRow.className = 'theme-studio-row';
  const swatchLabel = document.createElement('span');
  swatchLabel.className = 'settings-row-label';
  swatchLabel.textContent = 'Stops';
  const swatchList = document.createElement('div');
  swatchList.className = 'theme-studio-swatch-list';
  swatchRow.appendChild(swatchLabel);
  swatchRow.appendChild(swatchList);
  root.appendChild(swatchRow);

  // --- sliders ------------------------------------------------------------

  const sliderInputs = {};
  for (const def of SLIDERS) {
    const row = document.createElement('div');
    row.className = 'settings-row';
    const label = document.createElement('span');
    label.className = 'settings-row-label';
    label.textContent = def.label;
    const input = document.createElement('input');
    input.type = 'range';
    input.className = 'theme-studio-slider';
    input.dataset.key = def.key;
    input.min = String(def.min);
    input.max = String(def.max);
    input.step = String(def.step);
    input.addEventListener('input', () => {
      draft = writePath(draft, def.path, Number(input.value));
      preview();
    });
    sliderInputs[def.key] = input;
    row.appendChild(label);
    row.appendChild(input);
    root.appendChild(row);
  }

  // --- toggles --------------------------------------------------------

  const toggleButtons = {};
  for (const def of TOGGLES) {
    const row = document.createElement('div');
    row.className = 'settings-row';
    const label = document.createElement('span');
    label.className = 'settings-row-label';
    label.textContent = def.label;
    const button = document.createElement('button');
    button.className = 'theme-studio-toggle';
    button.dataset.toggle = def.key;
    button.addEventListener('click', () => {
      draft = {...draft, [def.key]: !draft[def.key]};
      preview();
    });
    toggleButtons[def.key] = button;
    row.appendChild(label);
    row.appendChild(button);
    root.appendChild(row);
  }

  // --- actions --------------------------------------------------------

  const actions = document.createElement('div');
  actions.className = 'theme-studio-actions';
  const removeBtn = document.createElement('button');
  removeBtn.className = 'theme-studio-remove';
  removeBtn.textContent = 'Remove Theme';
  removeBtn.addEventListener('click', () => onRemove());
  const saveBtn = document.createElement('button');
  saveBtn.className = 'theme-studio-save';
  saveBtn.textContent = 'Save';
  saveBtn.addEventListener('click', () => onCommit(sanitizeSpec(draft)));
  actions.appendChild(removeBtn);
  actions.appendChild(saveBtn);
  root.appendChild(actions);

  // --- mutations --------------------------------------------------------

  // Coalesced like renderCanvas: each send costs a chrome.send, a 192x108
  // raster and a window repaint, and a drag produces one per pointer event.
  function sendPreview() {
    if (pendingPreviewFrame) return;
    pendingPreviewFrame = requestAnimationFrame(() => {
      pendingPreviewFrame = 0;
      onPreview(structuredClone(draft));
    });
  }

  function preview() {
    draft = sanitizeSpec(draft);
    sendPreview();
    render();
  }

  // Toggles classes on the existing dot/swatch elements rather than calling
  // render() - a pointerdown that both selects and starts a drag in the same
  // gesture must not replace the dot the pointer is already captured on.
  function selectStop(i) {
    if (i < 0 || i >= draft.stops.length) return;
    selected = i;
    dotsLayer.querySelectorAll('.theme-studio-dot').forEach((dot, idx) => {
      dot.classList.toggle('selected', idx === selected);
    });
    swatchList.querySelectorAll('.theme-studio-swatch').forEach((swatch, idx) => {
      swatch.classList.toggle('selected', idx === selected);
    });
  }

  // A drag holds pointer capture on its own dot element - rebuilding the
  // dots layer mid-drag (as preview()/render() does) would detach it and
  // silently end the gesture, so this moves that element directly instead.
  function moveStop(i, x, y, dotEl) {
    if (i < 0 || i >= draft.stops.length) return;
    const stops = draft.stops.map((stop, idx) => idx === i ?
        {...stop, x: clamp(x, 0.02, 0.98), y: clamp(y, 0.02, 0.98)} :
        stop);
    draft = sanitizeSpec({...draft, stops});
    sendPreview();
    if (dotEl) {
      dotEl.style.left = `${draft.stops[i].x * 100}%`;
      dotEl.style.top = `${draft.stops[i].y * 100}%`;
    }
    renderCanvas();
  }

  function addStop(x, y) {
    if (draft.stops.length >= 5) return false;
    const index = draft.stops.length;
    const slot = ADD_STOP_SLOTS[index % ADD_STOP_SLOTS.length];
    const stop = {
      x: x != null ? x : slot[0],
      y: y != null ? y : slot[1],
      color: STOP_PALETTE[index % STOP_PALETTE.length],
      alpha: 0.85,
    };
    draft = {...draft, stops: [...draft.stops, stop]};
    selected = index;
    preview();
    return true;
  }

  function removeStop(i) {
    if (draft.stops.length <= 1 || i < 0 || i >= draft.stops.length) return false;
    picker.close();
    const stops = draft.stops.filter((_, idx) => idx !== i);
    selected = clamp(selected, 0, stops.length - 1);
    draft = {...draft, stops};
    preview();
    return true;
  }

  function setStop(i, hex, alpha) {
    if (i < 0 || i >= draft.stops.length) return;
    const stops = draft.stops.map((stop, idx) => idx === i ?
        {...stop, color: hex, alpha} :
        stop);
    draft = {...draft, stops};
    preview();
  }

  // Night shift/ambient are the space's opt-in, not preset identity, so they
  // survive the swap. Closes the picker first - its selected stop index
  // would otherwise point at a stop this just replaced.
  function loadPreset(preset) {
    picker.close();
    draft = {
      ...draft,
      name: preset.name,
      stops: preset.stops.map((s) => ({...s})),
      opacity: preset.opacity,
      effects: {...preset.effects},
      darkAdaptation: preset.darkAdaptation,
    };
    selected = 0;
    preview();
  }

  function randomize() {
    picker.close();
    draft = {...draft, name: 'Custom', stops: harmonyStops()};
    selected = 0;
    preview();
  }

  // --- rendering --------------------------------------------------------

  function renderCanvas() {
    if (pendingFrame) return;
    pendingFrame = requestAnimationFrame(() => {
      pendingFrame = 0;
      paintCanvas(canvas, draft);
    });
  }

  function renderDots() {
    dotsLayer.replaceChildren();
    draft.stops.forEach((stop, i) => {
      const dot = document.createElement('span');
      dot.className = 'theme-studio-dot';
      dot.classList.toggle('selected', i === selected);
      dot.dataset.stopIndex = String(i);
      dot.style.left = `${stop.x * 100}%`;
      dot.style.top = `${stop.y * 100}%`;
      dot.style.background = stop.color;
      wireDot(dot, i);
      dotsLayer.appendChild(dot);
    });
  }

  function wireDot(dot, i) {
    dot.addEventListener('contextmenu', (event) => {
      event.preventDefault();
      event.stopPropagation();
      removeStop(i);
    });
    dot.addEventListener('pointerdown', (event) => {
      event.preventDefault();
      event.stopPropagation();
      if (selected !== i) {
        selectStop(i);
      }
      dot.setPointerCapture(event.pointerId);
      const rect = canvasWrap.getBoundingClientRect();
      const move = (ev) => {
        if (!rect.width || !rect.height) return;
        moveStop(i, (ev.clientX - rect.left) / rect.width, (ev.clientY - rect.top) / rect.height, dot);
      };
      const up = () => {
        dot.removeEventListener('pointermove', move);
        dot.removeEventListener('pointerup', up);
        dot.removeEventListener('pointercancel', up);
        render();
      };
      dot.addEventListener('pointermove', move);
      dot.addEventListener('pointerup', up);
      dot.addEventListener('pointercancel', up);
    });
  }

  function renderSwatches() {
    swatchList.replaceChildren();
    draft.stops.forEach((stop, i) => {
      const swatch = document.createElement('button');
      swatch.className = 'theme-studio-swatch';
      swatch.classList.toggle('selected', i === selected);
      swatch.dataset.stopIndex = String(i);
      swatch.style.background = stop.color;
      swatch.style.opacity = String(stop.alpha);
      swatch.addEventListener('click', () => {
        selectStop(i);
        picker.open(swatch, stop.color, stop.alpha);
      });
      swatchList.appendChild(swatch);
    });
    const addBtn = document.createElement('button');
    addBtn.className = 'theme-studio-add-stop';
    addBtn.title = 'Add stop';
    addBtn.textContent = '+';
    addBtn.disabled = draft.stops.length >= 5;
    addBtn.addEventListener('click', () => addStop());
    swatchList.appendChild(addBtn);
    if (draft.stops.length > 1) {
      const removeBtnLocal = document.createElement('button');
      removeBtnLocal.className = 'theme-studio-remove-stop';
      removeBtnLocal.title = 'Remove selected stop';
      removeBtnLocal.textContent = '−';
      removeBtnLocal.addEventListener('click', () => removeStop(selected));
      swatchList.appendChild(removeBtnLocal);
    }
  }

  function renderPresetActive() {
    presetList.querySelectorAll('.theme-studio-preset').forEach((dot, index) => {
      dot.classList.toggle('active', presets[index].name === draft.name);
    });
  }

  function renderSliders() {
    for (const def of SLIDERS) {
      sliderInputs[def.key].value = String(readPath(draft, def.path));
    }
  }

  function renderToggles() {
    for (const def of TOGGLES) {
      toggleButtons[def.key].setAttribute('aria-pressed', String(!!draft[def.key]));
    }
  }

  function render() {
    renderCanvas();
    renderDots();
    renderSwatches();
    renderPresetActive();
    renderSliders();
    renderToggles();
  }

  render();

  cleanups.push(() => {
    if (pendingFrame) cancelAnimationFrame(pendingFrame);
    // A send after the host tore the studio down would repaint the window
    // with a draft nothing can save any more.
    if (pendingPreviewFrame) cancelAnimationFrame(pendingPreviewFrame);
  });

  return {
    destroy() {
      for (const cleanup of cleanups) cleanup();
      root.remove();
    },
    spec() {
      return structuredClone(draft);
    },
  };
}
