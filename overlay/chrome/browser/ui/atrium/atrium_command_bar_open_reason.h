// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_command_bar_open_reason.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_COMMAND_BAR_OPEN_REASON_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_COMMAND_BAR_OPEN_REASON_H_

#include <cstdint>

namespace atrium {

// why the palette is being opened, which is what decides its
// opening state on the Swift side (empty query vs the current tab's URL
// prefilled and selected). Crosses the ABI as an int32_t via
// AtriumOpenCommandBarWithReason - see the retired ABI header's COMMAND BAR OPEN
// REASON doc block. The numeric values are wire values: APPEND ONLY, never
// renumber.
//
// Its own header (the atrium_link_copy_format.h precedent) so both
// atrium_browser_controller.h and the retired ABI loader can name the type
// without either including the other.
enum class CommandBarOpenReason : int32_t {
  // Today's Cmd+T: empty query, suggestions shown. The default everywhere,
  // so every older open path keeps its exact behavior.
  kNewTab = 0,
  // Cmd+L: prefill the active tab's URL, fully selected, so typing replaces
  // it; Escape leaves the tab untouched.
  kEditCurrentURL = 1,
  // A click on an address row: the same prefill as kEditCurrentURL, plus an
  // anchor rect the widget turns into the card's placement.
  kEditAtAnchor = 2,
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_COMMAND_BAR_OPEN_REASON_H_
