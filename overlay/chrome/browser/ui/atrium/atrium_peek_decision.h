// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_peek_decision.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_PEEK_DECISION_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_PEEK_DECISION_H_

namespace atrium {

// the pure decision behind auto-peek, extracted from
// ShouldAutoPeek (atrium_peek_intercept.cc) so it can be asserted rather than
// read. Same posture as the core's navigation.rs - bools in, bool
// out, no content:: or Browser dependency - shared by
// //chrome/browser/ui:ui, which resolves these from a live WebContents and
// BrowserController, and :unit_tests via a diamond.
//
// The behaviour was already shipped and had no test
// over it at all, which is the actual reason this exists: the Keyhole's whole
// promise is that a pinned page never navigates away, and nothing was holding
// anyone to it.
struct PeekDecisionInputs {
  // The source tab has a valid last-committed URL. Without one there is no
  // origin for the target to be off-site FROM.
  bool source_has_committed_url = false;
  // The target URL is http or https. mailto:, javascript: and chrome:// are
  // not pages to put in a card.
  bool is_http_or_https = false;
  // Target and source share an eTLD+1.
  bool is_same_domain_or_host = false;
  // The navigating Browser is one Atrium manages. False for the peek's own
  // contents and for DevTools, which is what stops a peek opening inside a
  // peek.
  bool has_atrium_controller = false;
  // Swift's answer to "is this tab pinned or favorited" (AtriumIsPeekSourceTab).
  // A dylib without that export leaves this false, which makes the auto path
  // inert rather than wrong.
  bool is_peek_source_tab = false;
};

// True when an off-site link click should open the Keyhole instead of
// navigating the source tab. Every field is independently necessary; there is
// a test asserting exactly that, because a decision table is the kind of thing
// that keeps passing after it stops meaning anything.
//
// Same-site returns false ON PURPOSE: a pinned Gmail or Notion has to stay
// usable as an app, so internal links navigate in place. That is a product choice, not an oversight.
bool ShouldAutoPeekForInputs(const PeekDecisionInputs& inputs);

// The pure decision behind showing a window.open popup as a peek, same posture
// as the struct above: bools in, bool out, no content:: or Browser dependency.
struct PopupPeekDecisionInputs {
  // The opener's Browser is one Atrium manages. A popup opened from another
  // popup, an app window or DevTools has no card to adopt into.
  bool has_atrium_controller = false;

  // The opener is the tab its window is showing. A background tab's popup
  // would otherwise throw a card over a page nobody is looking at.
  bool source_is_active_tab = false;

  // http, https, or the empty initial document. Sign-in libraries open a blank
  // popup and write into it, so an empty target is the common case here.
  bool target_is_peekable_scheme = false;

  // The feature string asked for a spot on another display. The card is a
  // child of the opener's own window and cannot leave it.
  bool targets_another_display = false;
};

// True when a page-opened popup should show as a peek over its opener instead
// of as a top-level popup window.
bool ShouldPeekPopupForInputs(const PopupPeekDecisionInputs& inputs);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_PEEK_DECISION_H_
