// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_main_extra_parts.cc
#include "chrome/browser/ui/atrium/atrium_main_extra_parts.h"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/strings/cstring_view.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/ui/atrium/atrium_auto_mini_player.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_chrome_context.h"
#include "chrome/browser/ui/atrium/atrium_confirm_to_quit.h"
#include "chrome/browser/ui/atrium/atrium_external_link.h"
#include "chrome/browser/ui/atrium/atrium_native_hotkey_map.h"
#include "chrome/browser/ui/atrium/atrium_pip_always_on_top.h"
#include "chrome/browser/ui/atrium/atrium_profile_bootstrap.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_tabs.h"
#include "chrome/browser/ui/atrium/atrium_window_appearance.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#if BUILDFLAG(IS_MAC)
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_settings_menu_hook.h"
#endif
#include "chrome/browser/ui/atrium/atrium_settings_source.h"
#include "chrome/browser/ui/atrium/atrium_settings_window.h"
#include "chrome/browser/ui/atrium/atrium_traffic_routing.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_model_source.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "chrome/common/chrome_switches.h"
#include "content/public/common/content_switches.h"

namespace atrium {

namespace {

// settings.json is where the user's answer to "warn before quitting"
// lives, and the browser's own quit gate reads a pref instead. Every launch
// hands the stored answer over, so a value edited in a previous session - or
// staged by a scenario, which starts on a fresh local state every time - is in
// force before the first Cmd+Q can be pressed.
//
// The stored value wins at launch, which makes the app menu's own "Confirm to
// Quit" item a change for the session rather than for good. That item writes
// the pref and nothing else; giving it the settings file too is separate work.
//
// Off the UI thread, because the core reads the file - the mistake
// AtriumSettingsWindow::PushSettings records - and through the settings
// surface's own reader, so the two cannot land on different files.
// and the same for the three fields a WINDOW paints. One read, both
// answers - two posts would read the same file twice and could disagree about
// it if a write landed between them. startupBehavior sits on the same
// read for the same reason; three answers now, still one read.
// The sidebar's width lives in a different store - see
// SeedStores below for what stays shared and what could not be.
//
// The reply may land either side of the first window: this read and
// ProfileBootstrap's are both async. SetWindowAppearance covers both orders -
// it applies to every window that already exists, and holds the value for the
// ones the constructor has yet to see.
//
// the two stores a launch has to read. The width the sidebar opens
// at is the fifth window value, and it is the one that is NOT in settings.json
// - the store keeps appState.sidebarWidth in the spaces document. So this is a
// second FILE, which no amount of sharing can avoid, but still one post and one
// reply: the width and the four settings answers reach SetWindowAppearance
// together, in a single struct, rather than in two calls whose order would
// decide which of them the next window inherits.
// hotkeys.json is the THIRD store, and it joins for the reason the
// spaces document did - the value it holds is not in settings.json and cannot
// be. Still one post and one reply: a second trip for the hotkeys would read
// on its own schedule, and a launch that installed the bindings before or
// after the window values depending on disk timing is the kind of order
// nothing asserts and everything depends on.
// routing.json is the FOURTH, and it joins for the reason the other
// two did. Its rules must be in hand before the first navigation can be
// consulted about, and a separate trip would read on its own schedule - a
// launch that armed routing before or after the first tab depending on disk
// timing is the kind of order nothing asserts and everything depends on.
struct SeedStores {
  std::string settings_json;
  std::string spaces_json;
  std::string hotkeys_json;
  std::string routing_json;
  bool spaces_absent = false;
  bool import_detected = false;
};

SeedStores ReadSeedStores(
    base::FilePath settings_path,
    base::FilePath spaces_path,
    base::FilePath hotkeys_path,
    base::FilePath routing_path,
    std::vector<std::pair<std::string, base::FilePath>> import_dirs) {
  SeedStores stores{LoadSettingsJSON(settings_path),
                    LoadSpacesJSON(spaces_path), LoadHotkeysJSON(hotkeys_path),
                    LoadRoutingJSON(routing_path)};
  stores.spaces_absent = !base::PathExists(spaces_path);
  // Only a fresh profile gets the offer, so only a fresh profile looks.
  stores.import_detected =
      stores.spaces_absent &&
      std::ranges::any_of(import_dirs, [](const auto& source) {
        return ImportDetected(source.first, source.second);
      });
  return stores;
}

// The reserved chrome context lands a little after startup, and Settings
// cannot build before it. 20 tries at 500ms covers a slow VM launch.
constexpr int kImportOfferTries = 20;
constexpr base::TimeDelta kImportOfferRetry = base::Milliseconds(500);

void ShowImportPaneWhenReady(int tries_left) {
  if (!AtriumChromeContext::GetIfExists()) {
    if (tries_left > 0) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE, base::BindOnce(&ShowImportPaneWhenReady, tries_left - 1),
          kImportOfferRetry);
    } else {
      LOG(WARNING) << "Atrium: the import offer found no chrome context";
    }
    return;
  }
  AtriumSettingsWindow::Show("import");
  VLOG(1) << "Atrium: import offer shown";
}

