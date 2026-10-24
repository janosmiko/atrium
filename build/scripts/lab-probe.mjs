#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Drives docs/lab/swipe-lab.html headlessly and asserts its interaction
// contract. Exit 0 means every assertion passed; non-zero means one did not.
//
// Why this exists: the lab is the design reference and the
// source of the production chrome. Until now the only way to check it was a
// human opening it and swiping. That is not a pass condition - it cannot gate a
// build, and it cannot tell you which of two changes broke the gesture.
//
// No dependencies. Node's global WebSocket (22+) speaks CDP directly, so there
// is nothing to install and nothing to keep in sync with a Chrome version.
//
//   node build/scripts/lab-probe.mjs            run the suite
//   node build/scripts/lab-probe.mjs --invert   flip one assertion; MUST exit non-zero
//   node build/scripts/lab-probe.mjs --keep     leave the browser up for inspection
//
// --invert is not a curiosity. A harness that cannot fail is indistinguishable
// from one that passes, and this whole file exists to be trusted unattended.

import { spawn } from 'node:child_process'
import { mkdtemp, readFile, rm, access } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join, resolve, dirname } from 'node:path'
import { fileURLToPath } from 'node:url'

const HERE = dirname(fileURLToPath(import.meta.url))
const REPO = resolve(HERE, '..', '..')
const LAB = join(REPO, 'docs', 'lab', 'swipe-lab.html')

const INVERT = process.argv.includes('--invert')
const KEEP = process.argv.includes('--keep')

const CHROME_CANDIDATES = [
  '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
  '/Applications/Chromium.app/Contents/MacOS/Chromium',
  join(process.env.HOME ?? '', 'chromium-atrium/src/out/atrium/Chromium.app/Contents/MacOS/Chromium'),
]

async function findChrome() {
  for (const path of CHROME_CANDIDATES) {
    try { await access(path); return path } catch { /* next */ }
  }
  throw new Error(`no browser found; tried:\n  ${CHROME_CANDIDATES.join('\n  ')}`)
}

// --- CDP ---------------------------------------------------------------

class CDP {
  #ws; #id = 0; #pending = new Map(); #sessionId = null

