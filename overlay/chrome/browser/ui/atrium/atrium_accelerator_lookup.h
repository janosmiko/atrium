// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_accelerator_lookup.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_ACCELERATOR_LOOKUP_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_ACCELERATOR_LOOKUP_H_

namespace ui {
class Accelerator;
}

namespace atrium {

// Writes the platform's accelerator for `command_id` into `out` and returns
// true, or returns false when the platform binds none.
//
// this exists because the macOS table is reached through a
// header that imports Cocoa. The API itself is plain C++, so one wrapper here
// keeps every caller out of Objective-C++.
bool AcceleratorForCommand(int command_id, ui::Accelerator* out);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_ACCELERATOR_LOOKUP_H_
