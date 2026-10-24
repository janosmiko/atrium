// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_sidebar_overview_width.h"

#include <algorithm>

namespace atrium {

int EffectiveSidebarWidth(bool overview_open, int user_width, int overview_min) {
  return overview_open ? std::max(user_width, overview_min) : user_width;
}

}  // namespace atrium
