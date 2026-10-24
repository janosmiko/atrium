// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_profile_bootstrap.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_PROFILE_BOOTSTRAP_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_PROFILE_BOOTSTRAP_H_

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/profiles/profile_manager_observer.h"

class Profile;
class ScopedProfileKeepAlive;

namespace atrium {

class BrowserController;

// M2b entry point replacing BrowserController::CreateForInitialProfile()
// when the spaces symbol group is connected. Reads the set of profile ids
// required by the user's spaces (AtriumCopyRequiredProfilesJSON), preloads
// each one under --no-startup-window, pins it alive, disables its
// SessionService before any window can write to it, and creates one
// BrowserController per profile.
//
// Lazy restore: AtriumCopyStartupTabJSON names the active
// space's profile id and the one tab to materialize there; that profile is
// shown with exactly that tab, every other required profile is created
// hidden with zero tabs (cold until the user visits that space). This
// replaces the "first (alphabetically) required id" heuristic with the real
// active-space id from Swift.
//
// M2c: also the single source of truth for kAtriumSpace keep-alive
// refcounting across window-slots - see
// AcquireProfileKeepAliveForSlot(), which BrowserController's constructor
// calls for every slot it creates (bootstrap, Cmd+N, or the
// BrowserCollectionObserver attach path), not just the bootstrap's own
// required-profile pass. The release side,
// ReleaseProfileKeepAliveForSlot(), is called from
// BrowserController::OnTabStripModelDestroyed - the keep-alive for a
// profile is only actually released once the LAST window-slot showing it
// has torn down.
class ProfileBootstrap : public ProfileManagerObserver {
 public:
  // Leaks intentionally: process-lifetime, mirrors BrowserController.
  //
  // nothing calls this. Multi-profile startup asked Swift which
  // profiles a launch needed, so since the cut every launch takes
  // BrowserController::CreateForInitialProfile instead. The class is kept
  // because its profile keep-alive API is still called on every window, and
  // This entry point may get a data source again.
  static void Start();

  // Shutdown fix: ProfileManager's observers_ list asserts empty
  // on destruction (check_empty=true, profile_manager.h), but ProfileBootstrap
  // is a leaked, process-lifetime AddObserver(this) that otherwise never
  // unregisters - a FATAL on every clean shutdown once anything actually
  // exercises it (a SIGTERM shutdown
  // gate surfaces it). No-op if
  // Start() was never called (no ProfileBootstrap instance exists). Call
  // from a shutdown hook that still runs before ProfileManager's destructor
  // (see AtriumMainExtraParts::PostMainMessageLoopRun).
  static void PrepareForShutdown();

  // M2c: acquires (if not already held) the process-lifetime kAtriumSpace
  // keep-alive for `profile`, refcounted per profile ACROSS WINDOW-SLOTS
  // - a second or third slot showing a profile
  // already warmed by an earlier slot (Cmd+N reusing the invoking window's
  // profile, or ProfileBootstrap's own required-profile pass) increments the
  // refcount without constructing a redundant ScopedProfileKeepAlive. No-op
  // if `profile` is null or Start() was never called (no ProfileBootstrap
  // instance - the M2a fallback, which relies solely on Chrome's own
  // per-Browser keep-alive, as before M2c). See ReleaseProfileKeepAliveForSlot
  // for the release side of this refcount.
  static void AcquireProfileKeepAliveForSlot(Profile* profile);

  // The release side of AcquireProfileKeepAliveForSlot's refcount -
  // decrements `profile_id`'s count and, only when it hits zero (the last
  // window-slot showing that profile has torn down), erases both the
  // ScopedProfileKeepAlive and the refcount entry, actually releasing the
  // keep-alive. Call from BrowserController::OnTabStripModelDestroyed (the
  // definitive "this controller's Browser is truly gone" signal) using the
  // controller's cached profile_id_ string member - browser_->profile() is
  // unusable there (browser_ is nulled in that same function). No-op if
  // `profile_id` has no outstanding refcount, or Start() was never called
  // (no ProfileBootstrap instance).
  static void ReleaseProfileKeepAliveForSlot(const std::string& profile_id);

  // a profile id must be a single safe path component - the
  // dylib's JSON is Atrium's own data, but it is still an on-disk file -
  // never let a malformed or tampered entry escape user_data_dir via ".."
  // or path separators. Mirrors ImportApplier.isValidProfileID's allowlist
  // (A-Za-z0-9, space, '_', '.', '-') instead of only blocklisting '/' and
  // '\\' - defense in depth (Swift already rejects anything outside that
  // allowlist before a profileID reaches here), length-capped to match the
  // Swift-side sanitizer (ImportApplier.maxProfileIDLength). Promoted from
  // this file's own anonymous namespace so
  // set_profile_display_name/copy_profile_names_json (the retired ABI header's
  // PROFILE DISPLAY NAMES doc block) can reuse the exact same guard instead
  // of a third drifting copy.
  static bool IsSafeProfileId(std::string_view id);

