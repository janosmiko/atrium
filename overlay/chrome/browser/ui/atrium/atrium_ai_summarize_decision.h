// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_AI_SUMMARIZE_DECISION_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_AI_SUMMARIZE_DECISION_H_

#include <string>
#include <string_view>

namespace atrium {

// The fallback when the settings read found no aiPageTextCap. Must match the
// core's DEFAULT_AI_PAGE_TEXT_CAP (settings.rs) - the user's row overrides it.
inline constexpr size_t kMaxSummarizeInputChars = 100000;

enum class SummarizeOutcome {
  kDisabled,     // Kill switch is off - never call the model.
  kUnavailable,  // Foundation Models isn't available on this Mac.
  kEmptyPage,    // Page text (after trimming) is empty.
  kCallModel,    // Safe to hand `page_text` to the model.
};

// `page_text` only decides kEmptyPage vs kCallModel once ai_enabled and
// model_available are both true.
SummarizeOutcome DecideSummarizeAction(bool ai_enabled,
                                        bool model_available,
                                        const std::string& page_text);

// appended by the clamp when it really cut, and stripped by
// BuildChatMessages, which turns it into an instruction. Unterminated page
// text read as the whole page is how the model stated an absence as fact.
inline constexpr std::string_view kTruncatedMarker = "\n\x01atrium-truncated";

// Truncates `text` to at most `cap` UTF-16 code units, appending
// kTruncatedMarker when it cut. A zero `cap` means the caller read no setting.
std::string ClampPageTextForModel(const std::string& text, size_t cap);

// Removes kTruncatedMarker from `text` and says whether it was there. The
// question travels through the same clamp as the page, and the marker on it
// would reach the model as page content.
bool StripTruncationMarker(std::string& text);

// one less than the panel's MAX_ROAMING_PAGES, which counts the
// page on screen. The joined text takes a single page's budget, so a long
// pinned session cannot grow the prompt without limit.
inline constexpr size_t kMaxAiPanelExtraPages = 4;

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_AI_SUMMARIZE_DECISION_H_
