// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_new_tab_modifiers.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_NEW_TAB_MODIFIERS_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_NEW_TAB_MODIFIERS_H_

#include <optional>

namespace atrium {

// Whether Cmd and Shift are both down right now. A Cmd+Shift+click and a
// noopener link both reach Browser::OpenURLFromTab as NEW_FOREGROUND_TAB with
// nothing in the params to tell them apart, and the click's modifiers are
// still held when its IPC lands.
bool NewTabModifiersHeld();

// Overrides the hardware read. std::nullopt restores it.
void SetNewTabModifiersHeldForTesting(std::optional<bool> held);

// Whether Option is down and Shift and Cmd are not. A real Shift+click and
// Atrium's Option-only peek chord both reach Browser::OpenURLFromTab as
// NEW_WINDOW, with nothing in the params to tell them apart.
bool OptionOnlyModifierHeld();

// Overrides the hardware read. std::nullopt restores it.
void SetOptionOnlyModifierHeldForTesting(std::optional<bool> held);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_NEW_TAB_MODIFIERS_H_
