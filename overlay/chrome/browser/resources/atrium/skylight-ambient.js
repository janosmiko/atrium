// Copyright 2026 The Atrium Authors
//
// toggles the class skylight-ambient.css animates.

export function setSkylightAmbient(enabled, {host} = {}) {
  const win = host || document.getElementById('atrium-root');
  if (!win) {
    return;
  }
  win.classList.toggle('skyambient', !!enabled);
}
