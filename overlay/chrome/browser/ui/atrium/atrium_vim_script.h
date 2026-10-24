// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_VIM_SCRIPT_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_VIM_SCRIPT_H_

#include <string>

namespace atrium {

// Unlike atrium_marginalia_script.cc, the library is a resource
// rather than a raw string literal: the vim grammar is big enough to deserve
// its own node suite, and a string inside a .cc cannot have one.
std::u16string BuildVimInstallScript();

// An extension popup gets the same two files plus a bridge that
// reads its own keys: stock ExtensionViewHost delegates that WebContents, so
// the browser never sees a key pressed there to forward.
std::u16string BuildVimPopupInstallScript();

// Runs one verb. Evaluates to false when the page is in INSERT mode and the
// browser's cached mode was stale, which is the veto the window re-dispatches
// on.
std::u16string BuildVimKeyScript(const std::string& character);

std::u16string BuildVimModeDrainScript();

// How many hint targets this frame has. Phase one of the allocation pass.
std::u16string BuildVimTargetCountScript();

// Phase two. `total` is every frame's count, because the label width has to
// match across frames or one frame's 'a' is another frame's prefix.
std::u16string BuildVimPaintHintsScript(int offset, int total);

// Takes the mode indicator off a page the toggle no longer applies to. The
// script itself stays, so switching back on needs no reload.
std::u16string BuildVimTeardownScript();

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_VIM_SCRIPT_H_
