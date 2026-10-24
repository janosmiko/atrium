// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_command_bar_match_text.cc
#include "chrome/browser/ui/atrium/atrium_command_bar_match_text.h"

#include <algorithm>
#include <array>
#include <string>

#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

namespace atrium {
namespace {

constexpr int kPrefixScore = 1000;
constexpr int kSubsequenceScore = 500;

// A floor under the position penalty: a hit late in a long title still has to
// outrank a miss, and a miss is not a score at all.
constexpr size_t kMaxPositionPenalty = 100;

constexpr size_t CountWordBreakChars() {
  size_t count = 0;
  for (int c = 0; c < 0x80; ++c) {
    count += !base::IsAsciiAlphaNumeric(static_cast<char>(c));
  }
  return count;
}

constexpr std::array<char, CountWordBreakChars()> MakeWordBreakChars() {
  std::array<char, CountWordBreakChars()> chars{};
  size_t at = 0;
  for (int c = 0; c < 0x80; ++c) {
    if (!base::IsAsciiAlphaNumeric(static_cast<char>(c))) {
      chars[at++] = static_cast<char>(c);
    }
  }
  return chars;
}

// Every ASCII byte that is not alphanumeric. A non-ASCII byte stays inside its
// word: a title in another script would otherwise break into one word per
// byte.
constexpr std::array<char, CountWordBreakChars()> kWordBreakCharsArray =
    MakeWordBreakChars();
constexpr std::string_view kWordBreakChars(kWordBreakCharsArray.data(),
                                            kWordBreakCharsArray.size());

// `token` is already lowercased, `word` is not.
std::optional<int> ScoreTokenInWord(std::string_view token,
                                    std::string_view word) {
  if (base::ToLowerASCII(word.front()) != token.front()) {
    return std::nullopt;
  }
  if (word.size() >= token.size()) {
    bool prefix = true;
    for (size_t at = 1; at < token.size() && prefix; ++at) {
      prefix = base::ToLowerASCII(word[at]) == token[at];
    }
    if (prefix) {
      return kPrefixScore;
    }
  }
  size_t need = 1;
  for (size_t at = 1; at < word.size() && need < token.size(); ++at) {
    if (base::ToLowerASCII(word[at]) == token[need]) {
      ++need;
    }
  }
  return need == token.size() ? std::optional<int>(kSubsequenceScore)
                             : std::nullopt;
}

std::optional<int> ScoreTokenInField(std::string_view token,
                                     std::string_view field) {
  std::optional<int> best;
  for (std::string_view word : base::SplitStringPiece(
           field, kWordBreakChars, base::TRIM_WHITESPACE,
           base::SPLIT_WANT_NONEMPTY)) {
    const std::optional<int> found = ScoreTokenInWord(token, word);
    if (!found) {
      continue;
    }
    const size_t start = static_cast<size_t>(word.data() - field.data());
    const int scored =
        *found - static_cast<int>(std::min(start, kMaxPositionPenalty));
    if (!best || scored > *best) {
      best = scored;
    }
  }
  return best;
}

}  // namespace

std::optional<int> CommandBarMatchScore(
    std::string_view query,
    base::span<const std::string_view> fields) {
  int total = 0;
  // The query breaks on the same characters the fields do, so a typed host
  // arrives as the words it would have to land on rather than as one token
  // carrying dots that no word contains.
  for (std::string_view raw_token : base::SplitStringPiece(
           query, kWordBreakChars, base::TRIM_WHITESPACE,
           base::SPLIT_WANT_NONEMPTY)) {
    const std::string token = base::ToLowerASCII(raw_token);
    std::optional<int> best;
    for (std::string_view field : fields) {
      const std::optional<int> scored = ScoreTokenInField(token, field);
      if (scored && (!best || *scored > *best)) {
        best = scored;
      }
    }
    // One unmatched token disqualifies the candidate: that is what makes a
    // second word narrow the list rather than widen it.
    if (!best) {
      return std::nullopt;
    }
    total += *best;
  }
  return total;
}

}  // namespace atrium
