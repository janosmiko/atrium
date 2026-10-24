// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_hex_color.h"

#include <string_view>

#include "base/strings/string_number_conversions.h"

namespace atrium {

std::optional<SkColor> ParseHexColor(const std::string& hex) {
  if (hex.size() != 7 || hex[0] != '#') {
    return std::nullopt;
  }
  uint32_t rgb = 0;
  if (!base::HexStringToUInt(std::string_view(hex).substr(1), &rgb)) {
    return std::nullopt;
  }
  return SkColorSetA(static_cast<SkColor>(rgb), SK_AlphaOPAQUE);
}

}  // namespace atrium
