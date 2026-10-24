// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_spaces_order.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SPACES_ORDER_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SPACES_ORDER_H_

#include <string>

namespace atrium {

// The spaces document with its `spaces` list in the order the sidebar draws
// them, which is the `order` field rather than the stored array position.
//
// The settings panes number the spaces they draw, and every index they send
// back reaches the core as a sidebar position, so the list they draw has to be
// in that order to begin with. Returned unchanged when the document cannot be
// read: the pane drawing a mis-sorted list beats it drawing nothing.
std::string SpacesInSidebarOrder(const std::string& spaces_json);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SPACES_ORDER_H_
