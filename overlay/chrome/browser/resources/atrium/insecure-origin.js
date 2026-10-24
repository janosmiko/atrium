// Copyright 2026 The Atrium Authors
// Split out so sidebar.js and devbar.js can share the same treatment
// without importing each other. Ported from the lab's
// isInsecure/hostOf (docs/lab/swipe-lab.html ~1916-1920).

// Not rust/atrium-core's host_is_local: it also treats private ranges as
// local for SSRF safety, which would wrongly secure a 192.168.x host here.
export function isInsecureOrigin(urlOrOrigin) {
  if (!urlOrOrigin || !urlOrOrigin.startsWith('http://')) {
    return false;
  }
  return !isLoopbackHost(hostOf(urlOrOrigin));
}

export function hostOf(urlOrOrigin) {
  return urlOrOrigin.replace(/^https?:\/\//, '').split('/')[0];
}

function isLoopbackHost(host) {
  const bare = bareHost(host).toLowerCase();
  if (bare === 'localhost' || bare.endsWith('.localhost') || bare === '::1') {
    return true;
  }
  // Chromium's IPAddress::IsLoopback treats all of 127.0.0.0/8 as loopback,
  // including shorthand like 127.1 - canonicalise through URL to catch it.
  if (!/^[0-9.]+$/.test(bare)) {
    return false;
  }
  try {
    return new URL('http://' + bare + '/').hostname.startsWith('127.');
  } catch {
    return false;
  }
}

// A bracket-delimited IPv6 host needs its own split - ":" inside the
// brackets is address, not a port separator.
function bareHost(host) {
  if (host.startsWith('[')) {
    const close = host.indexOf(']');
    return close === -1 ? host.slice(1) : host.slice(1, close);
  }
  return host.split(':')[0];
}
