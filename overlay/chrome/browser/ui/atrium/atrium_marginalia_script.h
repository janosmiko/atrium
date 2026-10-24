// Copyright 2026 The Atrium Authors

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_MARGINALIA_SCRIPT_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_MARGINALIA_SCRIPT_H_

#include <string>

namespace atrium {

// Run via ExecuteJavaScriptInIsolatedWorld - the shared JS
// library re-sends on every call but only installs its click listener once.
std::u16string BuildWrapSelectionScript(const std::string& mark_id);

// The naive exact-substring re-anchor, for each {id, quote} pair
// in `marginalia_json` not already marked.
std::u16string BuildReanchorScript(const std::string& marginalia_json);

std::u16string BuildDrainClicksScript();

std::u16string BuildScrollToMarkScript(const std::string& mark_id);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_MARGINALIA_SCRIPT_H_