// Writes the flag BEFORE the window opens, so the pane's settings push reads
// it, and so a crash while the window builds cannot offer twice.
void OfferImportOnce(const std::string& settings_json) {
  std::string marked = WithImportPromptShown(settings_json);
  if (marked.empty()) {
    return;
  }
  SettingsWriteRunner()->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&SaveSettingsJSON, ResolveSettingsFilePath(),
                     std::move(marked)),
      base::BindOnce([](int32_t code) {
        // No written flag means the offer would repeat on every launch.
        if (code != kSaveWritten) {
          LOG(WARNING) << "Atrium: importPromptShown could not be written; "
                          "the import offer is skipped";
          return;
        }
        ShowImportPaneWhenReady(kImportOfferTries);
      }));
}

void SeedFromSettings() {
  // Resolved here rather than inside ReadSeedStores, matching the settings
  // path: one place works the environment out, so two readers cannot land on
  // different files. ATRIUM_SPACES_FILE and ATRIUM_HOTKEYS_FILE are what a
  // scenario stages, and one Environment answers both.
  std::unique_ptr<base::Environment> environment = base::Environment::Create();
  std::string spaces_override =
      DevOnlyPathOverride("ATRIUM_SPACES_FILE");
  std::string hotkeys_override =
      DevOnlyPathOverride("ATRIUM_HOTKEYS_FILE");
  std::string routing_override =
      DevOnlyPathOverride("ATRIUM_ROUTING_FILE");
  base::FilePath app_support;
  base::PathService::Get(base::DIR_APP_DATA, &app_support);
  std::vector<std::pair<std::string, base::FilePath>> import_dirs;
  for (std::string_view source : kImportSources) {
    import_dirs.emplace_back(source, ImportSourceDir(std::string(source)));
  }

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ReadSeedStores, ResolveSettingsFilePath(),
                     SpacesFilePath(spaces_override, app_support),
                     HotkeysFilePath(hotkeys_override, app_support),
                     RoutingFilePath(routing_override, app_support),
                     std::move(import_dirs)),
      base::BindOnce([](SeedStores stores) {
        // Before anything that can open a tab. A seam that consults empty
        // rules routes nothing, which looks exactly like routing being off.
        SetRoutingRulesJSON(std::move(stores.routing_json));
        SetAutoMiniPlayer(AutoMiniPlayerFromSettings(stores.settings_json));
        std::optional<bool> enabled =
            ConfirmToQuitFromSettings(stores.settings_json);
        if (enabled.has_value()) {
          SetConfirmToQuitPref(*enabled);
        }
        // seeded here rather than read at the moment a
        // link arrives - openURLs runs on the UI thread, where reading a file
        // is a fatal DCHECK. An absent key leaves the default (on) alone,
        // which is the same answer the core gives for the same file.
        std::optional<bool> little = ExternalLinksOpenInLittleFromSettings(
            stores.settings_json);
        if (little.has_value()) {
          SetExternalLinksOpenInLittle(*little);
        }
        WindowAppearance appearance =
            WindowAppearanceFromSettings(stores.settings_json);
        // The one field the settings object cannot answer, filled in from the
        // document that holds it.
        appearance.sidebar_width = SidebarWidthFromSpaces(stores.spaces_json);
        AtriumBrowserWindow::SetWindowAppearance(appearance);
        SetConfirmQuitPanelAppearance(appearance.opaque_surround,
                                      appearance.surround_argb);
        std::optional<std::string> notification_timeout =
            AtriumBrowserWindow::NotificationTimeoutFromSettings(
                stores.settings_json);
        if (notification_timeout.has_value()) {
          AtriumBrowserWindow::SetNotificationTimeoutStep(
              *notification_timeout);
        }
        std::optional<std::pair<std::string, bool>> skylight =
            AtriumBrowserWindow::SkylightFromSettings(stores.settings_json);
        if (skylight.has_value()) {
          AtriumBrowserWindow::SetSkylight(skylight->first, skylight->second);
        }
        // without this the drift is only ever switched on by a
        // settings SAVE, so a stored preference did nothing until the user
        // toggled it again in the same session.
        std::optional<bool> chrome_page_tint =
            AtriumBrowserWindow::ChromePageTintFromSettings(
                stores.settings_json);
        if (chrome_page_tint.has_value()) {
          AtriumBrowserWindow::SetChromePageTint(*chrome_page_tint);
        }
        // The pin the user left on: without this it survives in the file but
        // not across a restart, which is the whole point of saving it.
        std::optional<bool> address_bar_pinned =
            AtriumBrowserWindow::AddressBarPinnedFromSettings(
                stores.settings_json);
        if (address_bar_pinned.has_value()) {
          AtriumBrowserWindow::SetDevBarPinned(*address_bar_pinned);
        }
        // without this the next mini player opens floating
        // regardless of what the user last chose.
        LoadPipAlwaysOnTopFromSettings(stores.settings_json);
        // Without this the Dock shows app.icns - the DEFAULT variant - until
        // the user opens the pane and picks their own again.
        std::optional<std::string> app_icon_variant =
            AtriumBrowserWindow::AppIconVariantFromSettings(
                stores.settings_json);
        if (app_icon_variant.has_value()) {
          AtriumBrowserWindow::SetAppIconVariant(*app_icon_variant);
        }
        std::optional<bool> ai_enabled =
            AtriumBrowserWindow::AiEnabledFromSettings(stores.settings_json);
        if (ai_enabled.has_value()) {
          AtriumBrowserWindow::SetAiEnabled(*ai_enabled);
        }
        std::optional<std::string> ai_panel_hotkey =
            AtriumBrowserWindow::AiPanelHotkeyFromSettings(
                stores.settings_json);
        if (ai_panel_hotkey.has_value()) {
          AtriumBrowserWindow::SetAiPanelHotkey(*ai_panel_hotkey);
        }
        std::optional<bool> auto_summarize =
            AtriumBrowserWindow::AutoSummarizeFromSettings(
                stores.settings_json);
        if (auto_summarize.has_value()) {
          AtriumBrowserWindow::SetAiPanelAutoSummarize(*auto_summarize);
        }
        std::optional<bool> vim_mode_enabled =
            AtriumBrowserWindow::VimModeEnabledFromSettings(
                stores.settings_json);
        if (vim_mode_enabled.has_value()) {
          AtriumBrowserWindow::SetVimModeEnabled(*vim_mode_enabled);
        }
        std::optional<std::string> translate_language =
            AtriumBrowserWindow::TranslateLanguageFromSettings(
                stores.settings_json);
        if (translate_language.has_value()) {
          AtriumBrowserWindow::SetAiPanelTranslateLanguage(
              *translate_language);
        }
        base::DictValue swipe_tuning =
            AtriumBrowserWindow::SwipeTuningFromSettings(
                stores.settings_json);
        if (!swipe_tuning.empty()) {
          AtriumBrowserWindow::SetSwipeTuning(std::move(swipe_tuning));
        }
        // The bindings the key hooks match against. Safe before any window
        // exists - the map is process-wide state the hooks read on each
        // event, not something a window is handed at construction - and the
        // reply lands well before there is a window to press a key at.
        NativeHotkeyMap::Get()->SetFromJSON(stores.hotkeys_json);
        if (ShouldOfferImport(stores.spaces_absent, stores.import_detected,
                              stores.settings_json)) {
          OfferImportOnce(stores.settings_json);
        }
      }));
}

