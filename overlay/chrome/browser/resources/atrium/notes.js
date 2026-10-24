// Copyright 2026 The Atrium Authors
//
// The Notes route: a per-space markdown pad, opened in a
// split beside the page (see AtriumBrowserWindow::OpenNotesBesideThisPage).

import {
  blockTypeForMarker,
  enterAction,
  inlineMatchBeforeCaret,
  renderMarkdownIntoPad,
  serializeToMarkdown,
} from './notes-pad.js';
import {flashCard, renderCommentCards} from './notes-cards.js';

let padEl = null;
let titleEl = null;
let cardsEl = null;
let saveTimer = null;
const noteTimers = new Map();

function setNote(note) {
  titleEl.textContent = note?.title || 'Notes';
  renderMarkdownIntoPad(padEl, note?.body ?? '');
  renderCommentCards(cardsEl, note?.marginalia);
}

// The block currently holding the caret, or null outside the pad.
function currentBlock(node) {
  let el = node.nodeType === 3 ? node.parentElement : node;
  while (el && el.parentElement !== padEl && el !== padEl) {
    el = el.parentElement;
  }
  return el === padEl ? null : el;
}

function scheduleSave() {
  clearTimeout(saveTimer);
  saveTimer = setTimeout(() => {
    chrome.send('notesBodyChanged', [serializeToMarkdown(padEl)]);
  }, 300);
}

function handleInput() {
  scheduleSave();
}

// Marker + space at the very start of an otherwise-empty block - the block
// shortcut path (# , - , 1. , > ...).
function tryBlockShortcut(event) {
  const sel = getSelection();
  if (!sel.rangeCount || event.key !== ' ') {
    return false;
  }
  const range = sel.getRangeAt(0);
  if (!range.collapsed || range.startContainer.nodeType !== 3) {
    return false;
  }
  const node = range.startContainer;
  const before = node.textContent.slice(0, range.startOffset);
  const blockType = blockTypeForMarker(before);
  if (!blockType || node.textContent.slice(range.startOffset).length > 0) {
    return false;
  }
  const block = currentBlock(node);
  if (!block || block.tagName.toLowerCase() !== 'p') {
    return false;
  }
  event.preventDefault();
  const tag = blockType === 'ul' || blockType === 'ol' ? 'li' : blockType;
  const replacement = document.createElement(tag);
  block.replaceWith(replacement);
  if (blockType === 'ul' || blockType === 'ol') {
    const wrap = document.createElement(blockType);
    replacement.replaceWith(wrap);
    wrap.appendChild(replacement);
  }
  const caretRange = document.createRange();
  caretRange.selectNodeContents(replacement);
  caretRange.collapse(true);
  sel.removeAllRanges();
  sel.addRange(caretRange);
  scheduleSave();
  return true;
}

// The closing marker of an inline span (**, *, _, `) just completed.
function tryInlineShortcut() {
  const sel = getSelection();
  if (!sel.rangeCount) {
    return;
  }
  const range = sel.getRangeAt(0);
  if (!range.collapsed || range.startContainer.nodeType !== 3) {
    return;
  }
  const node = range.startContainer;
  const before = node.textContent.slice(0, range.startOffset);
  const match = inlineMatchBeforeCaret(before);
  if (!match) {
    return;
  }
  const start = range.startOffset - match.length;
  const spanRange = document.createRange();
  spanRange.setStart(node, start);
  spanRange.setEnd(node, range.startOffset);
  const el = document.createElement(match.tag);
  el.textContent = match.inner;
  spanRange.deleteContents();
  spanRange.insertNode(el);
  const after = document.createRange();
  after.setStartAfter(el);
  after.collapse(true);
  sel.removeAllRanges();
  sel.addRange(after);
}

function handleKeydown(event) {
  if (tryBlockShortcut(event)) {
    return;
  }
  if (event.key === 'Enter') {
    const sel = getSelection();
    if (!sel.rangeCount) {
      return;
    }
    const range = sel.getRangeAt(0);
    const block = currentBlock(range.startContainer);
    if (!block) {
      return;
    }
    const isListItem = block.tagName.toLowerCase() === 'li';
    const type = isListItem ? block.parentElement.tagName.toLowerCase() : block.tagName.toLowerCase();
    const action = enterAction(type, block.textContent, range.startOffset);
    if (action === 'exit') {
      event.preventDefault();
      const host = isListItem ? block.parentElement : block;
      const fresh = document.createElement('p');
      host.after(fresh);
      if (isListItem) {
        block.remove();
        if (host.children.length === 0) {
          host.remove();
        }
      } else {
        block.remove();
      }
      const caretRange = document.createRange();
      caretRange.selectNodeContents(fresh);
      caretRange.collapse(true);
      sel.removeAllRanges();
      sel.addRange(caretRange);
      scheduleSave();
    }
    return;
  }
}

// Debounced per card, matching the pad's own 300 ms save, so a card being
// typed into does not write on every keystroke.
function scheduleNoteSave(id, text) {
  clearTimeout(noteTimers.get(id));
  noteTimers.set(id, setTimeout(() => {
    noteTimers.delete(id);
    chrome.send('marginaliaNoteChanged', [id, text]);
  }, 300));
}

function handleCardInput(event) {
  const field = event.target?.closest?.('.mnote');
  if (!field) {
    return;
  }
  scheduleNoteSave(field.dataset.markId, field.textContent);
}

// Card -> mark: clicking the immutable quote jumps to its mark on the
// page. The note field itself isn't included - a click there is placing the
// caret to edit, not navigating.
function handleCardClick(event) {
  const quote = event.target?.closest?.('.mquote');
  if (!quote) {
    return;
  }
  const card = quote.closest('.mcard');
  if (card) {
    chrome.send('marginalMarkJumpRequested', [card.dataset.markId]);
  }
}

export function installNotes(root) {
  root.classList.add('notesroot');
  titleEl = document.createElement('div');
  titleEl.className = 'notes-title';
  padEl = document.createElement('div');
  padEl.className = 'notes-pad';
  padEl.contentEditable = 'true';
  padEl.spellcheck = false;
  cardsEl = document.createElement('div');
  cardsEl.className = 'notes-cards';
  root.appendChild(titleEl);
  root.appendChild(padEl);
  root.appendChild(cardsEl);

  window.atriumSetNote = setNote;
  window.atriumFlashMarginal = (id) => flashCard(cardsEl, id);

  cardsEl.addEventListener('input', handleCardInput);
  cardsEl.addEventListener('click', handleCardClick);

  padEl.addEventListener('input', handleInput);
  padEl.addEventListener('keydown', handleKeydown);
  // Inline shortcuts fire on the character AFTER the closing marker lands,
  // which 'input' sees and 'keydown' does not (the DOM text isn't updated
  // yet at keydown time).
  padEl.addEventListener('input', tryInlineShortcut);

  renderMarkdownIntoPad(padEl, '');
  chrome.send('notesReady', []);
}
