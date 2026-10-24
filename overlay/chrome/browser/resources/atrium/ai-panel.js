// Copyright 2026 The Atrium Authors
//
// the AI panel shell only. Attach-to-tab,
// the kill switch and per-space memory live elsewhere. Summarize/chat go
// through an injectable backend until a real model is wired.

import {icon, markFlock} from './icons.js';
import {renderMarkdown} from './markdown-render.js';
import {installVimSurface} from './vim-surface.js';
import {installHover} from './hint-triggers.js';
import {REAL_TIMEOUT_CLOCK} from './clock.js';
import {TRANSLATE_LANGUAGES, languageCode, languageLabel, languageName} from
    './translate-languages.js';

const MIN_WIDTH = 220;
// The width a double-click on the handle returns to, and the lab's own default
// (swipe-lab.html:1527). Named here rather than repeated at the two use sites.
const DEFAULT_WIDTH = 280;
const MAX_WIDTH_CAP = 400;
const MAX_MEMORY_ENTRIES_PER_SPACE = 200;

// The 45%-of-window ceiling stays in SetAiPanelWidth: this document IS the
// panel, so clamping against its own innerWidth pinned it at 45% of itself.
/** Clamps a candidate panel width to [220, 400]. */
export function clampWidth(width) {
  return Math.min(MAX_WIDTH_CAP, Math.max(MIN_WIDTH, width));
}

// Ports the lab's mocked AI: a fixed summary and a setTimeout'd "(mock)"
// reply, no fetch or model session.
export const STUB_AI_BACKEND = {
  summarize(context) {
    const label = (context && context.pageTitle) || 'this page';
    return `"${label}" covers three areas relevant to this space. Key ` +
        'decisions sit mid-page; two questions remain open. Action items: ' +
        'review the draft, confirm the timeline, share it.';
  },
  // Returns a canceller so a second question fired before the first
  // resolves does not land two answers.
  chat(context, query, respond) {
    const label = (context && context.pageTitle) || 'this page';
    const handle = setTimeout(() => {
      respond(`(mock) About "${label}" - the page answers this in ` +
              'its second section; two lines cited.');
    }, 450);
    return () => clearTimeout(handle);
  },
};

// Both the on-device and Ollama/custom-endpoint paths can finish out of
// order, so replies are matched by id, not by send order.
const MAX_PENDING_SUMMARIZE = 8;

// How many pages a pinned thread carries as context. A constant
// rather than a setting: nobody has asked to tune it, and the cost of getting
// it wrong is a slower answer, not a broken one.
export const MAX_ROAMING_PAGES = 5;

// summarize() and chat() return nothing and answer later through the two
// globals below, once the native reply echoes the id the request sent. Both
// kinds share one id sequence, so the Summary globals also carry chat answers.
export function createNativeAiBackend() {
  let nextId = 0;
  const pending = new Map();
  function resolve(id, text) {
    const render = pending.get(id);
    if (!render) {
      return;
    }
    pending.delete(id);
    render(text, true);
  }
  window.atriumSetAiPanelSummary = resolve;
  // Its own arm, not `resolve`: a stream that dies partway must keep the text
  // it managed to send, and only the error push can say which this is.
  window.atriumSetAiPanelSummaryError = (id, text) => {
    const render = pending.get(id);
    if (!render) {
      return;
    }
    pending.delete(id);
    render(text, true, true);
  };
  // the answer so far, while it is still arriving. The id stays in
  // `pending` - only the final push retires it, so a stream that ends in an
  // error still reaches the same render with what it managed to say.
  window.atriumSetAiPanelSummaryChunk = (id, text, reasoning) => {
    const render = pending.get(id);
    if (render) {
      render(text, false, false, reasoning || '');
    }
  };
  function request(message, context, render, extraArgs) {
    const id = ++nextId;
    pending.set(id, render);
    // A reply that never comes (an unanswered disabled/early-return path)
    // must not grow this map forever - drop the oldest past the cap.
    if (pending.size > MAX_PENDING_SUMMARIZE) {
      pending.delete(pending.keys().next().value);
    }
    const tabId = context && context.tabId != null ? String(context.tabId) : '';
    // Trails the existing args so an older native build still
    // reads the first three and answers about the current page alone.
    const extraTabIds =
        (context && context.extraTabIds ? context.extraTabIds : []).map(String);
    chrome.send(message, [tabId, id, ...extraArgs, extraTabIds]);
    return id;
  }
  return {
    summarize(context, render) {
      request('aiPanelSummarizeRequested', context, render, []);
    },
    // The canceller only drops the answer. The model call runs to its end.
    chat(context, query, respond) {
      const id = request('aiPanelChatRequested', context, respond, [query]);
      return () => pending.delete(id);
    },
  };
}

