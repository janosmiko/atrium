// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_HEX_COLOR_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_HEX_COLOR_H_

#include <optional>
#include <string>

#include "third_party/skia/include/core/SkColor.h"

namespace atrium {

// Parses the "#RRGGBB" shape every colour the Rust core emits carries.
// nullopt on anything else, so a caller leaves the surface where it was.
std::optional<SkColor> ParseHexColor(const std::string& hex);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_HEX_COLOR_H_
