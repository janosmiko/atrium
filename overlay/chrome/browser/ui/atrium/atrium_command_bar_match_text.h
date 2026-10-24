// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_command_bar_match_text.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_COMMAND_BAR_MATCH_TEXT_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_COMMAND_BAR_MATCH_TEXT_H_

#include <optional>
#include <string_view>

#include "base/containers/span.h"

namespace atrium {

// How well `query` describes a candidate whose text is `fields` - a title, a
// host, an alias. Higher is a better row, nothing at all is not a hit.

// EVERY token must land, so "gh iss" needs both halves. A token lands on a
// word when it is that word's prefix, or when its characters appear in order
// in the word AND share its first character.

// Query and field break on the same characters, whitespace and punctuation, so
// a typed host arrives as the words it has to land on. A query holding no
// token at all scores zero, which every candidate ties on.

// That first-character rule is the whole brake on the fuzzy half: without it
// "xyz" hits everything.

// Prefix beats fuzzy, an earlier word beats a later one, and nothing else is
// ranked - the caller sorts stably, so equal scores keep the order they
// arrived in (recency for tabs, sidebar order for favourites).

// ASCII case folding only, the limit the substring test before it had.
// command-match.js is this matcher for rows the page filters itself.
std::optional<int> CommandBarMatchScore(
    std::string_view query,
    base::span<const std::string_view> fields);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_COMMAND_BAR_MATCH_TEXT_H_
