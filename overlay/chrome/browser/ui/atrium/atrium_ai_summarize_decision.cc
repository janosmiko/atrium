// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_ai_summarize_decision.h"

#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"

namespace atrium {

SummarizeOutcome DecideSummarizeAction(bool ai_enabled,
                                        bool model_available,
                                        const std::string& page_text) {
  if (!ai_enabled) {
    return SummarizeOutcome::kDisabled;
  }
  if (!model_available) {
    return SummarizeOutcome::kUnavailable;
  }
  std::u16string trimmed;
  base::TrimWhitespace(base::UTF8ToUTF16(page_text), base::TRIM_ALL,
                        &trimmed);
  if (trimmed.empty()) {
    return SummarizeOutcome::kEmptyPage;
  }
  return SummarizeOutcome::kCallModel;
}

std::string ClampPageTextForModel(const std::string& text, size_t cap) {
  const size_t limit = cap > 0 ? cap : kMaxSummarizeInputChars;
  std::u16string wide = base::UTF8ToUTF16(text);
  if (wide.size() <= limit) {
    return text;
  }
  wide.resize(limit);
  // Carried in the string rather than threaded through the five signatures
  // between here and the prompt, which also keeps the on-device Swift path
  // from being the one route that loses it. BuildChatMessages strips it.
  return base::UTF16ToUTF8(wide) + std::string(kTruncatedMarker);
}

bool StripTruncationMarker(std::string& text) {
  const size_t at = text.find(kTruncatedMarker);
  if (at == std::string::npos) {
    return false;
  }
  text.erase(at);
  return true;
}

}  // namespace atrium
