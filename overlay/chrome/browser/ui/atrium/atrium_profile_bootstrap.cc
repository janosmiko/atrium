// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_profile_bootstrap.cc
#include "chrome/browser/ui/atrium/atrium_profile_bootstrap.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

#include "base/check.h"
#include "base/json/json_reader.h"
#include "base/values.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_log_safe_url.h"
#include "chrome/browser/ui/atrium/atrium_settings_source.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_tabs.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "content/public/browser/browser_thread.h"
#include "url/gurl.h"

namespace atrium {

namespace {

// length-capped to match the Swift-side sanitizer
// (ImportApplier.maxProfileIDLength) - defense in depth, not the primary
// guard (Swift already rejects an overlong id before it ever reaches here).
constexpr size_t kMaxProfileIdLength = 64;

// The one ProfileBootstrap instance for the process, if Start() has run.
// Never destroyed (leaked, process-lifetime, mirrors BrowserController's
// Registry() pattern) - PrepareForShutdown() needs a way back to it despite
// Start() not returning the pointer.
ProfileBootstrap*& Instance() {
  static ProfileBootstrap* instance = nullptr;
  return instance;
}

}  // namespace

// static
void ProfileBootstrap::Start() {
  // Instance() must be assigned BEFORE Bootstrap() runs: the
  // already-resident initial profile's whole BrowserController is created
  // synchronously inside Bootstrap(), and that controller's ctor calls
  // AcquireProfileKeepAliveForSlot - which silently no-ops while Instance()
  // is null. Under acquire-only dedup that was invisible; with the
  // release side (ReleaseProfileKeepAliveForSlot) it left the initial
  // profile's refcount one short of every async-created profile's, so
  // closing its window would wrongly release the bootstrap's own pin too
  // (probe-caught: 'Default' never logged "refcount now 2" while
  // 'Profile 1' did).
  Instance() = new ProfileBootstrap();  // Leaks; process-lifetime like BrowserController.
  Instance()->Bootstrap();
}

// static
void ProfileBootstrap::PrepareForShutdown() {
  if (!Instance()) {
    return;
  }
  g_browser_process->profile_manager()->RemoveObserver(Instance());
}

// static
void ProfileBootstrap::AcquireProfileKeepAliveForSlot(Profile* profile) {
  if (!profile || !Instance()) {
    return;
  }
  Instance()->AcquireProfileKeepAlive(profile);
}

// static
void ProfileBootstrap::ReleaseProfileKeepAliveForSlot(
    const std::string& profile_id) {
  if (!Instance()) {
    return;
  }
  auto it = Instance()->keep_alive_refcounts_.find(profile_id);
  if (it == Instance()->keep_alive_refcounts_.end()) {
    LOG(WARNING) << "Atrium: ReleaseProfileKeepAliveForSlot('" << profile_id
                 << "') with no outstanding refcount - ignoring";
    return;
  }
  if (--it->second > 0) {
    VLOG(1) << "Atrium: keep-alive refcount for '" << profile_id
            << "' now " << it->second;
    return;
  }
  // Last window-slot showing this profile just tore down: actually release
  // the keep-alive, not just the count.
  Instance()->keep_alive_refcounts_.erase(it);
  Instance()->keep_alives_.erase(profile_id);
  VLOG(1) << "Atrium: released keep-alive for '" << profile_id << "'";
}

// static
bool ProfileBootstrap::IsSafeProfileId(std::string_view id) {
  if (id.empty() || id.size() > kMaxProfileIdLength || id.front() == '.' ||
      id.find("..") != std::string_view::npos) {
    return false;
  }
  return std::ranges::all_of(id, [](unsigned char c) {
    return base::IsAsciiAlphaNumeric(c) || c == ' ' || c == '_' ||
           c == '.' || c == '-';
  });
}

ProfileBootstrap::ProfileBootstrap() = default;

void ProfileBootstrap::Bootstrap() {
  // which profiles this launch needs, and which tab to show
  // first, both come from the spaces document. One call rather than three,
  // because the startup tab's profile has to BE one of the required ones or
  // its window is never created - deriving them together makes that true by
  // construction instead of by a std::find afterwards.
  //
  // Blocking, deliberately: nothing can be created until the answer is known,
  // so there is nothing to do while waiting. A refused or missing file leaves
  // the plan null and the single-Default fallback below stands, which is what
  // every launch has done since the Swift cut.
  //
  // Reading the file here is allowed for the same reason the path this
  // replaced could call GetLastUsedProfile: PreBrowserStart runs before the UI
  // thread's blocking restrictions tighten, and CreateForInitialProfile's own
  // comment says as much about its sync profile load. The sidebar's read is
  // posted because it runs later, on a thread where it would be fatal - not
  // because reading is forbidden everywhere.
  // the Startup row, which the plan now needs, read here rather than
  // taken from SeedFromSettings. That seed posts to the thread pool
  // (atrium_main_extra_parts.cc) while this runs synchronously from
  // PreBrowserStart, so its answer would land after the plan was already built.
  //
  // Through ResolveSettingsFilePath rather than a literal path, so a scenario
  // staging ATRIUM_SETTINGS_FILE drives the row instead of the real profile's
  // file. Blocking, on the same licence the plan read below already takes.
  //
  // The row's own spelling is handed over verbatim - the core owns
  // normalisation, and null (an absent row) is the same answer it gives an
  // unreadable one.
  std::optional<std::string> startup_behavior;
  std::optional<base::Value> settings = base::JSONReader::Read(
      LoadSettingsJSON(ResolveSettingsFilePath()), base::JSON_PARSE_RFC);
  if (settings && settings->is_dict()) {
    // FindString and not Find: a row carrying a non-string is one the core
    // refuses the whole file over, so there is no value here to pass on.
    if (const std::string* row =
            settings->GetDict().FindString("startupBehavior")) {
      startup_behavior = *row;
    }
  }
  std::string plan_json;
  if (OpenAtriumCoreBlocking()) {
    if (char* raw = atrium_core_launch_plan(
            startup_behavior ? startup_behavior->c_str() : nullptr)) {
      plan_json = raw;
      atrium_core_string_free(raw);
    }
  }
  std::vector<std::string> startup_ids;
  std::optional<base::Value> plan =
      plan_json.empty() ? std::nullopt
                        : base::JSONReader::Read(plan_json, base::JSON_PARSE_RFC);
  if (plan && plan->is_dict()) {
    const base::DictValue& root = plan->GetDict();
    if (const base::ListValue* required = root.FindList("requiredProfiles")) {
      for (const base::Value& id : *required) {
        // Still checked here: the id becomes a directory name through
        // AppendASCII, and a plan is a file the user can edit.
        if (id.is_string() && IsSafeProfileId(id.GetString())) {
          required_profile_ids_.push_back(id.GetString());
        }
      }
    }
    if (const base::ListValue* startup = root.FindList("startupProfiles")) {
      for (const base::Value& id : *startup) {
        if (id.is_string()) {
          startup_ids.push_back(id.GetString());
        }
      }
    }
    if (const base::DictValue* tab = root.FindDict("startupTab")) {
      if (const std::string* id = tab->FindString("profileID")) {
        active_profile_id_ = *id;
      }
      if (const std::string* url = tab->FindString("url")) {
        startup_tab_url_ = *url;
      }
    }
  }
  if (required_profile_ids_.empty()) {
    LOG(WARNING) << "Atrium: no usable profile ids in the launch plan; "
                    "falling back to a single Default profile";
    required_profile_ids_.push_back("Default");
  }
  // Never trust the plan's own active id blindly: a profile it names but the
  // required list does not would leave the shown window with no controller.
  if (active_profile_id_.empty() ||
      std::find(required_profile_ids_.begin(), required_profile_ids_.end(),
                active_profile_id_) == required_profile_ids_.end()) {
    active_profile_id_ = required_profile_ids_.front();
    startup_tab_url_ = "about:blank";
  }

  // The startup tab is a page the user was on, so it goes through LogSafeUrl
  // like every other url this layer prints.
  LOG(INFO) << "Atrium: bootstrapping " << required_profile_ids_.size()
            << " profile(s); showing '" << active_profile_id_
            << "' first with startup tab '"
            << LogSafeUrl(GURL(startup_tab_url_)) << "'";

  ProfileManager* profile_manager = g_browser_process->profile_manager();
  // Register before any bootstrap below so a CreateProfileAsync-created
  // profile cannot slip past us (ProfileManagerObserver::OnProfileAdded fires
  // once, at creation time - there is no "already added" replay).
  profile_manager->AddObserver(this);

  // split the required profiles into "needed before first paint"
  // and "deferred". An empty startup list means no deferral - preload
  // everything, the old behaviour - and the active id is always
  // startup-needed regardless, so a stale or inconsistent list can never cost
  // the visible window.
  const base::FilePath user_data_dir = profile_manager->user_data_dir();
  for (const std::string& profile_id : required_profile_ids_) {
    const bool startup_needed =
        startup_ids.empty() || profile_id == active_profile_id_ ||
        std::find(startup_ids.begin(), startup_ids.end(), profile_id) !=
            startup_ids.end();
    if (!startup_needed) {
      deferred_profile_ids_.push_back(profile_id);
      continue;
    }
    base::FilePath path = user_data_dir.AppendASCII(profile_id);
    if (Profile* existing = profile_manager->GetProfileByPath(path)) {
      // Already resident, so its OnProfileAdded fired before we registered
      // and this direct call is our only chance to bootstrap it. This is the
      // NORMAL path for the last-used profile: chrome_browser_main.cc loads
      // the initial profile (CreateInitialProfile) well before
      // PreBrowserStart() runs, even under --no-startup-window.
      LOG(INFO) << "Atrium: profile '" << profile_id
                << "' already resident; bootstrapping directly";
      OnProfileAdded(existing);
      continue;
    }
    profile_manager->CreateProfileAsync(
        path, base::BindOnce(&ProfileBootstrap::OnProfileCreated,
                             // Safe: this object is never destroyed.
                             base::Unretained(this), profile_id));
  }
  if (!deferred_profile_ids_.empty()) {
    LOG(INFO) << "Atrium: deferring " << deferred_profile_ids_.size()
              << " profile(s) past first paint";
    // Resident-active case: the shown window's OnProfileAdded already ran
    // mid-loop, before this set was complete - schedule here instead. The
    // async-active case schedules from OnProfileAdded's show branch (no
    // window has shown yet at this point); whichever fires first wins.
    if (shown_a_window_) {
      ScheduleDeferredLoad();
    }
  }
}

void ProfileBootstrap::ScheduleDeferredLoad() {
  if (deferred_kick_scheduled_ || deferred_profile_ids_.empty()) {
    return;
  }
  deferred_kick_scheduled_ = true;
  content::GetUIThreadTaskRunner({})->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&ProfileBootstrap::LoadDeferredProfiles,
                     // Safe: this object is never destroyed.
                     base::Unretained(this)),
      base::Seconds(2));
}