// Cocoa reference date, matching atrium_sidebar_tabs.cc's MigrationInstantNow
// and every other file that duplicates this rather than sharing a header for
// one two-line function.
double ArchiveSweepInstantNow() {
  return base::Time::Now().InSecondsFSinceUnixEpoch() - 978307200.0;
}

// How often the timer ticks, not how often a tab actually archives - the
// threshold is measured in hours, so checking every 15 minutes costs nothing
// and still catches a due tab promptly after its TTL passes.
constexpr base::TimeDelta kArchiveSweepInterval = base::Minutes(15);

// a snooze's shortest duration is the fixed one-hour default
// sidebarTabSnoozed still uses (the duration menu is a separate AC) - 15
// minutes late on that is a quarter of the whole snooze, far worse than 15
// minutes late against a 12-hour-plus archive threshold. A one-minute tick
// keeps the miss under 2% of the shortest snooze for one JSON parse over a
// handful of rows.
constexpr base::TimeDelta kWakeCheckInterval = base::Minutes(1);

// One snoozed row past its wake_at, as read off atrium_core_snoozed_overview.
struct DueSnoozedTab {
  std::string id;
  std::string url;
  std::string title;
  bool alert = false;
  int space_index = -1;
};

// atrium_core_sweep_archive only reports whether anything moved, not which
// rows - a before/after diff of this is how the caller learns the ids.
std::vector<std::string> ArchivedIdsNow() {
  std::vector<std::string> ids;
  char* raw = atrium_core_archived_overview();
  if (!raw) {
    return ids;
  }
  std::string json(raw);
  atrium_core_string_free(raw);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return ids;
  }
  for (const base::Value& entry : parsed->GetList()) {
    const base::DictValue* dict = entry.GetIfDict();
    if (!dict) {
      continue;
    }
    if (const std::string* id = dict->FindString("id")) {
      ids.push_back(*id);
    }
  }
  return ids;
}