// The prompt is what reaches the model, so the tests pin these
// strings rather than the button labels.
export const QUICK_ACTIONS = [
  {id: 'summarize', label: 'Summarize', icon: 'summarize',
   prompt: (language) => `Summarize this page in ${language}.`},
  {id: 'bullets', label: 'Bullet points', icon: 'bullets',
   prompt: (language) =>
       `Summarize this page as bullet points in ${language}.`},
];

// Starts on the Settings > AI tag and holds the panel's own choice
// after that: a pick here is for the thread in front of the user, so it is
// never written back to settings.
function buildLanguagePicker() {
  const select = document.createElement('select');
  select.className = 'aiplang';
  select.dataset.aiPanelLanguage = '';
  select.title = 'Summary language';
  select.setAttribute('aria-label', 'Summary language');
  for (const tag of TRANSLATE_LANGUAGES) {
    const option = document.createElement('option');
    option.value = tag;
    // The closed select shows the selected option's own text, so the code has
    // to be the option's text. showLanguage() puts the name in the hint.
    option.textContent = languageCode(tag);
    option.title = languageLabel(tag);
    select.appendChild(option);
  }
  return select;
}

function buildPanel() {
  const panel = document.createElement('div');
  panel.className = 'aipanel';

  const header = document.createElement('div');
  header.className = 'aiphead';
  const title = document.createElement('span');
  title.className = 'aiptitle';
  title.textContent = 'AI';
  const pin = document.createElement('button');
  pin.className = 'aippin ov-btn';
  pin.type = 'button';
  pin.dataset.aiPanelPin = '';
  pin.title = 'Pin - keep open across tabs';
  pin.setAttribute('aria-label', 'Pin - keep open across tabs');
  pin.setAttribute('aria-pressed', 'false');
  pin.appendChild(icon('pin'));
  const clear = document.createElement('button');
  clear.className = 'aipclear ov-btn';
  clear.type = 'button';
  clear.dataset.aiPanelClear = '';
  clear.title = 'Clear conversation';
  clear.setAttribute('aria-label', 'Clear conversation');
  clear.appendChild(icon('trash'));
  const close = document.createElement('button');
  close.className = 'aipclose ov-btn';
  close.type = 'button';
  close.dataset.aiPanelClose = '';
  close.title = 'Close';
  close.setAttribute('aria-label', 'Close');
  close.appendChild(icon('close'));
  const actions = document.createElement('div');
  actions.className = 'aipactions';
  actions.append(clear, pin, close);
  header.append(title, actions);

  const context = document.createElement('div');
  context.className = 'aictx';

  const summary = document.createElement('div');
  summary.className = 'aisum';

  const chatLog = document.createElement('div');
  chatLog.className = 'aichatlog';

  const chatRow = document.createElement('div');
  chatRow.className = 'aichatrow';
  const input = document.createElement('input');
  input.className = 'aichatinput';
  input.type = 'text';
  input.placeholder = 'Ask about this page';
  const send = document.createElement('button');
  send.className = 'aichatsend ov-btn';
  send.type = 'button';
  send.dataset.aiPanelSend = '';
  send.title = 'Send';
  send.setAttribute('aria-label', 'Send');
  send.appendChild(icon('send'));
  chatRow.append(input, send);

  const quick = document.createElement('div');
  quick.className = 'aiquick';
  for (const action of QUICK_ACTIONS) {
    const button = document.createElement('button');
    button.className = 'aiquickbtn ov-btn';
    button.type = 'button';
    button.dataset.aiPanelQuick = action.id;
    // The glyph alone, so the row fits the picker beside it. The label reaches
    // the reader through the hover hint installHover raises over aria-label.
    button.title = action.label;
    button.setAttribute('aria-label', action.label);
    button.appendChild(icon(action.icon));
    quick.appendChild(button);
  }
  quick.appendChild(buildLanguagePicker());

  // A sibling of the resize grip, not its container - the grip stays pinned
  // to the panel's true edge (currentWidth() drags it), while the card
  // itself only needs to fill the space the panel's own padding leaves.
  // Under the log, over the actions, never in the log: see showThinking.
  const status = document.createElement('div');
  status.className = 'aistatus';

  const card = document.createElement('div');
  card.className = 'aipcard';
  card.append(header, context, summary, chatLog, status, quick, chatRow);

  const handle = document.createElement('div');
  // ovresize: the regular docked sidebar's own resize-handle look
  // (sidebar-overlay-resize.css) - hairline at rest, accent on hover/drag.
  handle.className = 'aipresize ovresize';
  handle.dataset.aiPanelResize = '';
  // Both gestures named, because a double-click reset is not discoverable
  // otherwise - the lab says the same on its own handle (swipe-lab.html:1526).
  handle.title = 'Drag to resize \u00b7 double-click resets';

  panel.append(card, handle);
  return panel;
}

// How far off the bottom still counts as "reading the newest".
// Not 0: a fractional scrollTop from a trackpad or a zoomed display would
// otherwise read as the reader having deliberately scrolled up.
const FOLLOW_SLACK_PX = 24;

