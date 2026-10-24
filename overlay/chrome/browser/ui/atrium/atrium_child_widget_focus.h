// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_child_widget_focus.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_CHILD_WIDGET_FOCUS_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_CHILD_WIDGET_FOCUS_H_

namespace views {
class Widget;
}

namespace atrium {

// Call before hiding a surface widget. Its first responder re-announces focus
// to its WebView on every AppKit change, and that takes the widget's focus
// manager straight back from whatever the close hands focus to.
void ReleaseChildWidgetFocus(views::Widget* widget);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_CHILD_WIDGET_FOCUS_H_