std::vector<DueSnoozedTab> DueSnoozedTabsNow(double now) {
  std::vector<DueSnoozedTab> due;
  char* raw = atrium_core_snoozed_overview();
  if (!raw) {
    return due;
  }
  std::string json(raw);
  atrium_core_string_free(raw);

  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return due;
  }
  for (const base::Value& entry : parsed->GetList()) {
    const base::DictValue* dict = entry.GetIfDict();
    if (!dict) {
      continue;
    }
    std::optional<double> wake_at = dict->FindDouble("wakeAt");
    const std::string* id = dict->FindString("id");
    const std::string* url = dict->FindString("url");
    if (!wake_at || *wake_at > now || !id || !url || url->empty()) {
      continue;
    }
    DueSnoozedTab tab;
    tab.id = *id;
    tab.url = *url;
    if (const std::string* title = dict->FindString("title")) {
      tab.title = *title;
    }
    tab.alert = dict->FindBool("alert").value_or(false);
    tab.space_index = dict->FindInt("spaceIndex").value_or(-1);
    due.push_back(std::move(tab));
  }
  return due;
}

// Same posture as atrium_settings_window.cc's BrowserForOpenedPage: a
// process-wide timer has no window of its own, so it has to find one, and
// only an Atrium-managed window with a realised native window is safe to
// add a tab to.
Browser* BrowserForWake() {
  BrowserWindowInterface* last =
      GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser();
  Browser* browser = last ? last->GetBrowserForMigrationOnly() : nullptr;
  if (!browser || !atrium::BrowserController::ControllerForBrowser(browser)) {
    return nullptr;
  }
  return atrium::AtriumBrowserWindow::HasRealisedWindow(browser) ? browser
                                                                  : nullptr;
}

}  // namespace

