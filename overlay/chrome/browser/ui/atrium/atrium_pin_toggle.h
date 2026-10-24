// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_pin_toggle.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_PIN_TOGGLE_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_PIN_TOGGLE_H_

#include <cstddef>
#include <string>
#include <vector>

namespace atrium {

// One sidebar row as Cmd+D sees it. `tab_id` is empty for a COLD row, one the
// space keeps with no live tab behind it.
struct PinToggleRow {
  std::string tab_id;
  bool selected = false;
  // Set on a pinned folder, which is one pinned item holding `members`.
  std::string folder_id;
  std::vector<PinToggleRow> members;
};

enum class PinToggleKind {
  kNothing,
  kPin,
  kUnpin,
  kUnpinMember,
};

// What Cmd+D does. `pinned_index` is set for kUnpin, `tab_id` for kPin, and
// `folder_id` with `member_index` for kUnpinMember.
struct PinToggleAction {
  PinToggleKind kind = PinToggleKind::kNothing;
  size_t pinned_index = 0;
  std::string tab_id;
  std::string folder_id;
  size_t member_index = 0;
};

// Which way Cmd+D moves the row the window is showing. The sidebar model's
// pinned section is unfiltered, so `pinned_index` is the index the core's own
// pinned list uses. `regular` includes folder members, which pin too.
PinToggleAction DecidePinToggle(const std::vector<PinToggleRow>& pinned,
                                const std::vector<PinToggleRow>& regular);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_PIN_TOGGLE_H_