  // loads a DEFERRED required profile now (the ensure_profile_loaded
  // op - a cross-profile switch can't wait for the post-first-show deferred
  // pass). A logged no-op for an id that isn't in the deferred set (unknown,
  // already loaded/loading, or unsafe - see the retired ABI header's ENSURE PROFILE
  // LOADED doc block), or if Start() never ran.
  static void EnsureProfileLoaded(const std::string& profile_id);

  ProfileBootstrap(const ProfileBootstrap&) = delete;
  ProfileBootstrap& operator=(const ProfileBootstrap&) = delete;

  // ProfileManagerObserver:
  void OnProfileAdded(Profile* profile) override;

 private:
  ProfileBootstrap();
  ~ProfileBootstrap() override;  // Never runs; process-lifetime instance.

  // The substantive bootstrap work (read required profiles, register the
  // observer, create/bootstrap each profile). Split out of the constructor
  // so Start() can assign Instance() FIRST: the already-resident
  // initial profile's BrowserController is created synchronously in here,
  // and its ctor's AcquireProfileKeepAliveForSlot call needs Instance()
  // non-null to count that slot's keep-alive reference (see Start()).
  void Bootstrap();

  bool IsRequiredProfilePath(const base::FilePath& path) const;

  // CreateProfileAsync's initialized_callback; `profile` is null on failure.
  // Only the failure path acts here (the substantive per-profile work runs
  // from OnProfileAdded): a failed ACTIVE profile promotes another
  // already-created controller to visible, so the process never ends up with
  // zero windows.
  void OnProfileCreated(const std::string& profile_id, Profile* profile);

  // Shared dedup logic behind AcquireProfileKeepAliveForSlot(); see its doc.
  void AcquireProfileKeepAlive(Profile* profile);

  // starts the CreateProfileAsync load for one required profile -
  // the shared path Bootstrap() (startup set), LoadDeferredProfiles() (the
  // post-first-show pass), and EnsureProfileLoaded() (on-demand op) all
  // funnel through. A no-op for an already-resident profile handled by
  // Bootstrap()'s own direct OnProfileAdded call.
  void StartProfileLoad(const std::string& profile_id);

  // kicks the CreateProfileAsync loads for every id still in
  // deferred_profile_ids_ (clears the set). Scheduled ~2s after the first
  // window shows; also invoked immediately when the active profile fails
  // (the promotion path needs SOME controller to show) and per-id on demand
  // via EnsureProfileLoaded.
  void LoadDeferredProfiles();

  // one-shot PostDelayedTask of LoadDeferredProfiles (~2s). Called
  // from BOTH the end of Bootstrap() (resident active profile - its shown
  // window's OnProfileAdded ran mid-loop, BEFORE the later ids were
  // partitioned into the deferred set, so that hook saw an empty set) and
  // OnProfileAdded's show branch (async-loaded active profile - lands after
  // the loop, when the set is complete). deferred_kick_scheduled_ keeps it
  // one-shot.
  void ScheduleDeferredLoad();

  std::vector<std::string> required_profile_ids_;
  // required profiles NOT in the startup set (see
  // the retired ABI header's STARTUP PROFILES doc block) - loaded by
  // LoadDeferredProfiles() instead of Bootstrap()'s pre-paint pass. Empty
  // when the dylib doesn't export the startup set (eager fallback).
  std::vector<std::string> deferred_profile_ids_;
  std::string active_profile_id_;
  // The tab to materialize in active_profile_id_'s controller; empty/unused
  // once that profile's controller has been created (see OnProfileAdded).
  std::string startup_tab_url_;
  // Keyed by profile id (Profile::GetBaseName()), one entry per profile with
  // an outstanding refcount (see keep_alive_refcounts_) - see
  // AcquireProfileKeepAlive / ReleaseProfileKeepAliveForSlot.
  std::map<std::string, std::unique_ptr<ScopedProfileKeepAlive>> keep_alives_;
  // How many window-slots currently hold profile_id's keep-alive.
  // Incremented in AcquireProfileKeepAlive (only the 0->1 transition
  // actually constructs the ScopedProfileKeepAlive); decremented in
  // ReleaseProfileKeepAliveForSlot, which erases both this entry and
  // keep_alives_' when the count reaches zero.
  std::map<std::string, int> keep_alive_refcounts_;

  // Resilience state for the promote-on-active-failure path. The controller
  // pointer never dangles: controllers are process-lifetime, never destroyed.
  bool shown_a_window_ = false;
  bool active_profile_failed_ = false;
  // see ScheduleDeferredLoad().
  bool deferred_kick_scheduled_ = false;
  raw_ptr<BrowserController> first_controller_ = nullptr;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_PROFILE_BOOTSTRAP_H_