bool WakeDueSnoozedTabs(double now) {
  if (!atrium_core_is_open()) {
    return false;
  }
  std::vector<DueSnoozedTab> due = DueSnoozedTabsNow(now);
  if (due.empty()) {
    return false;
  }

  // No window to land a woken tab in yet - nothing has been removed from
  // doc.snoozed, so the next tick retries rather than losing the wake.
  Browser* browser = BrowserForWake();
  if (!browser) {
    return false;
  }
  auto* window = static_cast<AtriumBrowserWindow*>(browser->window());
  BrowserController* controller = BrowserController::ControllerForBrowser(browser);

  for (const DueSnoozedTab& tab : due) {
    if (tab.space_index >= 0 && controller) {
      controller->SelectSpace(tab.space_index);
    }
    window->OpenOrActivateUrl(tab.url);
    atrium_core_snoozed_remove(tab.id.c_str());

    if (!tab.alert) {
      continue;
    }
    content::WebContents* active =
        browser->tab_strip_model()->GetActiveWebContents();
    if (!active) {
      continue;
    }
    window->NotifySnoozedTabReturned(base::NumberToString(TabId(active)),
                                     tab.title,
                                     SpaceAccentColor(browser, tab.space_index));
  }

  window->PushSidebarSnapshot();
  base::ThreadPool::PostTask(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
      base::BindOnce([] {
        if (atrium_core_save() < 0) {
          LOG(WARNING) << "Atrium: wake sweep save failed";
        }
      }));
  return true;
}

std::vector<std::string> SweepArchivedTabIds(const char* threshold,
                                              double now) {
  if (!atrium_core_is_open()) {
    return {};
  }
  std::vector<std::string> before = ArchivedIdsNow();
  if (!atrium_core_sweep_archive(threshold, now)) {
    return {};
  }
  std::vector<std::string> after = ArchivedIdsNow();
  std::vector<std::string> swept;
  for (const std::string& id : after) {
    bool was_already_archived =
        std::find(before.begin(), before.end(), id) != before.end();
    if (!was_already_archived) {
      swept.push_back(id);
    }
  }
  return swept;
}

void NotifyThenPushArchiveSweep(
    const std::vector<std::string>& swept,
    base::OnceCallback<void(const std::vector<std::string>&)> notify,
    base::OnceClosure push) {
  if (!swept.empty()) {
    std::move(notify).Run(swept);
  }
  std::move(push).Run();
}

AtriumMainExtraParts::AtriumMainExtraParts() = default;
AtriumMainExtraParts::~AtriumMainExtraParts() = default;

