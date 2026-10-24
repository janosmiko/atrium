// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_corner_radius.h"

#include <algorithm>

namespace atrium {

float ConcentricCornerRadius(float outer, int inset) {
  const float gap = static_cast<float>(std::max(0, inset));
  return std::max(kMinNestedCornerRadius, outer - gap);
}

}  // namespace atrium