void ProfileBootstrap::StartProfileLoad(const std::string& profile_id) {
  // A profile someone else already loaded had its controller created by the
  // observer's own OnProfileAdded - calling it again here would construct a
  // duplicate (slot 0, profile) controller.
  if (BrowserController::ControllerFor(BrowserController::kDefaultSlot,
                                       profile_id)) {
    return;
  }
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  base::FilePath path =
      profile_manager->user_data_dir().AppendASCII(profile_id);
  if (Profile* existing = profile_manager->GetProfileByPath(path)) {
    // Resident but never bootstrapped (loaded before our observer
    // registered, then deferred) - same direct call Bootstrap() makes for
    // the already-resident initial profile.
    OnProfileAdded(existing);
    return;
  }
  profile_manager->CreateProfileAsync(
      path, base::BindOnce(&ProfileBootstrap::OnProfileCreated,
                           // Safe: this object is never destroyed.
                           base::Unretained(this), profile_id));
}

void ProfileBootstrap::LoadDeferredProfiles() {
  if (deferred_profile_ids_.empty()) {
    return;
  }
  std::vector<std::string> ids;
  ids.swap(deferred_profile_ids_);
  LOG(INFO) << "Atrium: loading " << ids.size() << " deferred profile(s)";
  for (const std::string& profile_id : ids) {
    StartProfileLoad(profile_id);
  }
}