void ApplyDefaultLaunchSwitches() {
  // this used to return early unless a dylib resolved, which was
  // how "is this an Atrium build" was answered. Since the cut no dylib
  // resolves, so a Dock or Finder launch got none of the switches below and
  // ran on Chromium's default user-data-dir. Every build is an Atrium build
  // now, so the switches are unconditional.
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  // Only the browser process (empty --type) resolves a profile directory or
  // runs first-run/default-browser flows; child processes that need
  // --user-data-dir inherit it from the parent's already-modified command
  // line, so touching theirs here would be a no-op at best.
  if (!command_line->GetSwitchValueASCII(switches::kProcessType).empty()) {
    return;
  }
  if (!command_line->HasSwitch(switches::kUserDataDir)) {
    base::FilePath app_data_dir;
    // base::DIR_APP_DATA is ~/Library/Application Support (see
    // chrome_paths_mac.mm's GetDefaultUserDataDirectory) - Append matches
    // run-atrium.sh's --user-data-dir literally.
    if (base::PathService::Get(base::DIR_APP_DATA, &app_data_dir)) {
      command_line->AppendSwitchPath(
          switches::kUserDataDir,
          app_data_dir.Append("Atrium").Append("Chromium"));
    }
  }
  if (!command_line->HasSwitch(switches::kNoFirstRun)) {
    command_line->AppendSwitch(switches::kNoFirstRun);
  }
  if (!command_line->HasSwitch(switches::kNoDefaultBrowserCheck)) {
    command_line->AppendSwitch(switches::kNoDefaultBrowserCheck);
  }
  // Read here, not on the SeedFromSettings hop: the feature this turns on is
  // read out of the command line before that hop runs.
  AppendAutoMiniPlayerFeature(
      AutoMiniPlayerFromSettings(LoadSettingsJSON(ResolveSettingsFilePath())),
      command_line);
}

void AtriumMainExtraParts::PreBrowserStart() {
  // this used to be `if (!UIBridge::Get()->connected()) return;`,
  // which skipped the chrome layer, the collection observer and every
  // controller-creating path whenever no dylib loaded. After the Swift cut
  // that is always, so the browser started with an Atrium window and nothing
  // inside it - no controller, no chrome context, no sidebar push.
  //
  // Everything below is already null-guarded on the individual op groups,
  // which is what the degraded-dylib design bought us: it degrades to the
  // no-dylib case by construction rather than by this early return.
  LOG(INFO) << "Atrium: starting the chrome layer";

  // Kicks off async creation of the reserved chrome-layer BrowserContext
  //. Async, so nothing here blocks startup - the WebUI stays
  // disabled until the profile lands, which is what
  // AtriumChromeContext::GetIfExists() returning null means.
  atrium::AtriumChromeContext::Start();
  // the app menu's Settings item has an explicit target and
  // action, so it never reaches the IDC intercept. Retargeted here, which is
  // after chrome::BuildMainMenu and before any browser window exists.
  // Mac-only: the app menu itself, and InstallSettingsMenuHook()'s
  // implementation, are AppKit constructs (see atrium_settings_menu_hook.h).
#if BUILDFLAG(IS_MAC)
  atrium::InstallSettingsMenuHook();
#endif
  // the settings row for "Warn before quitting" had no consumer, so
  // it rendered, persisted and changed nothing. Local state exists by now -
  // PreCreateThreads built it - and the read below is async, so the pref is in
  // place well before a window is up to press Cmd+Q at.
  //
  // Sidebar position, the native-app surround and glass opacity sit
  // on the same read - three more rows that rendered and changed nothing, and
  // "Restore tabs on launch" and the hotkeys, the
  // whole Inputs pane's worth of the same complaint.
  SeedFromSettings();
  atrium::DeleteOrphanCustomEndpointApiKey();
  // Suppress Chromium's default startup window. Both branches below create
  // Atrium's own window(s) (ProfileBootstrap / CreateForInitialProfile), so
  // the stock startup window is a redundant extra - and worse, the
  // CollectionObserver above adopts it into a non-zero slot, which relaunch
  // then persists and restores as a phantom window. Launchers pass
  // --no-startup-window, but a bare .app double-click does not; forcing it
  // here covers both.
  base::CommandLine::ForCurrentProcess()->AppendSwitch(
      switches::kNoStartupWindow);
  // Belt-and-suspenders catch-all so any Atrium
  // Browser created OUTSIDE BrowserController's own self-creation paths
  // (bootstrap, Cmd+N) still gets a controller attached - closes the M2a
  // "unwired second window" debt for any FUTURE such path too. It never
  // fires for a Browser this class is already creating itself
  // (CreatingOwnBrowser() guard).
  // Mac-only: BrowserController itself has no portable implementation yet,
  // so there is no non-Mac
  // controller for this catch-all to attach, so there is nothing to skip
  // here, only a class that does not exist on this platform.
#if BUILDFLAG(IS_MAC)
  BrowserController::InstallCollectionObserver();
#endif
  // which profiles a launch needs comes from the spaces document
  // now. Bootstrap falls back to a single Default profile when the document
  // cannot be read, which is what every launch has done since the Swift cut,
  // so this is safe on a fresh profile as well as a restored one.
  ProfileBootstrap::Start();

  // started here rather than after the core opens - there is
  // no callback for that, and the timer's own is_open() check is what a
  // tick before the profile lands degrades to.
  archive_sweep_timer_.Start(
      FROM_HERE, kArchiveSweepInterval,
      base::BindRepeating(&AtriumMainExtraParts::RunArchiveSweep,
                          weak_factory_.GetWeakPtr()));
  wake_check_timer_.Start(
      FROM_HERE, kWakeCheckInterval,
      base::BindRepeating(&AtriumMainExtraParts::RunWakeSweep,
                          weak_factory_.GetWeakPtr()));
}

