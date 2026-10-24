// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SEARCH_ENGINE_MIRROR_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SEARCH_ENGINE_MIRROR_H_

#include <optional>
#include <string>
#include <vector>

class TemplateURLService;

namespace atrium {

struct MirroredEngine {
  std::string id;
  std::string name;
  std::string keyword;
  std::string url;
  bool enabled = true;
};

// The usable `searchEngines` entries: TemplateURLData's SetKeyword and SetURL
// DCHECK on empty input, so the rest are dropped here. nullopt is no answer
// (unparsable JSON, no list), which the mirror must not read as an empty list.
std::optional<std::vector<MirroredEngine>> SearchEnginesFromSettings(
    const std::string& settings_json);

// Makes the service's `atrium:<id>` entries equal the enabled engines: adds
// missing ones, updates changed ones, removes disabled or deleted ones. Every
// other TemplateURL is left alone.
void MirrorSearchEngines(TemplateURLService* service,
                         const std::vector<MirroredEngine>& engines);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SEARCH_ENGINE_MIRROR_H_