// static
void ProfileBootstrap::EnsureProfileLoaded(const std::string& profile_id) {
  ProfileBootstrap* self = Instance();
  if (!self || !IsSafeProfileId(profile_id)) {
    return;
  }
  auto it = std::find(self->deferred_profile_ids_.begin(),
                      self->deferred_profile_ids_.end(), profile_id);
  if (it == self->deferred_profile_ids_.end()) {
    // Unknown id, or already loaded/loading (the deferred entry is erased
    // the moment its load starts) - logged no-op per the bridge contract.
    VLOG(1) << "Atrium: ensure_profile_loaded no-op for '" << profile_id
            << "'";
    return;
  }
  self->deferred_profile_ids_.erase(it);
  LOG(INFO) << "Atrium: on-demand load of deferred profile '" << profile_id
            << "'";
  self->StartProfileLoad(profile_id);
}

ProfileBootstrap::~ProfileBootstrap() = default;

void ProfileBootstrap::AcquireProfileKeepAlive(Profile* profile) {
  const std::string profile_id = profile->GetBaseName().AsUTF8Unsafe();
  const int new_count = ++keep_alive_refcounts_[profile_id];
  if (new_count > 1) {
    // Already held by an earlier slot (or this bootstrap's own
    // required-profile pass) - only the 0->1 transition below actually
    // constructs the ScopedProfileKeepAlive; every later slot sharing this
    // profile just bumps the count (see ReleaseProfileKeepAliveForSlot for
    // the matching decrement on window close).
    VLOG(1) << "Atrium: keep-alive refcount for '" << profile_id << "' now "
            << new_count;
    return;
  }
  // TryAcquire (not the throwing constructor) because AddKeepAlive refuses a
  // profile that is already being destroyed - a fresh profile here in
  // practice, but a null result is a safe no-op either way.
  keep_alives_[profile_id] = ScopedProfileKeepAlive::TryAcquire(
      profile, ProfileKeepAliveOrigin::kAtriumSpace);
}

