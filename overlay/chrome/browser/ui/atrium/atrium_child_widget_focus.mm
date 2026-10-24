// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_child_widget_focus.mm
#include "chrome/browser/ui/atrium/atrium_child_widget_focus.h"

#import <AppKit/AppKit.h>

#include "ui/views/focus/focus_manager.h"
#include "ui/views/widget/widget.h"

namespace atrium {

void ReleaseChildWidgetFocus(views::Widget* widget) {
  if (NSWindow* window = widget->GetNativeWindow().GetNativeNSWindow()) {
    [window makeFirstResponder:nil];
  }
  if (views::FocusManager* focus = widget->GetFocusManager()) {
    focus->ClearFocus();
    focus->SetStoredFocusView(nullptr);
  }
}

}  // namespace atrium