export function isAtBottom(el) {
  return el.scrollHeight - el.scrollTop - el.clientHeight <= FOLLOW_SLACK_PX;
}

// Reused across a rebuild, never replaced: the user can open it
// while the thinking still streams in, and a new element snaps it shut under
// them. No reasoning means no control at all, rather than an empty one.
function applyThinking(host, reasoning) {
  let block = host.querySelector('.aithink');
  if (!reasoning) {
    if (block) {
      block.remove();
    }
    return null;
  }
  if (!block) {
    block = document.createElement('details');
    block.className = 'aithink';
    const label = document.createElement('summary');
    label.className = 'aithink-label';
    label.textContent = 'Thinking';
    const body = document.createElement('div');
    body.className = 'aithink-body';
    block.append(label, body);
    host.prepend(block);
  }
  // textContent, not the markdown renderer: this is the model talking to
  // itself, and half a sentence mid-stream is not valid markdown.
  const body = block.querySelector('.aithink-body');
  // The fold is its own scroller, so the chat log's following does
  // not reach it. Read the position before the write, or the new text has
  // already moved the bottom out from under the reader.
  const follow = isAtBottom(body);
  body.textContent = reasoning;
  if (follow) {
    body.scrollTop = body.scrollHeight;
  }
  return block;
}

function appendBubble(chatLog, role, text, reasoning = '') {
  const bubble = document.createElement('div');
  bubble.className = `aibubble aibubble-${role}`;
  // only the model formats. What the user typed is what they meant,
  // so an asterisk in their question stays an asterisk.
  if (role === 'ai') {
    const body = document.createElement('div');
    body.className = 'aibubbletext';
    renderMarkdown(body, text);
    bubble.appendChild(body);
    applyThinking(bubble, reasoning);
  } else {
    bubble.textContent = text;
  }
  chatLog.appendChild(bubble);
  return bubble;
}

// The streaming half of appendBubble: same two parts, kept in place so the
// fold the user opened mid-answer stays open.
function updateAiBubble(bubble, text, reasoning) {
  renderMarkdown(bubble.querySelector('.aibubbletext'), text);
  applyThinking(bubble, reasoning);
}

// No `aibubble-ai`/`aibubble-me` role: a role class is how the chat log picks
// out real messages (aiBubbles() in the tests, and any future memory replay),
// and a loader must not read back as one until it is replaced by an answer.
// Sits under the text a dead stream left behind, rather than
// replacing it: half an answer plus a reason beats a blank pane.
function appendErrorNote(host, text) {
  const note = document.createElement('div');
  note.className = 'aierrnote';
  note.textContent = text;
  host.appendChild(note);
  return note;
}

function buildLoader() {
  const loader = document.createElement('div');
  loader.className = 'ailoader';
  loader.setAttribute('aria-live', 'polite');
  const label = document.createElement('span');
  label.className = 'ailoader-label';
  label.textContent = 'Thinking';
  loader.append(label, markFlock());
  return loader;
}

// Above the prompt, not at the end of the log: a reasoning model
// streams its reasoning first, and as a bubble the loader was removed by that
// chunk, leaving the panel idle-looking for as long as the model thought.
function showThinking(panel) {
  const loader = buildLoader();
  panel.querySelector('.aistatus').replaceChildren(loader);
  return loader;
}

function buildOffState() {
  const off = document.createElement('div');
  off.className = 'aipaneloff';
  off.textContent =
      'AI is off. Turn it on in Settings › AI to summarize or chat.';
  return off;
}

/**
 * @param {Element} root
 * @param {{backend?: {summarize: Function, chat: Function},
 *          hotkey?: string, getContext?: Function, enabled?: boolean}} [opts]
 */