bool ProfileBootstrap::IsRequiredProfilePath(
    const base::FilePath& path) const {
  for (const std::string& profile_id : required_profile_ids_) {
    if (path.BaseName().AsUTF8Unsafe() == profile_id) {
      return true;
    }
  }
  return false;
}

void ProfileBootstrap::OnProfileCreated(const std::string& profile_id,
                                        Profile* profile) {
  if (profile) {
    // Success: the substantive work (SetSavingEnabled, keep-alive,
    // BrowserController) already ran from OnProfileAdded, which the
    // ProfileManager fires before this callback.
    return;
  }
  LOG(ERROR) << "Atrium: creation of profile '" << profile_id << "' failed";
  if (profile_id != active_profile_id_ || shown_a_window_) {
    return;
  }
  // The profile that was supposed to own the visible window is gone. Promote
  // the first successfully-created controller, if any; otherwise flag the
  // failure so the next OnProfileAdded shows its window instead. Either way
  // the process never ends up running with zero windows.
  active_profile_failed_ = true;
  // the promotion below needs SOME controller to exist - if
  // everything else was deferred there is none yet, so kick the deferred
  // loads immediately (the flag above makes the first arrival show its
  // window). A no-op when nothing was deferred.
  LoadDeferredProfiles();
  if (first_controller_) {
    LOG(WARNING) << "Atrium: active profile '" << profile_id
                 << "' failed to load; promoting the first available "
                    "profile's window";
    shown_a_window_ = true;
    // first_controller_ was created hidden (show=false), so it has zero
    // tabs (lazy restore) - the intended startup tab belonged to the
    // profile that just failed, so there's nothing meaningful to show it
    // instead; a plain about:blank beats a totally empty window.
    first_controller_->EnsureAtLeastOneTab();
    BrowserController::ActivateWindow(first_controller_);
  }
}