  static async connect(url) {
    const cdp = new CDP()
    cdp.#ws = new WebSocket(url)
    await new Promise((ok, fail) => {
      cdp.#ws.addEventListener('open', ok, { once: true })
      cdp.#ws.addEventListener('error', () => fail(new Error(`cannot reach ${url}`)), { once: true })
    })
    cdp.#ws.addEventListener('message', (e) => {
      const msg = JSON.parse(e.data)
      const entry = cdp.#pending.get(msg.id)
      if (!entry) return
      cdp.#pending.delete(msg.id)
      msg.error ? entry.fail(new Error(msg.error.message)) : entry.ok(msg.result)
    })
    return cdp
  }

  send(method, params = {}) {
    const id = ++this.#id
    const payload = { id, method, params }
    if (this.#sessionId) payload.sessionId = this.#sessionId
    this.#ws.send(JSON.stringify(payload))
    return new Promise((ok, fail) => this.#pending.set(id, { ok, fail }))
  }

  useSession(sessionId) { this.#sessionId = sessionId }
  close() { this.#ws.close() }

  /// Runs an expression in the PAGE via CDP Runtime.evaluate - this is a
  /// remote call into a sandboxed headless browser, not JS eval() in this
  /// process. Nothing from outside this file reaches it.
  ///
  /// Evaluates in the page and returns the value, turning a thrown JS error
  /// into a thrown Node error rather than a silent undefined.
  async evaluate(expression) {
    const { result, exceptionDetails } = await this.send('Runtime.evaluate', {
      expression, returnByValue: true, awaitPromise: true,
    })
    if (exceptionDetails) {
      throw new Error(exceptionDetails.exception?.description ?? exceptionDetails.text)
    }
    return result.value
  }
}

// --- assertions --------------------------------------------------------

const results = []
function check(name, actual, expected, note = '') {
  const pass = JSON.stringify(actual) === JSON.stringify(expected)
  results.push({ name, pass, actual, expected, note })
  const mark = pass ? 'ok  ' : 'FAIL'
  console.log(`${mark} ${name}${pass ? '' : `\n       expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}${note ? `\n       ${note}` : ''}`}`)
}

// --- run ---------------------------------------------------------------

let chrome, profileDir
try {
  const bin = await findChrome()
  profileDir = await mkdtemp(join(tmpdir(), 'atrium-lab-probe-'))

  chrome = spawn(bin, [
    '--headless=new',
    '--remote-debugging-port=0',
    `--user-data-dir=${profileDir}`,
    '--no-first-run',
    '--no-default-browser-check',
    '--disable-extensions',
    '--allow-file-access-from-files',
    'about:blank',
  ], { stdio: ['ignore', 'ignore', 'pipe'] })

  // The ephemeral port lands in DevToolsActivePort once the server is up.
  const portFile = join(profileDir, 'DevToolsActivePort')
  let wsPath = null
  for (let i = 0; i < 100 && !wsPath; i++) {
    await new Promise((r) => setTimeout(r, 100))
    try {
      const [port, path] = (await readFile(portFile, 'utf8')).split('\n')
      if (port && path) wsPath = `ws://127.0.0.1:${port}${path}`
    } catch { /* not up yet */ }
  }
  if (!wsPath) throw new Error('browser never wrote DevToolsActivePort')

  const cdp = await CDP.connect(wsPath)
  const { targetId } = await cdp.send('Target.createTarget', { url: `file://${LAB}` })
  const { sessionId } = await cdp.send('Target.attachToTarget', { targetId, flatten: true })
  cdp.useSession(sessionId)
  await cdp.send('Runtime.enable')

  // The lab builds its panes on load; poll for the globals rather than sleep a
  // fixed amount, so a slow machine does not produce a flaky failure.
  let ready = false
  for (let i = 0; i < 100 && !ready; i++) {
    await new Promise((r) => setTimeout(r, 100))
    ready = await cdp.evaluate(`typeof machine === 'object' && typeof ui === 'object'
                            && document.querySelectorAll('.pane').length > 0`).catch(() => false)
  }
  if (!ready) throw new Error('lab globals never appeared - machine/ui/.pane')

  // 1. The rail keeps three panes mounted (prev, current, next) and translates
  //    them. This is the architecture the whole migration is buying; if it
  //    regresses to rebuild-per-frame the blink comes back.
  check('rail mounts exactly 3 panes',
    await cdp.evaluate(`document.querySelectorAll('#rail .pane').length`),
    INVERT ? 99 : 3,
    'ui.rebuildPanes builds offsets -1, 0, +1')

  // 2. Panes are positioned only by transform. A pane that moves by changing
  //    left/width is doing layout per frame, which is the SwiftUI failure mode.
  check('panes are positioned by transform only',
    await cdp.evaluate(`
      (() => {
        const panes = [...document.querySelectorAll('#rail .pane')]
        return panes.every(p => {
          const s = p.style
          return s.transform.includes('translateX') && !s.left && !s.width
        })
      })()`),
    true)

  // 3. Overview precedes the first real space (commit 2c75355).
  check('Overview sits before space 0',
    await cdp.evaluate(`SPACES[0].overview === true`), true)

  // 4. A discrete switch moves spaceIndex and leaves the rail at three panes -
  //    i.e. the switch does not leak panes or strand them.
  // Polled, not slept: headless throttles requestAnimationFrame, so the
  // settle takes longer here than the lab's 280ms settleDur implies. A fixed
  // wait produced a false failure at 600ms on the first run.
  const jump = await cdp.evaluate(`
    (async () => {
      const before = machine.spaceIndex
      const target = before + 1 < SPACES.length ? before + 1 : before - 1
      machine.jumpTo(target)
      const deadline = Date.now() + 5000
      while (machine.spaceIndex !== target && Date.now() < deadline) {
        await new Promise(r => setTimeout(r, 50))
      }
      return { before, target, after: machine.spaceIndex,
               waitedMs: 5000 - (deadline - Date.now()),
               panes: document.querySelectorAll('#rail .pane').length }
    })()`)
  if (jump.after === jump.target) console.log(`     (settled in ~${jump.waitedMs}ms)`)
  check('jumpTo commits the space index', jump.after, jump.target)
  check('rail still holds 3 panes after a switch', jump.panes, 3)

  // 5. Every pane carries its padding inside a full-width box - the single
  //    box model that makes a transient width proposal harmless. This is the
  //    property SwiftUI could not hold across the scrub handoff.
  check('panes are full-bleed with inner padding',
    await cdp.evaluate(`
      (() => {
        const p = document.querySelector('#rail .pane')
        const cs = getComputedStyle(p)
        return cs.position === 'absolute' && parseFloat(cs.paddingLeft) > 0
      })()`),
    true)

  // 6. The favourites grid is built with DOM APIs, not an innerHTML template
  //    (option A). The mount point must be gone and the grid present.
  check('favgrid replaced its mount point',
    await cdp.evaluate(`
      (() => {
        const grid = document.querySelector('#rail .pane .favgrid')
        const mount = document.querySelector('#rail .pane .favgrid-mount')
        return !!grid && !mount
      })()`),
    true)

  check('favgrid cell count matches the space favourites',
    await cdp.evaluate(`
      (() => {
        const si = machine.spaceIndex
        const expected = (SPACES[si].favs || 0)
        const pane = document.querySelector('#rail .pane[data-si="' + si + '"]')
        const cells = pane ? pane.querySelectorAll('.favgrid .fav').length : -1
        return cells === expected
      })()`),
    true)

  // 7. THE security property option A buys. A favourite label is a tab title in
  //    the real browser - a string an arbitrary web page controls, rendered on a
  //    page holding chrome.send. It must reach the DOM as text, never as markup.
  check('a hostile favourite label is not parsed as markup',
    await cdp.evaluate(`
      (() => {
        const hostile = '<img src=x onerror="window.__pwned=1">'
        const grid = buildFavGrid([{ t: hostile, c: '#123456' }], 0)
        const cell = grid.querySelector('.fav')
        return {
          noImgElement: grid.querySelectorAll('img').length === 0,
          titleIsExactText: cell.title === hostile,
          notExecuted: window.__pwned === undefined,
        }
      })()`),
    { noImgElement: true, titleIsExactText: true, notExecuted: true },
    'if this fails, the label reached an HTML parser')

  if (!KEEP) cdp.close()
} catch (err) {
  console.error(`\nprobe error: ${err.message}`)
  results.push({ name: 'harness', pass: false })
} finally {
  if (chrome && !KEEP) {
    chrome.kill('SIGTERM')
    await new Promise((r) => setTimeout(r, 300))
    if (!chrome.killed) chrome.kill('SIGKILL')
  }
  // Profile dir lives in the OS temp dir, never the repo. Removing only what
  // this script created.
  if (profileDir && !KEEP) await rm(profileDir, { recursive: true, force: true }).catch(() => {})
}

const passed = results.filter((r) => r.pass).length
console.log(`\n${passed}/${results.length} assertions passed`)
process.exit(passed === results.length && results.length > 0 ? 0 : 1)
