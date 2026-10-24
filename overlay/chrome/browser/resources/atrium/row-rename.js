// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//
// renaming a row. The model has always carried custom_title and
// the sidebar has always read it, so this field is the only writer. This is the lab's inline field
// (swipe-lab.html:4309-4321) on this side.

// Replaces the row's title with an input, and commits on blur or Enter.
// `commit(name)` is called with the trimmed value, or with '' when the field is
// emptied - which clears the rename rather than storing a blank, so a row can
// fall back to the page's own title.
export function startRowRename(row, commit) {
  const title = row?.querySelector('.tl');
  if (!title || title.querySelector('input')) {
    return null;
  }
  const before = title.textContent;
  const input = document.createElement('input');
  input.className = 'tlrename';
  input.type = 'text';
  input.value = before;
  title.textContent = '';
  title.appendChild(input);
  input.focus?.();
  input.select?.();

  let done = false;
  const finish = (name) => {
    if (done) {
      return;
    }
    done = true;
    title.textContent = name || before;
    commit(name);
  };

  input.addEventListener('keydown', (e) => {
    // Stopped here or the sidebar's own hotkeys fire while someone is typing a
    // name - the same reason the lab stops it.
    e.stopPropagation();
    if (e.key === 'Enter') {
      finish(input.value.trim());
    } else if (e.key === 'Escape') {
      // Escape abandons: the row keeps whatever it had, and nothing is sent.
      done = true;
      title.textContent = before;
    }
  });
  // A click inside the field must not activate the row underneath it.
  input.addEventListener('mousedown', (e) => e.stopPropagation());
  input.addEventListener('click', (e) => e.stopPropagation());
  input.addEventListener('blur', () => finish(input.value.trim()), {once: true});
  return input;
}
