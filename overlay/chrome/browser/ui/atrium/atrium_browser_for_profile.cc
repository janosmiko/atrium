// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_browser_for_profile.cc
#include "chrome/browser/ui/atrium/atrium_browser_for_profile.h"

#include "base/logging.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_profile_window_decision.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_collection.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"

namespace atrium {

namespace {

// Every live Atrium-managed window, most recently activated first - the
// order ChooseProfileWindow's activation rule needs. `*browsers` is a
// parallel list, so the index ChooseProfileWindow returns names a Browser*.
void CollectAtriumWindows(std::vector<ProfileWindowCandidate>* candidates,
                          std::vector<Browser*>* browsers) {
  candidates->clear();
  browsers->clear();
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* window) {
        Browser* browser = window->GetBrowserForMigrationOnly();
        // An incognito profile's GetPath() is its parent's, so it would
        // otherwise answer to the parent's profile_id here.
        if (browser->profile()->IsOffTheRecord()) {
          return true;
        }
        // The same "is this an Atrium window" test BrowserForOpenedPage
        // uses (atrium_settings_window.cc).
        BrowserController* controller =
            BrowserController::ControllerForBrowser(browser);
        if (!controller) {
          return true;  // Not an Atrium-managed window; keep scanning.
        }
        auto* atrium_window =
            static_cast<AtriumBrowserWindow*>(browser->window());
        candidates->push_back({
            .profile_id = browser->profile()->GetBaseName().AsUTF8Unsafe(),
            .realised = AtriumBrowserWindow::HasRealisedWindow(browser),
            .little = controller->IsLittle(),
            .shown = atrium_window && !atrium_window->IsExcludedFromWindowCycling(),
        });
        browsers->push_back(browser);
        return true;
      },
      BrowserCollection::Order::kActivation);
}

}  // namespace

Browser* BrowserForProfileId(const std::string& profile_id) {
  std::vector<ProfileWindowCandidate> candidates;
  std::vector<Browser*> browsers;
  CollectAtriumWindows(&candidates, &browsers);

  int index = ChooseProfileWindow(profile_id, candidates);
  if (index >= 0) {
    return browsers[static_cast<size_t>(index)];
  }

  // `browsers` is already in activation order and Atrium-only, so its first
  // entry - if any - is the last active Atrium window of any profile, which
  // is what CreateWindowSlotForProfile requires as its invoker.
  Browser* invoker = browsers.empty() ? nullptr : browsers.front();
  if (!invoker) {
    VLOG(1) << "Atrium: no window could be opened for profile '"
            << profile_id << "'";
    return nullptr;
  }
  // CreateWindowSlotForProfile hands back a controller, not a Browser*, and
  // there is no public accessor for the one it just built - re-scan instead.
  BrowserController::CreateWindowSlotForProfile(invoker, profile_id);

  CollectAtriumWindows(&candidates, &browsers);
  index = ChooseProfileWindow(profile_id, candidates);
  if (index < 0) {
    VLOG(1) << "Atrium: no window could be opened for profile '"
            << profile_id << "'";
    return nullptr;
  }
  return browsers[static_cast<size_t>(index)];
}

}  // namespace atrium
