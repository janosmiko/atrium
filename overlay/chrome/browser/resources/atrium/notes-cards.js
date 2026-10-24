// Copyright 2026 The Atrium Authors
//
// Marginalia comment cards. No innerHTML: a quote is text
// lifted off an arbitrary web page, the most attacker-controlled string this
// surface handles.

export const FLASH_CLASS = 'mflash';

export function buildCommentCard(doc, mark) {
  const card = doc.createElement('div');
  card.className = 'mcard';
  card.dataset.markId = mark.id;

  const quote = doc.createElement('blockquote');
  quote.className = 'mquote';
  quote.textContent = mark.quote ?? '';

  const note = doc.createElement('div');
  note.className = 'mnote';
  note.contentEditable = 'true';
  note.spellcheck = false;
  note.dataset.markId = mark.id;
  note.textContent = mark.note ?? '';

  card.appendChild(quote);
  card.appendChild(note);
  return card;
}

export function renderCommentCards(host, marginalia) {
  while (host.firstChild) {
    host.removeChild(host.firstChild);
  }
  for (const mark of marginalia ?? []) {
    host.appendChild(buildCommentCard(host.ownerDocument ?? document, mark));
  }
}

// The pad half. Returns false when the id names no card, which is what a
// click on a mark whose card was deleted has to survive.
export function flashCard(host, id) {
  const card = host.querySelector(`.mcard[data-mark-id="${CSS.escape(id)}"]`);
  if (!card) {
    return false;
  }
  card.scrollIntoView();
  card.classList.remove(FLASH_CLASS);
  card.classList.add(FLASH_CLASS);
  return true;
}
