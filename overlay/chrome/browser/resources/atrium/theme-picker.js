// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

// Theme Studio: the prototype's hue-ring/SV-square color picker.
import {clamp} from './clamp.js';

const SIZE = 190;
const CENTER = SIZE / 2;
const RING_RADIUS = 81;
const RING_WIDTH = 18;
const SV_HALF = 48;

function hexToRgb(hex) {
  let h = (hex || '').replace('#', '');
  if (h.length === 3) h = [...h].map((c) => c + c).join('');
  const n = parseInt(h, 16) || 0;
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
}

function rgbToHex(r, g, b) {
  return '#' + [r, g, b].map((v) => clamp(Math.round(v), 0, 255).toString(16).padStart(2, '0')).join('');
}

function rgbToHsv(r, g, b) {
  r /= 255; g /= 255; b /= 255;
  const max = Math.max(r, g, b), min = Math.min(r, g, b), d = max - min;
  let h = 0;
  if (d) {
    if (max === r) h = ((g - b) / d) % 6;
    else if (max === g) h = (b - r) / d + 2;
    else h = (r - g) / d + 4;
    h *= 60;
    if (h < 0) h += 360;
  }
  return [h, max ? d / max : 0, max];
}

function hsvToHex(h, s, v) {
  const c = v * s;
  const x = c * (1 - Math.abs(((h / 60) % 2) - 1));
  const m = v - c;
  const rgb = h < 60 ? [c, x, 0] : h < 120 ? [x, c, 0] : h < 180 ? [0, c, x] :
      h < 240 ? [0, x, c] : h < 300 ? [x, 0, c] : [c, 0, x];
  return rgbToHex(...rgb.map((t) => (t + m) * 255));
}

function alphaToHex(a) {
  return clamp(Math.round(a * 255), 0, 255).toString(16).padStart(2, '0');
}

// icon-color.js's precedent: canvas 2D calls throw where there is no real
// context (the WebUI test shim), which reads as "nothing drawn" here.
function drawPicker(canvas, ringCache, pk) {
  try {
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    ctx.setTransform(2, 0, 0, 2, 0, 0);
    ctx.clearRect(0, 0, SIZE, SIZE);
    ctx.drawImage(ringCache, 0, 0, SIZE, SIZE);

    const x0 = CENTER - SV_HALF, y0 = CENTER - SV_HALF, sz = SV_HALF * 2;
    ctx.fillStyle = `hsl(${pk.h}, 100%, 50%)`;
    ctx.fillRect(x0, y0, sz, sz);
    let g = ctx.createLinearGradient(x0, 0, x0 + sz, 0);
    g.addColorStop(0, '#fff');
    g.addColorStop(1, 'rgba(255,255,255,0)');
    ctx.fillStyle = g;
    ctx.fillRect(x0, y0, sz, sz);
    g = ctx.createLinearGradient(0, y0, 0, y0 + sz);
    g.addColorStop(0, 'rgba(0,0,0,0)');
    g.addColorStop(1, '#000');
    ctx.fillStyle = g;
    ctx.fillRect(x0, y0, sz, sz);
    ctx.strokeStyle = 'rgba(255,255,255,0.25)';
    ctx.strokeRect(x0 + 0.5, y0 + 0.5, sz - 1, sz - 1);

    const ha = (pk.h * Math.PI) / 180;
    ctx.beginPath();
    ctx.arc(CENTER + Math.cos(ha) * RING_RADIUS, CENTER + Math.sin(ha) * RING_RADIUS, 7, 0, Math.PI * 2);
    ctx.fillStyle = `hsl(${pk.h}, 100%, 50%)`;
    ctx.fill();
    ctx.lineWidth = 2.5;
    ctx.strokeStyle = '#fff';
    ctx.stroke();

    ctx.beginPath();
    ctx.arc(x0 + pk.s * sz, y0 + (1 - pk.v) * sz, 6, 0, Math.PI * 2);
    ctx.fillStyle = hsvToHex(pk.h, pk.s, pk.v);
    ctx.fill();
    ctx.lineWidth = 2;
    ctx.strokeStyle = pk.v > 0.6 && pk.s < 0.5 ? 'rgba(0,0,0,0.6)' : '#fff';
    ctx.stroke();
  } catch (e) {
    // No 2D canvas in this environment.
  }
}

function buildRingCache() {
  const cache = document.createElement('canvas');
  cache.width = cache.height = SIZE * 2;
  try {
    const ctx = cache.getContext('2d');
    if (!ctx) return cache;
    ctx.setTransform(2, 0, 0, 2, 0, 0);
    for (let a = 0; a < 360; a++) {
      ctx.beginPath();
      ctx.strokeStyle = `hsl(${a}, 100%, 50%)`;
      ctx.lineWidth = RING_WIDTH;
      ctx.arc(CENTER, CENTER, RING_RADIUS, ((a - 1.2) * Math.PI) / 180, ((a + 1.2) * Math.PI) / 180);
      ctx.stroke();
    }
  } catch (e) {
    // No 2D canvas in this environment - an unfilled cache still lets the
    // rest of the picker render and get exercised.
  }
  return cache;
}

