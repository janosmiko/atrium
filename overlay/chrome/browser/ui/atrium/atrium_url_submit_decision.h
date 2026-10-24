// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_url_submit_decision.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_URL_SUBMIT_DECISION_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_URL_SUBMIT_DECISION_H_

class GURL;

namespace atrium {

enum class SubmitTarget {
  kSameTab,
  kNewTab,
};

// Where an address-bar submission lands. Navigating a favourite's tab off its
// site leaves the tile bound to a page it does not name, so that submission
// gets a tab of its own. Same site is the same eTLD+1 with private registries
// counted, the boundary AtriumPeekIntercept and LogSafeUrl already draw.
SubmitTarget DecideUrlSubmitTarget(bool active_is_favourite,
                                   const GURL& favourite_url,
                                   const GURL& typed);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_URL_SUBMIT_DECISION_H_
