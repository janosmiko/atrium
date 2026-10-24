// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_sidebar_model_source.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_MODEL_SOURCE_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_MODEL_SOURCE_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "base/files/file_path.h"

namespace atrium {

// where the sidebar's model comes from, as a decision that can be
// asserted rather than read. Same posture as atrium_peek_decision.h - values
// in, values out, no content:: or Browser dependency - shared by
// //chrome/browser/ui:ui and :unit_tests via a diamond.
//
// There are three sources, and which one answers is the whole question:
//
//   The Swift dylib, while it exists. It owns the model, and its answer is
//   authoritative - the file on disk LAGS it, because writes are debounced.
//
//   The Rust core, from spaces.json, when the dylib has no model. That is
//   every build after the cut, and today it is also an old dylib without the
//   export.
//
//   Nothing, when there is no store file either. The caller then emits the
//   flat tab list it genuinely knows, which shows LESS rather than something
//   made up.
enum class SidebarModelSource {
  kDylib,
  kRustCore,
  kFlatTabListOnly,
};

// Picks the source. `dylib_model` is what the dylib returned, empty when it
// has no export or nothing to say. `store_file_readable` is whether
// spaces.json could be read.
//
// The dylib wins whenever it answers. That ordering is not a preference, it is
// correctness: while Swift owns the model, the file is up to a second stale,
// so reading it would serve rows from before the user's last drag or close.
SidebarModelSource ChooseSidebarModelSource(const std::string& dylib_model,
                                            bool store_file_readable);

// Where spaces.json is.
//
// Honours ATRIUM_SPACES_FILE, matching SpacesStore.defaultFileURL - the probes
// stage a store that way, and a C++ reader that ignored the override would
// read the user's real one during a test.
//
// `app_support` is the Application Support directory. Returns an empty path
// when the override is unset and `app_support` is empty, so the caller falls
// through rather than reading from a made-up location.
base::FilePath SpacesFilePath(const std::string& env_override,
                              const base::FilePath& app_support);

// One live tab, in strip order.
struct LiveTab {
  std::string url;
  uint64_t id = 0;
  // The tab this window is showing - the row that gets the active pill
  //. The core's LiveTab mirrors this with #[serde(default)], so an
  // all-false strip still yields a model, just with nothing selected.
  bool active = false;
  // The favourite this tab IS, empty for an ordinary tab. The core binds the
  // tile from this key rather than by url, because a redirect moves the tab
  // off the tile's url.
  std::string claimed;
  // A tab in ANOTHER window of this profile, listed so the core can tell a
  // row or tile whose tab is next door from a cold one. The core never hands
  // such an id out as live here. A frozen window marks the tab that left it
  // active, and that tab is one of these.
  bool elsewhere = false;
};

// The live strip as the Rust core wants it: [{"url":..,"id":..,"active":..}].
//
// Order is the contract, not a detail. The core hands ids out by CONSUMING
// this queue rather than by looking a URL up, because two rows sharing a URL
// have to get different tabs - a first-match map gives both the same one, and
// then both rows activate it.
//
// Written by hand rather than through base::Value because a tab id is uint64_t
// and base::Value has no 64-bit integer. As a double it comes out `11.0`, which
// the core's `u64` refuses - and a refused strip is an EMPTY strip, so every
// row goes cold with nothing to say which call went wrong.
std::string LiveStripJSON(const std::vector<LiveTab>& tabs);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_MODEL_SOURCE_H_
