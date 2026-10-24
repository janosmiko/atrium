// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_little_marker.h"

namespace atrium {

bool& CreatingLittleBrowser() {
  static bool creating = false;
  return creating;
}

}  // namespace atrium
