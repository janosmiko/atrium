// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Shared CDP plumbing for the sidebar probes.
//
// Picking the surface is the part worth having in one place. Every
// AtriumBrowserWindow builds a sidebar surface, including the per-profile
// windows that are never shown - a user with five spaces on five profiles has
// five, all rendering identical rows. Choosing the wrong one produced three
// confident wrong diagnoses in a single session, so: visibilityState first,
// then laid-out-ness, then window area as a tie-break.
// THREE probes still open a socket of their own, and each has a reason worth
// keeping rather than a copy worth deleting:
//
//   edge-paging-probe, sidebar-diagnose - a raw ONE-SHOT socket that asks a
//     candidate surface one question and closes. Not a connect at all. The
//     surface-picking half below is what those two share.
//   lab-probe - a class with private fields that surfaces CDP protocol errors
//     (msg.error) as well as evaluation ones, and runs headless against the
//     lab rather than against the app.
//
// Anything else that needs a socket should import connect.

const PORT = process.env.ATRIUM_CDP_PORT || '9334';

/**
 * How long to wait for the socket to open, and for any one CDP reply.
 *
 * A probe with no timeout does not fail, it HANGS - and a hung probe in the VM
 * harness reads as a slow run rather than a broken one. cdp-eval.mjs was the
 * only one of the twenty-three that had this; the rest waited forever. It came
 * here rather than the other way round.
 */
export const CDP_TIMEOUT_MS = 8000;

export async function connect(target) {
  const ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((r, x) => {
    const openTimer = setTimeout(
        () => x(new Error(`socket never opened in ${CDP_TIMEOUT_MS}ms`)),
        CDP_TIMEOUT_MS);
    const done = (fn) => (e) => { clearTimeout(openTimer); fn(e); };
    ws.addEventListener('open', done(r), {once: true});
    ws.addEventListener('error', done(x), {once: true});
  });
  let id = 0;
  const pending = new Map();
  ws.addEventListener('message', (e) => {
    const m = JSON.parse(e.data);
    if (m.id) { pending.get(m.id)?.(m); pending.delete(m.id); }
  });
  const send = (method, params = {}) => new Promise((r, x) => {
    const n = ++id;
    const timer = setTimeout(() => {
      pending.delete(n);
      x(new Error(`${method} timed out after ${CDP_TIMEOUT_MS}ms`));
    }, CDP_TIMEOUT_MS);
    pending.set(n, (m) => { clearTimeout(timer); r(m); });
    ws.send(JSON.stringify({id: n, method, params}));
  });
  await send('Runtime.enable');
  const ev = async (expression) => {
    const r = await send('Runtime.evaluate',
        {expression, returnByValue: true, awaitPromise: true});
    if (r.result?.exceptionDetails) {
      // The EXPRESSION is in the message, not only the exception. A probe
      // evaluates dozens of these and "TypeError: undefined is not an object"
      // on its own names none of them. row-drop-probe's own copy did this and
      // the other twenty-one did not, which is the kind of thing that stops
      // spreading once there is one copy.
      const why = r.result.exceptionDetails.exception?.description ||
          r.result.exceptionDetails.text || 'threw';
      throw new Error(`${why} :: ${expression.slice(0, 80)}`);
    }
    return r.result?.result?.value;
  };
  return {ws, send, ev};
}

/**
 * What to ask a candidate surface about itself, and how to score the answer.
 *
 * Exported because swipe-gesture-probe picks its own surface (it needs the
 * connection afterwards, and its own row/folder counts for a retry), and used
 * to rank candidates by CONTENT instead - which picks a hidden per-profile
 * surface whenever one renders more rows than the visible one. That is how it
 * ended up calling getComputedStyle on a null .rail. Sharing the
 * scoring rather than the whole picker keeps the two from drifting apart
 * again, which is the failure this file's header is already about.
 */
export const SURFACE_STATE_JS = `JSON.stringify({
  v: document.visibilityState === 'visible',
  w: document.documentElement.clientWidth,
  a: window.outerWidth * window.outerHeight,
})`;

/** Higher is more likely to be the surface a person is looking at. */
export function surfaceRank(m) {
  return (m.v ? 2 : 0) + (m.w > 0 ? 1 : 0);
}

/** The sidebar surface a person is actually looking at. */
export async function visibleSidebar() {
  const targets = await (await fetch(`http://127.0.0.1:${PORT}/json/list`)).json();
  const candidates = targets.filter((t) => t.url.includes('top-chrome/sidebar'));
  if (!candidates.length) {
    console.error(`no sidebar surface on :${PORT}`);
    process.exit(2);
  }
  let best = null;
  let lastConnectError = null;
  for (const candidate of candidates) {
    let c;
    try {
      c = await connect(candidate);
    } catch (err) {
      lastConnectError = err;
      continue;
    }
    const state = await c.ev(SURFACE_STATE_JS).catch(() => null);
    const m = state ? JSON.parse(state) : {v: false, w: 0, a: 0};
    const score = surfaceRank(m);
    if (!best || score > best.score || (score === best.score && m.a > best.area)) {
      best?.c.ws.close();
      best = {c, score, area: m.a};
    } else {
      c.ws.close();
    }
  }
  if (!best) {
    console.error(`no sidebar surface connected on :${PORT} ` +
        `(${candidates.length} candidate${candidates.length === 1 ? '' : 's'}, ` +
        `last error: ${lastConnectError?.message || lastConnectError})`);
    process.exit(2);
  }
  return best.c;
}

export function checker() {
  const state = {failures: 0};
  return {
    state,
    check(label, actual, expected) {
      const ok = JSON.stringify(actual) === JSON.stringify(expected);
      if (!ok) { state.failures++; }
      console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}` +
          (ok ? '' : `\n       expected ${JSON.stringify(expected)}` +
                     `\n       actual   ${JSON.stringify(actual)}`));
    },
    done(name) {
      console.log(`\n${state.failures ? state.failures + ' FAILED' : 'all checks passed'}`);
      process.exit(state.failures ? 1 : 0);
    },
  };
}

export const wait = (ms) => new Promise((r) => setTimeout(r, ms));
// The overview .ovrailpane sits first among .railpane matches and renders no
// rows. [data-space-index] is what a real space pane carries and it never does.
export const PANE = `(document.querySelector('.railpane[data-space-index]') || document.querySelector('.sidebar'))`;
