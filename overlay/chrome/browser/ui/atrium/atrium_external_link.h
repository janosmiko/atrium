// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_external_link.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_EXTERNAL_LINK_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_EXTERNAL_LINK_H_

#include <optional>
#include <string_view>

namespace atrium {

// where a link another macOS app hands over should open.
//
// Two halves, split the way confirm_to_quit's are. This one is pure and
// linkable by a unit test; the window it opens is in atrium_external_link_hook,
// which needs the controller.

// The externalLinksOpenInLittle field of a WHOLE settings object - the shape
// LoadSettingsJSON returns and the settings surface sends back.
//
// nullopt for an object that will not parse and for one that does not carry
// the field, matching ConfirmToQuitFromSettings. Absent is a real answer:
// every key past the first four is optional, so a settings file from another
// build legitimately arrives without it, and the caller keeps the DEFAULT
// rather than inventing a value.
//
// The default for an absent key is true, which is the core's own answer for
// the same file (settings::load). Do not spell it a second time here - ask
// through ExternalLinksOpenInLittle() below.
std::optional<bool> ExternalLinksOpenInLittleFromSettings(
    std::string_view settings_json);

// The value this process runs on, seeded once at startup from settings.json.
//
// A cached bool rather than a read at the moment of the event: the decision
// happens on -[AppController application:openURLs:], on the UI thread, where
// reading a file is a fatal DCHECK. SetConfirmToQuitPref's startup seam is the
// same shape and the same seed post.
//
// Defaults to true before anything seeds it, which is what a settings file
// without the key means.
bool ExternalLinksOpenInLittle();
void SetExternalLinksOpenInLittle(bool enabled);

// Whether a handed-over URL opens in a little window.
//
// The answer comes from the Rust core (atrium_core_external_link_target); this
// only fills the struct in. `has_url` and `is_http_or_https` are what the
// caller's GURL says, because GURL is the authority on a scheme and a
// hand-rolled prefix check here would be a second opinion.
bool ExternalLinkOpensLittleWindow(bool setting_enabled,
                                   bool has_url,
                                   bool is_http_or_https);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_EXTERNAL_LINK_H_