void AtriumMainExtraParts::RunArchiveSweep() {
  // The user's rung, read each tick so a settings save needs no relay here.
  // "twelveHours" is settings.rs's default for a file that has no value.
  const std::string threshold =
      AtriumBrowserWindow::ArchiveThresholdFromSettings(
          AtriumBrowserWindow::LastSettingsJson())
          .value_or("twelveHours");
  std::vector<std::string> swept =
      SweepArchivedTabIds(threshold.c_str(), ArchiveSweepInstantNow());
  if (swept.empty()) {
    return;
  }
  if (Browser* browser = BrowserForWake()) {
    auto* window = static_cast<AtriumBrowserWindow*>(browser->window());
    NotifyThenPushArchiveSweep(
        swept,
        base::BindOnce(&AtriumBrowserWindow::NotifyTabsArchived,
                        base::Unretained(window)),
        base::BindOnce(&AtriumBrowserWindow::PushSidebarSnapshot,
                        base::Unretained(window)));
  }
  // Only when something actually moved - a tick that found nothing due must
  // not schedule a write every 15 minutes forever.
  base::ThreadPool::PostTask(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
      base::BindOnce([] {
        if (atrium_core_save() < 0) {
          LOG(WARNING) << "Atrium: archive sweep save failed";
        }
      }));
}

void AtriumMainExtraParts::RunWakeSweep() {
  WakeDueSnoozedTabs(ArchiveSweepInstantNow());
}

void AtriumMainExtraParts::PostMainMessageLoopRun() {
  // Stopped before ProfileBootstrap's own teardown so a tick cannot land
  // mid-shutdown against a core that PrepareForShutdown is about to close.
  archive_sweep_timer_.Stop();
  wake_check_timer_.Stop();
  // Must run before BrowserProcessImpl::StartTearDown() destroys the
  // ProfileManager (later in this same shutdown phase) - see
  // ProfileBootstrap::PrepareForShutdown's doc. A no-op when no
  // ProfileBootstrap instance exists, which is every launch since the cut.
  ProfileBootstrap::PrepareForShutdown();
#if BUILDFLAG(IS_MAC)
  BrowserController::UninstallCollectionObserver();
#endif
  // the last save. Synchronous, unlike every gesture's, because
  // the run loop has already returned - a posted one would not be run. This
  // replaces the shutdown flush that went with UIBridge::WillTerminate.
  atrium_core_save();
}

}  // namespace atrium