void ProfileBootstrap::OnProfileAdded(Profile* profile) {
  if (!profile || !IsRequiredProfilePath(profile->GetPath())) {
    // Not one of ours (e.g. a system/guest original profile some other
    // component created); leave it alone.
    return;
  }

  const std::string profile_id = profile->GetBaseName().AsUTF8Unsafe();
  LOG(INFO) << "Atrium: profile '" << profile_id << "' initialized";

  // Must run before any BrowserController below creates a Browser: the first
  // Browser for this profile is what would otherwise schedule the first
  // SessionService command, and SetSavingEnabled(false) DCHECKs that no
  // command has been scheduled yet (session_service_base.cc:885).
  if (SessionService* session_service =
          SessionServiceFactory::GetForProfile(profile)) {
    session_service->SetSavingEnabled(false);
  }

  // Pins the profile alive even though its Browser may never show a window.
  // Belt-and-suspenders alongside the Browser ctor's own kBrowserWindow
  // keep-alive (browser.cc): that one only exists while a Browser for this
  // profile is alive, and nothing in this bootstrap promises one always will
  // be once hidden-space teardown exists.
  AcquireProfileKeepAlive(profile);

  bool show = (profile_id == active_profile_id_);
  if (active_profile_failed_ && !shown_a_window_) {
    LOG(WARNING) << "Atrium: active profile failed earlier; showing '"
                 << profile_id << "' instead";
    show = true;
  }
  if (show) {
    shown_a_window_ = true;
  }
  // The startup tab URL only applies to the profile it was computed for;
  // any other profile shown here got there via the failure-promotion path
  // above (active_profile_failed_), which has no legitimate startup tab of
  // its own - CreateForProfile ignores this string when show is false.
  std::string startup_url =
      (show && profile_id == active_profile_id_) ? startup_tab_url_
                                                  : "about:blank";
  // Decided model: the shown launch window opens TAB-LESS with the palette
  // (like Cmd+N/CreateWindowSlot) when there is no real tab to restore -
  // never a stray about:blank. An empty active space's startup tab resolves
  // to "" or "about:blank" here; both mean "nothing to restore", so open
  // zero tabs and let the palette (opened just below) create the first tab
  // on Enter. Ungated since the palette is an HTML surface the
  // browser builds itself, so there is no longer a build without one that
  // would need the about:blank to avoid a window with no way to make a tab.
  if (show && (startup_url.empty() || startup_url == "about:blank")) {
    startup_url.clear();
  }
  BrowserController* controller =
      BrowserController::CreateForProfile(profile, show, startup_url);
  // auto-open the palette on the shown startup window only when
  // there is NO restored tab to look at - startup_url is empty here exactly
  // in that case (see the clear above). A window that DID restore a live
  // tab (Space::startupTab, mirrored by StartupTabJSON.shouldAutoOpenCommandBar)
  // stays palette-free so the user lands on their tab, not an overlay.
  // Mirrors CreateWindowSlot's Cmd+N post-show hook, applied to the
  // multi-profile bootstrap's one shown window (the hidden-pool profiles
  // this function also constructs never reach here with show=true, so this
  // fires at most once per launch).
  if (show && startup_url.empty()) {
    controller->OpenCommandBar(/*is_startup_open=*/true);
  }
  if (!first_controller_) {
    first_controller_ = controller;
  }
  if (!show) {
    BrowserController::ShowLoadedProfile(profile);
  }
  // the visible window exists - the deferred profiles can load
  // now without competing with first paint. Short delay so their profile
  // init + hidden-controller construction lands after the window has
  // actually presented, not in the same task burst; a cross-profile switch
  // in that window doesn't wait for it (ensure_profile_loaded jumps the
  // queue). See ScheduleDeferredLoad's doc for why Bootstrap() also
  // schedules.
  if (show) {
    ScheduleDeferredLoad();
  }
}

}  // namespace atrium
