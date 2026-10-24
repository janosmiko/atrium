// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_little_window_registry.h"

#include <algorithm>
#include <vector>

#include "base/no_destructor.h"

namespace atrium {

namespace {

std::vector<const BrowserWindowInterface*>& LittleWindows() {
  static base::NoDestructor<std::vector<const BrowserWindowInterface*>>
      windows;
  return *windows;
}

}  // namespace

void RegisterLittleWindow(BrowserWindowInterface* browser) {
  LittleWindows().push_back(browser);
}

void UnregisterLittleWindow(BrowserWindowInterface* browser) {
  std::erase(LittleWindows(), browser);
}

bool IsLittleWindow(const BrowserWindowInterface* browser) {
  const auto& windows = LittleWindows();
  return std::find(windows.begin(), windows.end(), browser) != windows.end();
}

}  // namespace atrium
