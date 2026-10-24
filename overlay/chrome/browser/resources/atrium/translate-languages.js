// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

// Hand-picked rather than every tag Intl knows: a 200-entry select is not a
// setting. Empty is the system language, which is what a fresh profile holds.
export const TRANSLATE_LANGUAGES = [
  '', 'en', 'es', 'fr', 'de', 'it', 'pt', 'nl', 'pl', 'hu', 'ru', 'tr', 'ja',
  'ko', 'zh',
];

export function languageName(tag) {
  try {
    return new Intl.DisplayNames(['en'], {type: 'language'}).of(tag) || tag;
  } catch {
    return tag;
  }
}

/** The name a picker shows for a tag, with empty read as the system one. */
export function languageLabel(tag) {
  return tag ? languageName(tag) :
               `System (${languageName(navigator.language)})`;
}

/** The code a picker too narrow for a name shows. */
export function languageCode(tag) {
  return String(tag || navigator.language || 'en').toUpperCase();
}