// A floating hue-ring/SV-square picker. Returns {open, close, destroy}.
export function mountColorPicker({onChange = () => {}} = {}) {
  const pk = {h: 0, s: 0, v: 1, alpha: 1};
  const ringCache = buildRingCache();

  const root = document.createElement('div');
  root.className = 'theme-picker';
  root.hidden = true;

  const canvas = document.createElement('canvas');
  canvas.className = 'theme-picker-canvas';
  canvas.width = SIZE * 2;
  canvas.height = SIZE * 2;
  root.appendChild(canvas);

  const alphaRow = document.createElement('div');
  alphaRow.className = 'theme-picker-row';
  const alphaLabel = document.createElement('label');
  alphaLabel.textContent = 'Alpha';
  const alphaInput = document.createElement('input');
  alphaInput.type = 'range';
  alphaInput.min = '0';
  alphaInput.max = '100';
  alphaInput.className = 'theme-picker-alpha';
  const alphaValue = document.createElement('span');
  alphaValue.className = 'theme-picker-alpha-value';
  alphaRow.appendChild(alphaLabel);
  alphaRow.appendChild(alphaInput);
  alphaRow.appendChild(alphaValue);
  root.appendChild(alphaRow);

  const hexRow = document.createElement('div');
  hexRow.className = 'theme-picker-row';
  const swatch = document.createElement('span');
  swatch.className = 'theme-picker-swatch';
  const hexInput = document.createElement('input');
  hexInput.className = 'theme-picker-hex';
  hexInput.maxLength = 7;
  hexInput.spellcheck = false;
  hexRow.appendChild(swatch);
  hexRow.appendChild(hexInput);
  root.appendChild(hexRow);

  document.body.appendChild(root);

  function currentHex() {
    return hsvToHex(pk.h, pk.s, pk.v);
  }

  function syncControls() {
    const hex = currentHex();
    hexInput.value = hex.toUpperCase();
    swatch.style.background = hex + alphaToHex(pk.alpha);
    const pct = Math.round(pk.alpha * 100);
    alphaInput.value = String(pct);
    alphaValue.textContent = `${pct}%`;
  }

  function redraw() {
    drawPicker(canvas, ringCache, pk);
  }

  function apply() {
    onChange({hex: currentHex(), alpha: pk.alpha});
  }

  function positionNear(anchor) {
    const rect = anchor.getBoundingClientRect();
    const width = SIZE + 24, height = 260;
    const viewportW = globalThis.innerWidth || 0;
    const viewportH = globalThis.innerHeight || 0;
    root.style.left = `${clamp(rect.left, 8, Math.max(8, viewportW - width - 8))}px`;
    let top = rect.bottom + 10;
    if (top + height > viewportH - 8) {
      top = Math.max(8, rect.top - height - 10);
    }
    root.style.top = `${top}px`;
  }

  canvas.addEventListener('pointerdown', (event) => {
    const rect = canvas.getBoundingClientRect();
    const scale = rect.width ? SIZE / rect.width : 1;
    const point = (ev) => ({
      x: (ev.clientX - rect.left) * scale,
      y: (ev.clientY - rect.top) * scale,
    });
    const p0 = point(event);
    const dist = Math.hypot(p0.x - CENTER, p0.y - CENTER);
    let mode = null;
    if (dist >= RING_RADIUS - RING_WIDTH / 2 - 4 && dist <= RING_RADIUS + RING_WIDTH / 2 + 4) {
      mode = 'hue';
    } else if (Math.abs(p0.x - CENTER) <= SV_HALF + 4 && Math.abs(p0.y - CENTER) <= SV_HALF + 4) {
      mode = 'sv';
    }
    if (!mode) return;
    event.preventDefault();
    canvas.setPointerCapture(event.pointerId);
    const move = (ev) => {
      const p = point(ev);
      if (mode === 'hue') {
        pk.h = ((Math.atan2(p.y - CENTER, p.x - CENTER) * 180) / Math.PI + 360) % 360;
      } else {
        pk.s = clamp((p.x - (CENTER - SV_HALF)) / (SV_HALF * 2), 0, 1);
        pk.v = clamp(1 - (p.y - (CENTER - SV_HALF)) / (SV_HALF * 2), 0, 1);
      }
      redraw();
      syncControls();
      apply();
    };
    move(event);
    const end = () => {
      canvas.removeEventListener('pointermove', move);
      canvas.removeEventListener('pointerup', end);
      canvas.removeEventListener('pointercancel', end);
    };
    canvas.addEventListener('pointermove', move);
    canvas.addEventListener('pointerup', end);
    canvas.addEventListener('pointercancel', end);
  });

  alphaInput.addEventListener('input', () => {
    pk.alpha = Number(alphaInput.value) / 100;
    syncControls();
    apply();
  });

  hexInput.addEventListener('change', () => {
    let value = hexInput.value.trim();
    if (!value.startsWith('#')) value = '#' + value;
    if (!/^#([0-9a-f]{3}|[0-9a-f]{6})$/i.test(value)) return;
    const [h, s, v] = rgbToHsv(...hexToRgb(value));
    pk.h = h; pk.s = s; pk.v = v;
    redraw();
    syncControls();
    apply();
  });

  const outsideClick = (event) => {
    if (root.hidden) return;
    if (root.contains(event.target)) return;
    close();
  };
  document.addEventListener('pointerdown', outsideClick, true);

  function open(anchor, hex, alpha) {
    const [h, s, v] = rgbToHsv(...hexToRgb(hex));
    pk.h = h; pk.s = s; pk.v = v; pk.alpha = alpha;
    root.hidden = false;
    positionNear(anchor);
    redraw();
    syncControls();
  }

  function close() {
    root.hidden = true;
  }

  function destroy() {
    document.removeEventListener('pointerdown', outsideClick, true);
    root.remove();
  }

  return {open, close, destroy};
}
