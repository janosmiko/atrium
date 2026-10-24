// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_settings_window.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SETTINGS_WINDOW_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SETTINGS_WINDOW_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/functional/callback_helpers.h"
#include "base/functional/function_ref.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "chrome/browser/shell_integration.h"
#include "chrome/browser/ui/atrium/atrium_settings_source.h"
#include "chrome/browser/ui/atrium/atrium_web_surface_widget.h"
#include "chrome/browser/ui/atrium/atrium_window_appearance.h"
#include "ui/views/widget/widget_observer.h"

class GURL;

namespace base {
class SequencedTaskRunner;
}

namespace content {
class WebContents;
}

namespace gfx {
class Point;
}

namespace network {
class SimpleHostResolver;
class SimpleURLLoader;
namespace mojom {
class URLLoaderFactory;
}  // namespace mojom
}  // namespace network

namespace views {
class Widget;
}  // namespace views

namespace atrium {

class AtriumBrowserWindow;
class DeferredProfileEngineApply;

// The page sends the WHOLE settings object on every save, so every field
// below carries a value whether or not its row is what the user touched.
struct SettingsApplyFields {
  std::optional<bool> confirm_to_quit;
  std::optional<bool> external_links_open_in_little;
  WindowAppearance appearance;
  std::optional<bool> memory_saver_enabled;
  std::optional<int> memory_saver_threshold_minutes;
  std::optional<std::string> notification_timeout;
  std::optional<std::pair<std::string, bool>> skylight;
  std::optional<bool> ai_enabled;
  std::optional<bool> auto_summarize;
  std::optional<bool> vim_mode_enabled;
  std::optional<std::string> translate_language;
  base::DictValue swipe_tuning;
  std::optional<bool> chrome_page_tint;
  std::optional<std::string> app_icon_variant;
};

// Defaults to all-false, so a field added here but not wired into
// DetermineSettingsApplyChanges reads as unchanged and is skipped rather than
// re-applied every save.
struct SettingsApplyChanges {
  bool confirm_to_quit = false;
  bool external_links_open_in_little = false;
  bool appearance = false;
  bool memory_saver_enabled = false;
  bool memory_saver_threshold_minutes = false;
  bool notification_timeout = false;
  bool skylight = false;
  bool ai_enabled = false;
  bool auto_summarize = false;
  bool vim_mode_enabled = false;
  bool translate_language = false;
  bool swipe_tuning = false;
  bool chrome_page_tint = false;
  bool app_icon_variant = false;
};

// `previous` is nullopt on the first save after launch, and every field due
// in `current` answers true.
SettingsApplyChanges DetermineSettingsApplyChanges(
    const SettingsApplyFields& current,
    const std::optional<SettingsApplyFields>& previous);

// Adds spaceCount, suggestEnabled, downloadDir and available to each entry
// of the document's `profiles` array. Returns the JSON the page is pushed.
// A free function so the browsertest can call it without a live window.
std::string WithProfileDetail(const std::string& spaces_json);

// The hotkeys file the Inputs pane's payload should write. Identical to it
// unless it names entries to forget, which the core drops so the shipped
// default applies again. Free for the same reason as WithProfileDetail.
std::string HotkeysFileAfterEdit(const std::string& edit_json);

// `file_json` with the shipped defaults merged in, which is the only shape
// NativeHotkeyMap::SetFromJSON can act on: it visits the actions a payload
// NAMES, and a reset works by removing one.
std::string HotkeysMergedForNativeMap(const std::string& file_json);

// Every hotkey preset's chord table, keyed by preset name - what the Inputs
// pane's picker applies. `{}` if the core declines to answer.
std::string HotkeyPresetTables();

// The Atrium settings window, hosting the /settings WebUI route.
//
// replaces the Swift settings window, which went with the ABI.
//
// ONE PER PROCESS, not one per browser window. That is what Cmd+, means on
// this platform - it brings the settings window forward rather than opening
// another - and it is why this does not follow CommandBarWidget's shape, which
// is parented to a browser window, covers it, and dies with it. A settings
// window outlives any single browser window and belongs to none of them.
//
// Portable by construction: no AppKit type appears here or in the
// implementation. The two seams that OPEN it are macOS-specific
// and live elsewhere.
class SettingsWebContentsDelegate;

class AtriumSettingsWindow : public AtriumWebSurfaceWidget,
                             public views::WidgetObserver {
 public:
  // The download-dir picker's platform dialog listener, which reports back
  // through the private OnProfileDownloadDirPicked below.
  friend class ProfileDownloadDirPicker;

  // Shows the settings window, creating it on first call and bringing the
  // existing one forward afterwards. A no-op before the reserved chrome
  // context exists, which is the same fail-soft posture the palette takes.
  // `section` names a pane (see settings.js's PANES); empty opens wherever the
  // page already was. An unknown or retired name resolves in the page rather
  // than here, so both callers of this and the rail agree on what it means.
  static void Show(const std::string& section = std::string());

  // Routes a deep link to the page. No-op for an empty section.
  void SelectPane(const std::string& section);

  // Whether the window is open. For tests and for the seams that want to
  // toggle rather than always raise.
  static bool IsShowing();

  // The System accent moves with no save behind it, and this window is not
  // one of SetWindowAppearance's live windows. No-op when nothing is open.
  static void RepaintOverlayTheme();

  // A space mutation from another window (the sidebar's icon picker, a
  // rename) has no observer here, so the caller must ask for a re-read.
  // No-op when nothing is open.
  static void RefreshSpaces();

  // Closes and destroys it. Safe when nothing is open.
  static void CloseForTesting();

  // The live instance, so a test can call OnSurfaceMessage directly instead
  // of driving JS through a loaded WebContents. Null when nothing is open.
  static AtriumSettingsWindow* ForTesting();

  // A bare instance with no widget or WebContents - Build() needs a live
  // AtriumChromeContext, which a unit test has none of. Only the network
  // probe below works on an instance made this way.
  static std::unique_ptr<AtriumSettingsWindow> CreateForTesting();

  // Set to observe what PushCustomEndpointConnectionResult sends to the
  // page - an instance made by CreateForTesting() has no WebContents to read
  // the pushed JS from.
  static void SetCustomEndpointConnectionResultObserverForTesting(
      base::RepeatingCallback<void(bool reachable, const std::string& error)>
          observer);

  // The page this window hosts, so a test can read what a push landed in it.
  // Null before Build() has run.
  content::WebContents* WebContentsForTesting();

  // The widget itself, so a test can reach its native window chrome. Null
  // before Build() has run.
  views::Widget* WidgetForTesting();

  // What NonClientHitTest answers at `point`, in the client view's own
  // coordinates - the only way a test can tell whether a point falls in the
  // page's own drag region without a titlebar to hand it hit-testing.
  int NonClientHitTestForTesting(const gfx::Point& point);

  // How many engine picks are still waiting on a load. A test asserts one per
  // profile - the pending count is the only place a superseded pick is
  // visible, since the winner's engine looks the same either way.
  size_t PendingEngineAppliesForTesting() const;

  // Starts the probe past the DNS SSRF guard - a unit test has no system
  // network context to resolve a host through.
  void StartCustomEndpointProbeForTesting(
      const GURL& url,
      const std::string& model,
      const std::optional<std::string>& api_key,
      network::mojom::URLLoaderFactory* loader_factory,
      int timeout_seconds = 0);

  AtriumSettingsWindow(const AtriumSettingsWindow&) = delete;
  AtriumSettingsWindow& operator=(const AtriumSettingsWindow&) = delete;

  // SurfaceHost:
  void OnSurfaceMessage(std::string_view name,
                        const base::ListValue& args) override;

  // views::WidgetObserver:
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;
  void OnWidgetThemeChanged(views::Widget* widget) override;
  void OnWidgetDestroying(views::Widget* widget) override;

  // Public so DeleteSoon can reach it; construction stays private, so Show()
  // is still the only way to make one.
  ~AtriumSettingsWindow() override;

 private:
  AtriumSettingsWindow();

  // Builds the widget and its contents. Returns false when the chrome context
  // is not ready yet, leaving nothing half-built.
  bool Build();

  // Mac only, for the sidebar vibrancy. A crash reload builds a new view that
  // forgets it, so every page ready re-applies it.
  void ClearPageBackground();

  // Sends the Rust core's settings to the page. A no-op before the route has
  // announced itself.
  void PushSettings();

  // `save_id` rides through to OnSettingsSaved so the page can match the
  // reply to the edit that sent it.
  void SaveSettings(std::string settings_json, int32_t save_id);

  // the pin. Deliberately not a settings key - it belongs to this
  // window, not to the profile, and a pin the user forgot they left on would
  // outlive the reason they set it.
  void SetAlwaysOnTop(bool floating);
  void PushAlwaysOnTop();

  // this window hosts the theme picker but owns no space tint of
  // its own, so it reads AtriumBrowserWindow::CurrentAppearance() rather
  // than being handed one.
  void PushOverlayTheme();

  // The reply from the thread-pool read.
  void OnSettingsLoaded(std::string json);
  void OnHotkeysLoaded(std::string json);
  void OnSpacesLoaded(int generation, std::string json);
  void OnRoutingLoaded(std::string json);

  // Posts PreviewImport for `source` in `dir` and replies to
  // OnImportPreviewed. Shared with OnImportRan, which re-previews after a
  // run so the pane's counts do not stay stale.
  void StartImportPreview(const std::string& source, const base::FilePath& dir);
  // Takes the core's JSON answer.
  void OnImportPreviewed(std::string source, std::string answer);
  // The run's reply. Static, so it lands after the window closes too: it arms
  // the saved routes, pushes every sidebar, then hands the outcome to the
  // window that is open now, if any. `settings_at_start` is the settings copy
  // when the run was posted.
  static void OnImportFinished(std::string settings_at_start,
                               ImportOutcome outcome);
  // ReadOutcomeHistory's reply: writes the batches, then hands off to
  // OnImportRan like OnImportFinished does for an outcome with no history.
  static void OnImportHistoryRead(ImportOutcome outcome);
  void OnImportRan(ImportOutcome outcome);
  // Re-renders every realised Atrium window's sidebar after an import.
  static void PushSidebarsEverywhere();

  // Re-reads routing.json and pushes it to the pane - the same trip
  // PushSettings makes on open.
  void PushRouting();

  // Writes an edited rule list back through the core, then arms the live
  // routing cache with what the write left on disk (the next matching
  // navigation, not the next relaunch).
  void SaveRouting(std::string routing_json);
  void OnRoutingSaved(std::string routing_json, int32_t code);

  // Writes an edited hotkeys map back through the core, then re-seeds the
  // live NativeHotkeyMap with what the write left on disk - a rebind acts on
  // the next matching key, not the next relaunch.
  void SaveHotkeys(std::string hotkeys_json);
  // `native_map_json` is the MERGED map, not the bytes written - see
  // HotkeysMergedForNativeMap for why the two differ after a reset.
  void OnHotkeysSaved(std::string native_map_json, int32_t code);

  // Re-reads spaces.json and pushes it, the same trip PushSettings makes on
  // open - used after a create, since the new space's id and order come from
  // the core rather than from anything this window already holds.
  void PushSpaces();

  // Writes the core's document, then pushes what the write left on disk.
  // `write_failed` is the warning logged when the write does not land, and
  // `then` runs after the push with whether the write landed.
  void SaveSpacesThenPush(
      const char* write_failed,
      base::OnceCallback<void(bool)> then = base::DoNothing());
  void OnSpacesSaved(const char* write_failed,
                     base::OnceCallback<void(bool)> then,
                     bool wrote);

  // Theme Studio's two edits. A null `spec` undoes the live preview for the
  // first and removes the stored theme for the second.
  void HandleThemePreview(const std::string& space_id, const base::Value* spec);
  void HandleThemeCommit(const std::string& space_id, const base::Value* spec);

  // The apply half of a save, chained behind the write so the window never
  // fades in a theme the file does not hold.
  void OnThemeSaved(const std::string& space_id, bool wrote);

  // Puts the stored theme back when the studio goes while a scrub is still
  // painted. Nothing else would - the preview never touched a file.
  void RevertLiveThemePreview();

  // The Profiles pane's two cross-profile actions: a data link opens in the
  // named profile's own window rather than this window's, and a folder pick
  // is a platform dialog with no synchronous result.
  void HandleProfileOpenPage(const std::string& profile_id,
                             const std::string& page);
  void NotifyPaneOfProfileFailure(const std::string& js_function,
                                  const std::string& profile_id);

  // Waits out a profile or a TemplateURLService that is still loading. A
  // second pick drops the first wait, because two of them race and the loser
  // lands its stale engine on top of the newer one the pane already shows.
  void ApplyProfileEngine(const std::string& profile_id,
                          const std::string& engine_id);
  void OnProfileEngineApplyFinished(const std::string& profile_id,
                                    bool applied);
  void HandleProfileDownloadDirPick(const std::string& profile_id);
  void OnProfileDownloadDirPicked(std::string profile_id, base::FilePath dir);

  // the API key lives only in the keychain, kept off the
  // settingsChanged round trip. Refuses outright while the kill switch is
  // off.
  void HandleCustomEndpointApiKeyChanged(std::string api_key);
  void OnCustomEndpointApiKeySet(bool ok);

  // Pushed on settingsReady, same as PushSettings' other blocking reads.
  void PushCustomEndpointApiKeyStatus();
  static void PushCustomEndpointApiKeyStatusToInstance();
  void OnCustomEndpointApiKeyStatusRead(bool has_key);

  // whether Apple on-device (Foundation Models) can actually run on
  // this Mac, so the Model row can show On-device as unavailable rather than
  // guessing. Kicked off on settingsReady, same as the API key status above.
  void CheckAiOnDeviceAvailability();
  void OnAiOnDeviceAvailabilityChecked(bool available);

  // Merges `ai_on_device_available_` into the last loaded settings and pushes
  // it - atriumSetSettings replaces rather than patches, so whichever of the
  // load and the availability check finishes second has to re-push both.
  void PushSettingsWithAvailability();

  // The General pane's Default browser row. The check runs on settingsReady
  // and again after a set, which the worker itself re-checks.
  void CheckDefaultBrowser();
  void OnDefaultBrowserChecked(shell_integration::DefaultWebClientState state);
  void HandleMakeDefaultBrowser();

  // The General pane's Hide sidebar switch: flips the sidebar of the window
  // that opened settings when its state differs from `hidden`.
  void HandleSidebarHiddenChanged(bool hidden);

  // the request-time SSRF guard on the custom endpoint's own
  // reachability probe. Redirects are refused outright rather than followed.
  void HandleCheckCustomEndpointConnection(std::string url,
                                           std::string model,
                                           int timeout_seconds);
  void OnCustomEndpointAddressChecked(GURL url,
                                      std::string model,
                                      int timeout_seconds,
                                      bool safe_to_connect);
  // The probe sends the same authenticated request the panel does, so it needs
  // the keychain key the panel gets in AtriumBrowserWindow.
  void OnCustomEndpointProbeApiKeyFetched(
      GURL url,
      std::string model,
      int timeout_seconds,
      network::mojom::URLLoaderFactory* loader_factory,
      std::optional<std::string> api_key);
  // The URLLoader half of the probe - its own function so
  // StartCustomEndpointProbeForTesting can drive it against a
  // TestURLLoaderFactory with no system network context.
  void StartCustomEndpointProbe(const GURL& url,
                                const std::string& model,
                                const std::optional<std::string>& api_key,
                                network::mojom::URLLoaderFactory* loader_factory,
                                int timeout_seconds);
  void OnCustomEndpointRedirectRefused();
  void PushCustomEndpointConnectionResult(bool reachable, std::string error);

  // What the write returned. Logs a different line per code, and tells the
  // page, because a failed save has to reach the user rather than the log.
  //
  // `confirm_to_quit` and `appearance` are what the saved object said about
  // the fields the browser itself has to act on, carried through the round
  // trip rather than held in members: two edits made faster than a write
  // completes would share a member and the older reply would apply the newer
  // value.
  void OnSettingsSaved(std::optional<bool> confirm_to_quit,
                       std::optional<bool> external_links_open_in_little,
                       WindowAppearance appearance,
                       std::optional<bool> memory_saver_enabled,
                       std::optional<int> memory_saver_threshold_minutes,
                       std::optional<std::string> notification_timeout,
                       std::optional<std::pair<std::string, bool>> skylight,
                       std::optional<bool> ai_enabled,
                       std::optional<bool> auto_summarize,
                       std::optional<bool> vim_mode_enabled,
                       std::optional<std::string> translate_language,
                       base::DictValue swipe_tuning,
                       std::optional<bool> chrome_page_tint,
                       std::optional<std::string> app_icon_variant,
                       int32_t save_id,
                       int32_t code);

  // Tracks the page's own drag region for NonClientHitTest. Dies before the
  // base class's web_contents_ - the destructor clears it as the delegate.
  std::unique_ptr<SettingsWebContentsDelegate> web_contents_delegate_;

  // Own the in-flight resolve and probe, so a second click cancels whichever
  // of these the first one left running rather than piling up.
  std::unique_ptr<network::SimpleHostResolver> custom_endpoint_resolver_;
  std::unique_ptr<network::SimpleURLLoader> custom_endpoint_probe_loader_;

  // A space to select once the page has the list.
  std::optional<int> pending_space_index_;

  // Bumped on every PushSpaces call and carried through its reply, so a read
  // started earlier that lands after a newer one is dropped instead of
  // overwriting fresher data.
  int spaces_generation_ = 0;

  // A pane asked for before the page could take it. A new window runs
  // SelectPane before settings.js defines its hook.
  std::optional<std::string> pending_pane_;

  // The window a theme preview painted, remembered because by the time a
  // close undoes it, the last-active browser may be an innocent other one.
  base::WeakPtr<AtriumBrowserWindow> preview_window_;

  // Survives a renderer reload, which is why the page asks rather than
  // remembers: this member is the window's z-order, and the page is told it.
  bool always_on_top_ = false;

  // The two halves PushSettingsWithAvailability merges. Members rather than
  // call arguments because the load and the availability check are two
  // independent async replies, on their own schedule.
  std::optional<std::string> last_settings_json_;
  std::optional<bool> ai_on_device_available_;
  // "yes", "no" or "unknown" once the default browser check answers.
  std::optional<std::string> default_browser_;

  // What OnSettingsSaved last pushed out, so the next save can skip a field
  // the file still carries but nobody changed. Nullopt until the first
  // successful write.
  std::optional<SettingsApplyFields> last_applied_settings_fields_;

  // At most one per profile - see ApplyProfileEngine.
  std::map<std::string, std::unique_ptr<DeferredProfileEngineApply>>
      pending_engine_applies_;

  base::WeakPtrFactory<AtriumSettingsWindow> weak_factory_{this};
};

// Whether the probe may attach the keychain API key. The probed URL comes from
// the page, so only the saved endpoint may get the key.
bool ProbeMayCarryApiKey(const GURL& probed_url, const std::string& saved_url);

// Whether a save must drop the keychain key. nullopt is a missing old file
// (first run). An unparsable old file or new JSON fails closed. An empty old
// URL counts when the new one is set: no legitimate key sits beside an empty
// URL, so a key found there is an orphan that must not follow the new URL.
bool SavedEndpointUrlChanged(
    const std::optional<std::string>& old_settings_file,
    const std::string& new_settings_json);

// Runs `write` unless the URL changed and `delete_key` failed. Fails closed so
// the old key never stays stored next to a new URL.
int32_t SaveSettingsClearingKey(
    const std::optional<std::string>& old_settings_file,
    const std::string& new_settings_json,
    base::FunctionRef<bool()> delete_key,
    base::FunctionRef<int32_t()> write);

// A key may only be stored next to a valid saved endpoint URL, so a key
// never sits beside an empty URL that a later save could point at a new
// host. Clearing (an empty key) is always allowed.
bool ApiKeyWriteAllowed(const std::string& api_key,
                        const std::optional<std::string>& settings_file);

// Whether startup must delete a key that has no valid saved URL. A file that
// exists but could not be read (`file_unreadable`) keeps the key, since the
// URL may be fine. `has_key` runs only when the URL is missing.
bool ShouldDeleteOrphanApiKey(const std::optional<std::string>& settings_file,
                              bool file_unreadable,
                              base::FunctionRef<bool()> has_key);

// Posts the orphan check to the settings write runner. Call once at startup.
void DeleteOrphanCustomEndpointApiKey();

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SETTINGS_WINDOW_H_