export function installAiPanel(root, opts = {}) {
  // Guards against a repeat install stacking listeners. Can't key off
  // '.aipanel' any more - the kill switch removes that node on purpose.
  if (root.dataset.aiPanelInstalled) {
    return;
  }
  root.dataset.aiPanelInstalled = '1';

  // The quick actions are glyphs alone, so this is where their labels live.
  // Scoped to the root rather than the body: one route per document in
  // production, but the tests build many panels in one.
  installHover(root, opts.clock || REAL_TIMEOUT_CLOCK);

  const backend = opts.backend || STUB_AI_BACKEND;
  const getContext = opts.getContext || (() => ({pageTitle: null}));

  const state = {
    open: false,
    width: 280,
    onRight: false,
    hotkey: opts.hotkey || '`',
    pendingCancel: null,
    // The chat loader bubble a pending reply owns, so a cancel can pull it
    // out of the log instead of leaving it to be stashed as if it were text.
    pendingLoader: null,
    pinned: false,
    // The thread a pinned panel carries between tabs, held as
    // {summary, chat} node lists while unpinned so re-pinning resumes it
    // rather than starting empty. Null until the first pin.
    roaming: null,
    // Tab ids the pinned thread has seen, oldest first, so the model gets the
    // pages the question is likely about. Capped at MAX_ROAMING_PAGES.
    roamingTabs: [],
    // True while the reader is at the newest text. Scrolling up
    // clears it, so an answer arriving cannot yank them off what they are
    // reading. Starts true - an empty log is at its own bottom.
    following: true,
    // `active` is the latest tab/space identity pushed from native, tracked
    // even while closed. `attached` is the one the panel's body currently
    // shows - they diverge only for an unpinned panel mid-close.
    active: null,
    attached: null,
    enabled: opts.enabled !== false,
    autoSummarize: opts.autoSummarize !== false,
    translateLanguage: opts.translateLanguage || '',
  };

  // Cleared by the kill switch - a privacy control clears
  // the data it governs.
  const aiMemory = new Map();

  function recordMemory(kind, text) {
    const spaceIndex = currentContext().spaceIndex;
    if (spaceIndex == null) {
      return;
    }
    const list = aiMemory.get(spaceIndex) || [];
    list.push({kind, text, at: Date.now()});
    if (list.length > MAX_MEMORY_ENTRIES_PER_SPACE) {
      list.shift();
    }
    aiMemory.set(spaceIndex, list);
  }

  function getAiMemory(spaceIndex) {
    return (aiMemory.get(spaceIndex) || []).slice();
  }

  // The tab alone is the identity: TabId is a process-wide counter stored on
  // the WebContents, so a tab moved to another space keeps its conversation.
  function sameTarget(a, b) {
    if (!a || !b) {
      return a === b;
    }
    return a.tabId === b.tabId;
  }

  function currentContext() {
    return state.attached || getContext();
  }

  // Pins the view to the newest text, but only while the reader
  // has not scrolled away from it. Called after anything that grows the log.
  function followChatLog() {
    const chatLog = panel && panel.querySelector('.aichatlog');
    if (chatLog && state.following) {
      chatLog.scrollTop = chatLog.scrollHeight;
    }
  }

  // Newest last, deduped, oldest dropped past MAX_ROAMING_PAGES.
  // Bounded because a pinned panel can outlive a long browsing session, and
  // every page in here costs tokens on every question asked afterwards.
  function noteRoamingPage(target) {
    if (!target || target.tabId == null) {
      return;
    }
    state.roamingTabs = state.roamingTabs.filter((id) => id !== target.tabId);
    state.roamingTabs.push(target.tabId);
    if (state.roamingTabs.length > MAX_ROAMING_PAGES) {
      state.roamingTabs.splice(0, state.roamingTabs.length - MAX_ROAMING_PAGES);
    }
  }

  // A pinned thread carries one conversation across pages, so an
  // answer written for one page sat directly under a context line naming the
  // next. The mark is what keeps an answer attached to its own page.
  function appendPageChangeMark(target) {
    const chatLog = panel && panel.querySelector('.aichatlog');
    // Nothing above it to separate: the summary and the log are both empty, so
    // a mark would announce a move away from a page nothing was said about.
    if (!chatLog ||
        (!chatLog.childNodes.length &&
         !panel.querySelector('.aisum').childNodes.length)) {
      return;
    }
    const mark = document.createElement('div');
    mark.className = 'aipagemark';
    const label = target && target.pageTitle;
    mark.textContent = label ? `Now on: ${label}` : 'Now on another page';
    chatLog.appendChild(mark);
    followChatLog();
  }

  // Set only while the "on" surface is built; null while the kill switch is
  // off, so a stray callback can't reach into DOM that was just torn down.
  let panel = null;
  let onKeyDown = null;
  let onRootClick = null;
  let vimSurface = null;
  // The toggle arrives after the first paint, so the surface installs either
  // way and reads this rather than waiting for the push.
  let vimOn = false;
  let dragStartX = null;
  let dragStartWidth = null;
  let nextSummarizeId = 0;

  // The container the browser sized, not a width this document chose - the
  // panel fills it, so the viewport is the width the user sees.
  function currentWidth() {
    return document.documentElement.clientWidth || state.width;
  }

  function refreshContextLine() {
    if (!panel) {
      return;
    }
    const label = currentContext().pageTitle;
    panel.querySelector('.aictx').textContent =
        label ? `About: ${label}` :
                 'No page here - open a tab to summarize or chat.';
  }

  // The panel belongs to its tab: tabId -> {open, summary, chat}.
  // Native forgets an entry when its tab leaves the window, which keeps this
  // bounded by the live tab count.
  const perTab = new Map();

  function stashAttached() {
    const target = state.attached;
    if (!panel || !target || target.tabId == null) {
      return;
    }
    const summary = [...panel.querySelector('.aisum').childNodes];
    const chat = [...panel.querySelector('.aichatlog').childNodes];
    if (!state.open && !summary.length && !chat.length) {
      perTab.delete(target.tabId);
      return;
    }
    perTab.set(target.tabId, {open: state.open, summary, chat});
  }

  // Returns whether the target's tab had the panel open.
  function restoreBodyFor(target) {
    const saved = target && perTab.get(target.tabId);
    panel.querySelector('.aisum').replaceChildren(...(saved ? saved.summary : []));
    panel.querySelector('.aichatlog').replaceChildren(...(saved ? saved.chat : []));
    // a different thread starts at its own newest. Carrying the
    // old one's scrollTop would land the reader mid-conversation.
    state.following = true;
    followChatLog();
    return !!saved && saved.open;
  }

  function attachToCurrent() {
    state.attached = state.active || getContext();
    refreshContextLine();
  }

  function applyState() {
    if (!panel) {
      return;
    }
    root.classList.toggle('aipopen', state.open);
    root.classList.toggle('aionright', state.onRight);
    root.classList.toggle('aionleft', !state.onRight);
  }

  function setOpen(next) {
    if (state.open === next) {
      return;
    }
    state.open = next;
    applyState();
    chrome.send('aiPanelToggled', [state.open]);
    reportRowMark();
  }

  // the sidebar marks the row this panel is attached to, and it
  // cannot see the panel - that is its own route. A pinned thread belongs to
  // no single tab, so it marks none. '' means no row.
  let reportedRowMark = null;

  function reportRowMark() {
    const mark = state.open && !state.pinned && state.attached?.tabId
        ? String(state.attached.tabId)
        : '';
    if (mark === reportedRowMark) {
      return;
    }
    reportedRowMark = mark;
    chrome.send('aiPanelRowMark', [mark]);
  }

  function renderSummary(text, done = true, reasoning = '') {
    const summaryEl = panel.querySelector('.aisum');
    // Its own box, so the thinking fold can sit beside the answer rather than
    // being wiped by the next render of it.
    let body = summaryEl.querySelector('.aisumtext');
    if (!body) {
      summaryEl.replaceChildren();
      body = document.createElement('div');
      body.className = 'aisumtext';
      summaryEl.appendChild(body);
    }
    // the same renderer the chat bubbles use. A summary is a model
    // reply too, and rendering it as flat text showed the markers raw.
    renderMarkdown(body, text);
    applyThinking(summaryEl, reasoning);
    // Only the finished text. Recording every frame would fill the space's
    // memory with prefixes of one answer.
    if (done) {
      recordMemory('summary', text);
    }
  }

  // A string return is the sync stub/test contract. The native backend
  // answers later through `render`, by which point the panel may have
  // re-attached or torn down - `render` re-checks id, panel, and target.
  function summarize() {
    const context = currentContext();
    const requestId = ++nextSummarizeId;
    const target = state.attached;
    panel.querySelector('.aisum').replaceChildren(buildLoader());
    // The pane replaces its whole content either way, so a partial
    // render needs nothing of its own - only the memory does, which must hold
    // the finished text rather than every frame of it.
    let streamed = false;
    // The last thinking seen. The completion push carries only the answer, so
    // without this the fold would empty itself on the final frame.
    let thinking = '';
    const render = (text, done = true, failed = false, reasoning = null) => {
      if (requestId !== nextSummarizeId || !panel ||
          !sameTarget(state.attached, target)) {
        return;
      }
      // A stream that died keeps what it said. Replacing it with the error
      // would blank a half-written summary the user was already reading.
      if (failed && streamed) {
        appendErrorNote(panel.querySelector('.aisum'), text);
        return;
      }
      streamed = streamed || !done;
      if (reasoning !== null) {
        thinking = reasoning;
      }
      renderSummary(text, done, thinking);
    };
    const text = backend.summarize(context, render);
    if (typeof text === 'string') {
      render(text);
    }
  }

  function sendChat() {
    const input = panel.querySelector('.aichatinput');
    const query = input.value.trim();
    if (!query) {
      return;
    }
    input.value = '';
    askModel(query);
  }

  // The tag settings.json holds, or the panel's own locale, which is the
  // macOS preferred language - the core has no way to read that itself.
  function translationLanguage() {
    const tag = state.translateLanguage ||
        (typeof navigator !== 'undefined' && navigator.language) || 'en';
    return languageName(tag);
  }

  // The picker is the panel's copy of the tag, so it follows a settings push
  // as well as its own change event.
  function showLanguage() {
    const select = panel && panel.querySelector('.aiplang');
    if (select) {
      select.value = state.translateLanguage;
      // The options carry codes, so the name only ever reaches the reader here.
      const label = `Summary language: ${languageLabel(state.translateLanguage)}`;
      select.title = label;
      select.setAttribute('aria-label', label);
    }
  }

  // A pinned thread starts over from the page in front of it.
  function resetRoamingTabs() {
    state.roamingTabs = state.attached && state.attached.tabId != null ?
        [state.attached.tabId] :
        [];
  }

  // The stashes are written from this DOM, so emptying it clears
  // them too. The carried page list is not, and a cleared thread that still
  // names them keeps asking about reading the user cannot see any more.
  function clearConversation() {
    cancelPendingReply();
    cancelPendingSummary();
    panel.querySelector('.aisum').replaceChildren();
    panel.querySelector('.aichatlog').replaceChildren();
    if (state.pinned) {
      resetRoamingTabs();
    }
    state.following = true;
  }

  function runQuickAction(id) {
    const action = QUICK_ACTIONS.find((a) => a.id === id);
    if (action) {
      askModel(action.prompt(translationLanguage()));
    }
  }

  function askModel(query) {
    const chatLog = panel.querySelector('.aichatlog');
    appendBubble(chatLog, 'me', query);
    recordMemory('chat', query);
    // A second question before the first answers cancels it the same way a
    // tab switch does - one pending reply, one loader, at a time.
    cancelPendingReply();
    state.pendingLoader = showThinking(panel);
    // a pinned thread asks about the pages it has travelled, not
    // only the one on screen. The current tab is last in roamingTabs, so the
    // native side does not need it named twice.
    const here = currentContext();
    const context = state.pinned
        ? {...here,
           extraTabIds: state.roamingTabs.filter((id) => id !== here.tabId)}
        : here;
    followChatLog();
    const loading = state.pendingLoader;
    // one bubble that grows, not one per push, so a slow model shows
    // words rather than a spinner and a stream that dies keeps what it said.
    let streaming = null;
    let thinking = '';
    state.pendingCancel = backend.chat(
        context, query, (reply, done = true, failed = false, reasoning = null) => {
          // The loader IS the claim on this thread. A backend that answers
          // after its own canceller ran - or after a clear dropped the thread -
          // finds it gone, and its reply belongs to a conversation nobody is in.
          if (!streaming && state.pendingLoader !== loading) {
            return;
          }
          if (failed && streaming) {
            appendErrorNote(chatLog, reply);
            clearThinking();
            state.pendingCancel = null;
            followChatLog();
            return;
          }
          // The native backend sends `reasoning || ''` on every
          // chunk, so taking an empty one as the new reasoning erased the fold
          // the moment the answer arrived.
          if (reasoning) {
            thinking = reasoning;
          }
          if (!streaming) {
            streaming = appendBubble(chatLog, 'ai', reply, thinking);
          } else {
            updateAiBubble(streaming, reply, thinking);
          }
          // Down only when the reply is finished. Text arriving is not the end
          // of the work, and for a reasoning model it is barely the start.
          if (done) {
            clearThinking();
            state.pendingCancel = null;
          }
          followChatLog();
        });
  }

  // Swaps the body between the tab's own thread and the roaming
  // one. Pinning stashes the tab's thread first, so unpinning can hand it
  // back untouched - the pinned thread never overwrites a per-tab one.
  function swapThreadForPin() {
    const sum = panel.querySelector('.aisum');
    const chat = panel.querySelector('.aichatlog');
    if (state.pinned) {
      stashAttached();
      resetRoamingTabs();
      sum.replaceChildren(...(state.roaming ? state.roaming.summary : []));
      chat.replaceChildren(...(state.roaming ? state.roaming.chat : []));
      state.following = true;
      followChatLog();
      return;
    }
    state.roaming = {summary: [...sum.childNodes], chat: [...chat.childNodes]};
    state.roamingTabs = [];
    restoreBodyFor(state.attached);
  }

  function setPinned(next) {
    if (state.pinned === next) {
      return;
    }
    // A reply owed to the thread being swapped out would otherwise land in
    // the one swapped in, which is the same hazard a tab change has.
    cancelPendingReply();
    state.pinned = next;
    const pinButton = panel.querySelector('.aippin');
    pinButton.classList.toggle('on', state.pinned);
    pinButton.setAttribute('aria-pressed', String(state.pinned));
    if (state.pinned) {
      attachToCurrent();
    }
    swapThreadForPin();
    reportRowMark();
  }

  function onPointerMove(e) {
    if (dragStartX === null) {
      return;
    }
    // screenX, not clientX: the panel IS the WebView, so clientX shifts under
    // the pointer on every width push.
    const delta =
        state.onRight ? dragStartX - e.screenX : e.screenX - dragStartX;
    const next = clampWidth(dragStartWidth + delta);
    if (next === state.width) {
      return;
    }
    state.width = next;
    // The container is what the panel is wide, so the edge only follows the
    // pointer if every move reaches the browser - pointerup alone would leave
    // the drag with no live feedback at all.
    chrome.send('aiPanelWidthChanged', [state.width]);
  }

  function onPointerUp() {
    if (dragStartX === null) {
      return;
    }
    dragStartX = null;
    dragStartWidth = null;
    window.removeEventListener('pointermove', onPointerMove);
    window.removeEventListener('pointerup', onPointerUp);
    panel?.querySelector('.aipresize')?.classList.remove('on');
    chrome.send('aiPanelWidthChanged', [state.width]);
  }

  // "Off" must mean no AI-shaped surface, trigger, in-flight reply, or
  // stored chat/summary text survives it.
  function teardownAiSurface() {
    vimSurface?.dispose();
    vimSurface = null;
    if (onKeyDown) {
      document.removeEventListener('keydown', onKeyDown);
      onKeyDown = null;
    }
    if (onRootClick) {
      root.removeEventListener('click', onRootClick);
      onRootClick = null;
    }
    if (dragStartX !== null) {
      window.removeEventListener('pointermove', onPointerMove);
      window.removeEventListener('pointerup', onPointerUp);
      dragStartX = null;
      dragStartWidth = null;
    }
    cancelPendingReply();
    state.open = false;
    panel = null;
    aiMemory.clear();
    perTab.clear();
    // The roaming thread is conversation too, and state.pinned deliberately
    // survives teardown, so leaving it would hand the rebuilt panel the old
    // chat the kill switch was turned on to erase.
    state.roaming = null;
    state.roamingTabs = [];
    root.replaceChildren();
    root.className = 'aipanelroot aipoff';
    root.appendChild(buildOffState());
  }

  function buildAiSurface() {
    // teardownAiSurface leaves the off-state div in root. Without this, an
    // off-to-on push stacks the real panel next to it in the same flex row.
    root.replaceChildren();
    root.className = 'aipanelroot';
    panel = buildPanel();
    root.appendChild(panel);
    // Installed here, not from atrium.js, so the AI kill switch takes the
    // listener with it. A scroller rather than a cursor: the panel
    // is one long answer with no rows to sit on.
    vimSurface = installVimSurface({
      root: panel,
      isEnabled: () => vimOn,
      scroller: () => panel.querySelector('.aichatlog'),
    });
    // state.pinned survives teardown by design - the fresh button
    // must match it or a click reads as toggling from the wrong starting
    // point and inverts the user's intent.
    const pinButton = panel.querySelector('.aippin');
    pinButton.classList.toggle('on', state.pinned);
    pinButton.setAttribute('aria-pressed', String(state.pinned));

    onRootClick = (e) => {
      if (e.target.closest('[data-ai-panel-close]')) {
        setOpen(false);
      } else if (e.target.closest('[data-ai-panel-send]')) {
        sendChat();
      } else if (e.target.closest('[data-ai-panel-pin]')) {
        setPinned(!state.pinned);
      } else if (e.target.closest('[data-ai-panel-clear]')) {
        clearConversation();
      } else if (e.target.closest('[data-ai-panel-quick]')) {
        runQuickAction(
            e.target.closest('[data-ai-panel-quick]').dataset.aiPanelQuick);
      }
    };
    root.addEventListener('click', onRootClick);

    // The reader's own scrolling is the only thing that decides
    // whether the panel follows, so this is read back rather than guessed at
    // from which gesture caused it.
    panel.querySelector('.aichatlog').addEventListener('scroll', (e) => {
      state.following = isAtBottom(e.currentTarget || e.target);
    });

    showLanguage();
    panel.querySelector('.aiplang').addEventListener('change', (e) => {
      state.translateLanguage = e.currentTarget.value;
      // The option text is a code, so the label showLanguage() writes is the
      // only place the name a pick chose still appears.
      showLanguage();
    });

    panel.querySelector('.aichatinput').addEventListener('keydown', (e) => {
      if (e.key === 'Enter') {
        sendChat();
      }
    });

    // the port kept the drag and dropped the reset. Clamped like
    // any other width - the default is wider than the 45% ceiling on a narrow
    // window, and resetting to something the panel cannot hold would leave it
    // reading wider than its own bound.
    panel.querySelector('.aipresize').addEventListener('dblclick', () => {
      const reset = clampWidth(DEFAULT_WIDTH);
      if (reset !== currentWidth()) {
        state.width = reset;
        // Same commit the drag makes on pointerup, so a reset survives a
        // reopen exactly as a dragged width does.
        chrome.send('aiPanelWidthChanged', [state.width]);
      }
    });

    panel.querySelector('.aipresize').addEventListener('pointerdown', (e) => {
      dragStartX = e.screenX;
      dragStartWidth = currentWidth();
      e.currentTarget.classList.add('on');
      window.addEventListener('pointermove', onPointerMove);
      window.addEventListener('pointerup', onPointerUp);
    });

    onKeyDown = (e) => {
      if (e.key === 'Escape' && state.open) {
        setOpen(false);
        return;
      }
      if (e.key === state.hotkey) {
        setOpen(!state.open);
        if (state.open) {
          attachToCurrent();
          if (state.autoSummarize) {
            summarize();
          }
          // Reached only from this hotkey/command-bar path, never from
          // atriumSetAiPanelTarget's tab-switch reopen - an implicit reopen
          // must not steal focus off the page.
          panel.querySelector('.aichatinput').focus();
        }
      }
    };
    document.addEventListener('keydown', onKeyDown);

    applyState();
    refreshContextLine();
    chrome.send('aiPanelReady', []);
  }

  // Native pushes this on every active-tab or active-space change.
  window.atriumSetAiPanelTarget = (target) => {
    state.active = target;
    if (sameTarget(state.attached, target)) {
      // Same tab, but its title or space may have changed since attach
      // - refresh the label without touching the summary/
      // chat body, which still belongs to this same tab.
      state.attached = target;
      refreshContextLine();
      return;
    }
    // A summary owed to the page being left is stale either way. A pending
    // reply is dropped below instead, only where its log is swapped out.
    cancelPendingSummary();
    // Beats the per-tab memory: reopening on a tab that once had the panel
    // puts it in front of a user who asked for neither. A favourite keeps its
    // panel through sameTarget above, which never reaches here.
    const leftTheSpace = state.attached && target &&
        state.attached.spaceIndex !== target.spaceIndex;
    // a pinned panel carries one thread across tabs, so it neither
    // saves the thread on screen into the tab being left nor replaces it with
    // the tab being entered. Every per-tab thread stays as the user left it.
    if (state.pinned) {
      state.attached = target;
      noteRoamingPage(target);
      refreshContextLine();
      appendPageChangeMark(target);
      return;
    }
    cancelPendingReply();
    stashAttached();
    state.attached = target;
    refreshContextLine();
    if (!panel) {
      return;
    }
    const wasOpenHere = restoreBodyFor(target);
    setOpen(!leftTheSpace && wasOpenHere);
    // Not only through setOpen: moving between two tabs that both had the
    // panel open leaves it open, and the mark still has to follow.
    reportRowMark();
  };

  // A reply owed to the tab being left must not land in the next one's log.
  // The strip is not per-tab, so a thread that stops being the one on screen
  // has to take its own indicator down or it reads as the new tab's.
  function clearThinking() {
    if (state.pendingLoader) {
      state.pendingLoader.remove();
      state.pendingLoader = null;
    }
  }

  function cancelPendingReply() {
    if (state.pendingCancel) {
      state.pendingCancel();
      state.pendingCancel = null;
    }
    clearThinking();
  }

  // Tab changes and Clear, never sendChat: a question asked while the summary
  // is still out must leave that summary running, and only a clear says the
  // user is done with it. The id bump drops the answer the old tab is owed.
  function cancelPendingSummary() {
    nextSummarizeId++;
    const loader = panel && panel.querySelector('.aisum .ailoader');
    if (loader) {
      loader.remove();
    }
  }

  window.atriumForgetAiPanelTab = (tabId) => {
    perTab.delete(tabId);
    if (!panel || !state.attached || state.attached.tabId !== tabId) {
      return;
    }
    // The closing tab is still attached when this lands before the next
    // target push. Drop its body now, or that push stashes it under a dead id.
    cancelPendingReply();
    cancelPendingSummary();
    state.attached = null;
    state.active = null;
    restoreBodyFor(null);
    refreshContextLine();
    if (!state.pinned) {
      setOpen(false);
    }
    reportRowMark();
  };

  // Pushed by the native side alongside the sidebar's own onRight setter:
  // this panel lives on the edge opposite the sidebar, so it flips whenever
  // the sidebar's own position setting changes.
  window.atriumSetAiPanelOnRight = (onRight) => {
    state.onRight = !!onRight;
    applyState();
  };

  // The extension side panel shares this panel's slot. Native closes this one
  // through here, so the page state and the native width stay in step.
  window.atriumCloseAiPanel = () => {
    setOpen(false);
  };

  // Remappable: the browser owns hotkey storage/settings, and pushes
  // whatever chord the user has configured for this action.
  window.atriumSetAiPanelHotkey = (key) => {
    state.hotkey = key || '`';
  };

  // Pushed on settings save and on the ready catch-up. Only the NEXT open
  // reads it - a panel already open keeps whatever it started with.
  window.atriumSetAiPanelAutoSummarize = (enabled) => {
    state.autoSummarize = enabled !== false;
  };

  // Empty means follow the panel's own locale, which is what a fresh profile
  // stores. Read per request, so a settings change needs no reopen.
  window.atriumSetAiPanelTranslateLanguage = (tag) => {
    state.translateLanguage = tag || '';
    showLanguage();
  };

  // No native caller yet (same gap as the entrypoints above).
  // Off tears every AI surface down to nothing, on rebuilds it fresh.
  window.atriumSetVimModeEnabled = (on) => {
    vimOn = !!on;
  };

  window.atriumSetAiPanelEnabled = (enabled) => {
    const next = !!enabled;
    if (state.enabled === next) {
      return;
    }
    state.enabled = next;
    if (next) {
      buildAiSurface();
    } else {
      teardownAiSurface();
    }
  };

  if (state.enabled) {
    buildAiSurface();
  } else {
    teardownAiSurface();
  }

  // No Settings > AI memory list ever got built, so this stays internal
  // rather than on window - tests reach it through the return value.
  return {getMemory: getAiMemory};
}
