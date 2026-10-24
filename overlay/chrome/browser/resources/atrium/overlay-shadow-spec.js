// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//
// getComputedStyle().boxShadow parsing shared by devbar.js and
// toast-surface.js: both push a shadow AtriumBrowserWindow
// cannot resolve itself, since overlay.css's --ov-shadow-tight varies by
// light, dark and native-chrome.

// Splits a computed box-shadow value into its comma-separated layers,
// respecting commas inside a colour function.
export function splitShadowLayers(boxShadow) {
  if (!boxShadow || boxShadow === 'none') {
    return [];
  }
  return boxShadow.split(/,(?![^(]*\))/).map((s) => s.trim());
}

// Parses one layer into the numbers AtriumOverlayShadowLayer needs. Only
// rgb()/rgba() colours are handled - what --ov-shadow-tight resolves to. A
// color-mix() ring layer serialises as color(srgb ...) and is not this
// function's job (see toast-stack.css's separate ring).
export function parseShadowLayer(layer) {
  const colorMatch = layer.match(/rgba?\(([^)]+)\)/);
  if (!colorMatch) {
    return null;
  }
  const [r, g, b, a] =
      colorMatch[1].split(/[\s,/]+/).filter(Boolean).map(Number);
  const nums = (layer.match(/-?\d+(\.\d+)?px/g) || []).map(parseFloat);
  const [offsetX, offsetY, blur] = nums;
  if (offsetX === undefined || offsetY === undefined || blur === undefined ||
      Number.isNaN(r) || Number.isNaN(g) || Number.isNaN(b)) {
    return null;
  }
  return {offsetX, offsetY, blur, r, g, b, a: a === undefined ? 1 : a};
}
