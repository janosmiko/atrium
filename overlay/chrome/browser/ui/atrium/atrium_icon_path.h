// Copyright 2026 The Atrium Authors

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_ICON_PATH_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_ICON_PATH_H_

#include <string>

namespace atrium {

// Which cache, if either, a WebUI request path belongs to.
//
// The icons are served from the atrium.top-chrome host itself rather than
// hosts of their own, because a separate host was never reached: the
// renderer's load failed with net::ERR_FAILED before StartDataRequest ran.
// See the request filter in atrium_ui.cc for the full account.
enum class IconKind {
  kNone,
  kFavicon,    // favicon/<scope>/<tab_id>.png
  kExtension,  // exticon/<scope>/<extension_id>.png
  kUrlIcon,    // urlicon/<scope>/<percent-encoded page url>.png
  kAppIcon,    // appicon/<variant slug>.png
};

// One parsed request. `id` is already decoded for kUrlIcon and raw otherwise;
// it is empty when the kind is kNone.
struct IconRequest {
  IconKind kind = IconKind::kNone;
  std::string id;

  // kFavicon, kUrlIcon, and kExtension: which profile's icons this request
  // may see. Empty when the path names none, which reaches no cache at all.
  std::string scope;
};

// Pure, so the routing can be asserted rather than read off the source
// (slot_presentation_table's posture). The id comes from the renderer, so
// nothing here trusts it - a caller looks it up and answers 404 on a miss.
IconRequest ParseIconPath(const std::string& path);

// Shorthand for callers that only need to know whether to serve the path at
// all, which is what the request filter asks.
IconKind IconRequestKind(const std::string& path);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_ICON_PATH_H_
