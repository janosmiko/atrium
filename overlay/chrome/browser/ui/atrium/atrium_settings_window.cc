// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_settings_window.cc
#include "chrome/browser/ui/atrium/atrium_settings_window.h"

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/no_destructor.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "build/build_config.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_manager_observer.h"
#include "base/base_paths.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/json/string_escape.h"
#include "base/task/bind_post_task.h"
#include "base/task/thread_pool.h"
#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/net/system_network_context_manager.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_for_profile.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_chrome_context.h"
#include "chrome/browser/ui/atrium/atrium_custom_endpoint_request_guard.h"
#include "chrome/browser/ui/atrium/atrium_overlay_theme_script.h"
#include "chrome/browser/ui/atrium/atrium_profile_bootstrap.h"
#include "chrome/browser/ui/atrium/atrium_profile_page_url.h"
#include "chrome/browser/ui/atrium/atrium_profile_prefs.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/select_file_policy/chrome_select_file_policy.h"
#include "chrome/browser/ui/atrium/atrium_confirm_to_quit.h"
#include "chrome/browser/ui/atrium/atrium_external_link.h"
#include "chrome/browser/ui/atrium/atrium_import_history.h"
#include "chrome/browser/ui/atrium/atrium_memory_saver.h"
#include "chrome/browser/ui/atrium/atrium_search_engine_mirror.h"
#include "chrome/browser/ui/atrium/atrium_settings_source.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_model_source.h"
#include "chrome/browser/ui/atrium/atrium_spaces_order.h"
#include "chrome/browser/ui/atrium/atrium_traffic_routing.h"
#include "components/history/core/browser/history_service.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/search_engines/template_url_service.h"
#include "third_party/atrium/core/include/atrium_core.h"
#if BUILDFLAG(IS_MAC)
#include "chrome/browser/ui/atrium/atrium_ai_backend_mac.h"
#include "chrome/browser/ui/atrium/atrium_ai_backend_custom_endpoint.h"
#include "chrome/browser/ui/atrium/atrium_custom_endpoint_keychain_mac.h"
#include "chrome/browser/ui/atrium/atrium_native_hotkey_map.h"
#include "chrome/browser/ui/atrium/atrium_settings_titlebar_mac.h"
#include "chrome/browser/ui/atrium/atrium_surface_quit_route_mac.h"
#endif  // BUILDFLAG(IS_MAC)
#include "content/public/browser/render_frame_host.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "chrome/common/webui_url_constants.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "third_party/blink/public/mojom/page/draggable_region.mojom.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkRegion.h"
#include "net/base/address_list.h"
#include "net/base/host_port_pair.h"
#include "net/base/ip_address.h"
#include "net/base/ip_endpoint.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/http/http_request_headers.h"
#include "net/base/network_anonymization_key.h"
#include "net/dns/public/resolve_error_info.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_host_resolver.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "services/network/public/mojom/host_resolver.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "ui/base/hit_test.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/ui_base_types.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "ui/color/color_provider_key.h"
#include "ui/gfx/color_utils.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/shell_dialogs/selected_file_info.h"
#include "ui/views/controls/webview/webview.h"
#include "components/input/native_web_keyboard_event.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/window/client_view.h"
#include "ui/views/window/non_client_view.h"
#include "url/gurl.h"

namespace atrium {

// Tracks the page's own -webkit-app-region regions (settings.css's
// .settings-drag-region) for SettingsClientView's NonClientHitTest below.
// Subclasses the context's key forwarder rather than replacing it, so
// unhandled keys still reach the window. Namespace-scope, not anonymous -
// the header's unique_ptr needs the concrete (publicly destructible) type,
// since WebContentsDelegate's own destructor is protected.
class SettingsWebContentsDelegate : public ChromeSurfaceKeyForwarder {
 public:
  void DraggableRegionsChanged(
      const std::vector<blink::mojom::DraggableRegionPtr>& regions,
      content::WebContents* contents) override {
    SkRegion region;
    for (const blink::mojom::DraggableRegionPtr& r : regions) {
      region.op(SkIRect::MakeXYWH(r->bounds.x(), r->bounds.y(),
                                  r->bounds.width(), r->bounds.height()),
                r->draggable ? SkRegion::kUnion_Op
                             : SkRegion::kDifference_Op);
    }
    draggable_region_.swap(region);
  }

  const SkRegion& draggable_region() const { return draggable_region_; }

  // Escape closes the window. Answered here and not by a view accelerator:
  // on Mac the forwarder redispatches the native NSEvent, which a synthetic
  // key has none of, so an accelerator never sees it under test.
  bool HandleKeyboardEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event) override {
    if (event.GetType() == blink::WebInputEvent::Type::kRawKeyDown &&
        event.windows_key_code == ui::VKEY_ESCAPE) {
      if (views::Widget* widget = views::Widget::GetTopLevelWidgetForNativeView(
              source->GetNativeView())) {
        widget->Close();
      }
      return true;
    }
    return ChromeSurfaceKeyForwarder::HandleKeyboardEvent(source, event);
  }

 private:
  SkRegion draggable_region_;
};

namespace {

// The one instance. A raw owning pointer rather than a NoDestructor, because
// this is created on demand and destroyed when its window closes - a
// NoDestructor would keep a dead shell around after the first close.
AtriumSettingsWindow*& Instance() {
  static AtriumSettingsWindow* instance = nullptr;
  return instance;
}

// Storage for SetCustomEndpointConnectionResultObserverForTesting - a unit
// test instance has no WebContents for PushCustomEndpointConnectionResult to
// push into.
base::RepeatingCallback<void(bool, const std::string&)>&
CustomEndpointConnectionResultObserver() {
  static base::NoDestructor<
      base::RepeatingCallback<void(bool, const std::string&)>>
      observer;
  return *observer;
}

constexpr int kInitialWidth = 860;
constexpr int kInitialHeight = 620;

// SimpleURLLoader has no default timeout - without this a hung endpoint
// leaves the Test connection button disabled forever.
constexpr base::TimeDelta kCustomEndpointProbeTimeout = base::Seconds(10);

// The probe spends the user's own tokens, so it asks for the shortest answer
// that still proves the endpoint serves chat completions.
constexpr char kProbePageText[] = "ping";

// A connection check only has to see chat-completions JSON come back, so it
// caps the answer. Uncapped, a 550B reasoning model spent 105 s writing about
// the word ping and the probe timed out on a working endpoint.
constexpr int kProbeMaxTokens = 64;

// Same backstop as the summarize path's kMaxCustomEndpointResponseBytes.
constexpr size_t kMaxCustomEndpointProbeResponseBytes = 1024 * 1024;

// the browser window a page opened from the settings window should
// land in.
//
// The settings window belongs to no browser - it is one per process, and its
// WebContents renders in the system profile, which allows no browser windows
// at all (AtriumChromeContext). So it cannot open a tab in "its" window the
// way the sidebar does; it has to find one.
//
// Realised-window guard for the same reason OpenCommandBarMatch has one: a tab
// added to a Browser with no native window aborts the browser process
//. Null when the app is running with every window closed, which is
// a normal state for Atrium and simply means the click does nothing.
Browser* BrowserForOpenedPage() {
  BrowserWindowInterface* last =
      GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser();
  Browser* browser = last ? last->GetBrowserForMigrationOnly() : nullptr;
  if (!browser || !BrowserController::ControllerForBrowser(browser)) {
    return nullptr;  // Not an Atrium-managed window.
  }
  return AtriumBrowserWindow::HasRealisedWindow(browser) ? browser : nullptr;
}

// The controller of the window a settings edit acts on. Same guard as
// BrowserForOpenedPage: null is a normal state, not a failure.
BrowserController* ControllerForOpenedPage() {
  Browser* browser = BrowserForOpenedPage();
  return browser ? BrowserController::ControllerForBrowser(browser) : nullptr;
}

// Whether the core still has a space with this id. atrium_core_space_theme
// cannot answer it: NULL there means a space with no theme, not an unknown
// one, so a bogus id would read as themeless and be acted on.
bool IsKnownSpaceId(const std::string& space_id) {
  char* raw = atrium_core_space_slot_for_id(space_id.c_str());
  if (!raw) {
    return false;
  }
  atrium_core_string_free(raw);
  return true;
}

// Takes ownership of a string the core minted and parses it. A null pointer
// and unreadable JSON both read as no theme, which serializes as null.
base::Value ThemeValue(char* raw) {
  if (!raw) {
    return base::Value();
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(raw, base::JSON_PARSE_RFC);
  atrium_core_string_free(raw);
  return parsed && parsed->is_dict() ? std::move(*parsed) : base::Value();
}

// settings.json is one file for the whole app, so every loaded profile's
// service has to take the list. The last-active window's profile is the
// fallback for the early startup window where no manager exists yet.
void MirrorEnginesEverywhere(const std::vector<MirroredEngine>& engines) {
  if (ProfileManager* manager = g_browser_process->profile_manager()) {
    for (Profile* profile : manager->GetLoadedProfiles()) {
      if (TemplateURLService* service =
              TemplateURLServiceFactory::GetForProfile(profile)) {
        MirrorSearchEngines(service, engines);
      }
    }
    return;
  }
  if (Browser* browser = BrowserForOpenedPage()) {
    MirrorSearchEngines(
        TemplateURLServiceFactory::GetForProfile(browser->profile()), engines);
  }
}

// The web_view_ fills the whole ClientView (FillLayout), so `point` is
// already in its coordinate space and needs no conversion.
class SettingsClientView : public views::ClientView {
  METADATA_HEADER(SettingsClientView, views::ClientView)

 public:
  SettingsClientView(views::Widget* widget,
                     views::View* contents_view,
                     SettingsWebContentsDelegate* delegate)
      : ClientView(widget, contents_view), delegate_(delegate) {}

  int NonClientHitTest(const gfx::Point& point) override {
    if (!delegate_) {
      return views::ClientView::NonClientHitTest(point);
    }
    if (delegate_->draggable_region().contains(point.x(), point.y())) {
      return HTCAPTION;
    }
    return ClientView::NonClientHitTest(point);
  }

  // Called from ~AtriumSettingsWindow, which destroys delegate_'s owner
  // before the base widget_ (and this view) tear down.
  void ClearDelegate() { delegate_ = nullptr; }

 private:
  raw_ptr<SettingsWebContentsDelegate> delegate_;
};

BEGIN_METADATA(SettingsClientView)
END_METADATA

// The core refuses an engine id it cannot parse and answers "unchanged" for
// the one it already holds. Only the stored value tells those two apart, and
// only the refusal must skip the live apply.
bool StoredProfileEngineIs(const std::string& profile_id,
                           const std::string& engine) {
  char* stored = atrium_core_profile_search_engine(profile_id.c_str());
  const bool same = stored && engine == stored;
  if (stored) {
    atrium_core_string_free(stored);
  }
  return same;
}

// A profile that the import created has never loaded, and a profile has no
// HistoryService until it loads. CreateProfileAsync answers at once for a
// loaded one.
void WriteImportedHistory(ImportedHistory batch) {
  ProfileManager* manager = g_browser_process->profile_manager();
  if (!manager) {
    return;
  }
  if (!ProfileBootstrap::IsSafeProfileId(batch.profile_id)) {
    VLOG(1) << "Atrium: settings names an unknown profile";
    return;
  }
  const base::FilePath path =
      manager->user_data_dir().AppendASCII(batch.profile_id);
  manager->CreateProfileAsync(
      path, base::BindOnce(
                [](ImportedHistory batch, Profile* profile) {
                  history::HistoryService* service =
                      profile ? HistoryServiceFactory::GetForProfile(
                                    profile, ServiceAccessType::EXPLICIT_ACCESS)
                              : nullptr;
                  if (!service) {
                    LOG(WARNING) << "Atrium: import history found no history "
                                    "service for profile "
                                 << batch.profile_id;
                    return;
                  }
                  const history::VisitSource source = batch.visit_source;
                  AddImportedHistory(
                      service, std::move(batch.rows), source,
                      base::BindOnce(
                          [](const std::string& id, size_t added) {
                            VLOG(1) << "Atrium: import history added " << added
                                    << " visits to profile " << id;
                          },
                          batch.profile_id));
                },
                std::move(batch)));
}

// nullopt when the JSON is not an object. A missing field reads as "", as in
// the core.
std::optional<std::string> SavedEndpointUrlFromJson(const std::string& json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  const std::string* url = parsed->GetDict().FindString("customEndpointUrl");
  return url ? *url : std::string();
}

bool DeleteCustomEndpointApiKey() {
#if BUILDFLAG(IS_MAC)
  return SetCustomEndpointApiKey("");
#else
  return true;
#endif
}

// nullopt when the file is missing. A file that cannot be read comes back
// empty, which no caller treats as a valid URL, and sets `unreadable` for the
// caller that must not act on a read error.
std::optional<std::string> ReadSettingsFileRaw(const base::FilePath& path,
                                               bool* unreadable = nullptr) {
  if (!base::PathExists(path)) {
    return std::nullopt;
  }
  std::string contents;
  const bool ok = base::ReadFileToString(path, &contents);
  if (unreadable) {
    *unreadable = !ok;
  }
  return contents;
}

bool HasSavedEndpointUrl(const std::optional<std::string>& settings_file) {
  const std::optional<std::string> url =
      settings_file ? SavedEndpointUrlFromJson(*settings_file) : std::nullopt;
  if (!url) {
    return false;
  }
  const GURL parsed(*url);
  return parsed.is_valid() && parsed.SchemeIs(url::kHttpsScheme);
}

#if BUILDFLAG(IS_MAC)
// Runs on SettingsWriteRunner, in sequence with the settings writes, so the
// check sees the URL those writes left on disk.
bool StoreCustomEndpointApiKey(std::string api_key) {
  if (!ApiKeyWriteAllowed(api_key,
                          ReadSettingsFileRaw(ResolveSettingsFilePath()))) {
    return false;
  }
  return SetCustomEndpointApiKey(api_key);
}
#endif

// The one path every settings write that can move the URL goes through. Runs
// on SettingsWriteRunner, in sequence with the key write.
int32_t SaveSettingsFailClosed(const std::string& json,
                               base::OnceClosure key_cleared) {
  const base::FilePath path = ResolveSettingsFilePath();
  const std::optional<std::string> old_file = ReadSettingsFileRaw(path);
  return SaveSettingsClearingKey(
      old_file, json,
      [&] {
        const bool deleted = DeleteCustomEndpointApiKey();
        if (deleted) {
          std::move(key_cleared).Run();
        }
        return deleted;
      },
      [&] { return SaveSettingsJSON(path, json); });
}

}  // namespace

// Waits out a profile or a TemplateURLService that is still loading, then
// retries once and reports only if that missed too. Mirrors first, because the
// picked engine is an `atrium:` GUID only the mirror adds.
class DeferredProfileEngineApply : public ProfileManagerObserver {
 public:
  DeferredProfileEngineApply(const std::string& profile_id,
                             const std::string& engine_id,
                             base::OnceCallback<void(bool)> done)
      : profile_id_(profile_id),
        engine_id_(engine_id),
        done_(std::move(done)) {
    if (ProfileManager* profile_manager =
            g_browser_process->profile_manager()) {
      profile_manager_observation_.Observe(profile_manager);
    }
    // A load that never arrives still owes the pane an answer.
    deadline_.Start(FROM_HERE, base::Seconds(30),
                    base::BindOnce(&DeferredProfileEngineApply::Finish,
                                   base::Unretained(this), false));
  }

  ~DeferredProfileEngineApply() override = default;

  DeferredProfileEngineApply(const DeferredProfileEngineApply&) = delete;
  DeferredProfileEngineApply& operator=(const DeferredProfileEngineApply&) =
      delete;

  // Separate from the constructor so the owner can store this first: the very
  // first attempt can already finish, and finishing hands the owner a delete.
  void Start() { Try(); }

  // ProfileManagerObserver:
  void OnProfileAdded(Profile* profile) override {
    if (profile->GetBaseName().AsUTF8Unsafe() == profile_id_) {
      PostTry();
    }
  }

  void OnProfileManagerDestroying() override { Finish(false); }

 private:
  void PostTry() {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&DeferredProfileEngineApply::Try,
                                  weak_factory_.GetWeakPtr()));
  }

  void Try() {
    if (finished_) {
      return;
    }
    Profile* profile = LoadedProfileForId(profile_id_);
    if (!profile) {
      return;
    }
    TemplateURLService* service =
        TemplateURLServiceFactory::GetForProfile(profile);
    if (!service) {
      Finish(false);
      return;
    }
    if (!service->loaded()) {
      if (!service_loaded_subscription_) {
        // Posted, never run inline: this fires inside the service's own
        // notification, and the mirror's Add/Remove calls from there trip the
        // re-entrancy CHECK in base/observer_list.h.
        service_loaded_subscription_ = service->RegisterOnLoadedCallback(
            base::BindOnce(&DeferredProfileEngineApply::PostTry,
                           weak_factory_.GetWeakPtr()));
        service->Load();
      }
      return;
    }
    if (const std::optional<std::vector<MirroredEngine>> engines =
            SearchEnginesFromSettings(
                AtriumBrowserWindow::LastSettingsJson())) {
      MirrorSearchEngines(service, *engines);
    }
    Finish(SetProfileDefaultSearchEngine(profile_id_, engine_id_));
  }

  void Finish(bool applied) {
    finished_ = true;
    deadline_.Stop();
    profile_manager_observation_.Reset();
    service_loaded_subscription_ = {};
    if (!applied) {
      LOG(WARNING) << "Atrium: profile '" << profile_id_
                   << "' never took the picked search engine";
    }
    // Through this object's own weak pointer, so a newer pick that drops this
    // one also drops the report it was about to make.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&DeferredProfileEngineApply::Report,
                                  weak_factory_.GetWeakPtr(), applied));
  }

  // The owner drops this object from here, so nothing may touch a member
  // after the call.
  void Report(bool applied) { std::move(done_).Run(applied); }

  const std::string profile_id_;
  const std::string engine_id_;
  base::OnceCallback<void(bool)> done_;
  bool finished_ = false;
  base::OneShotTimer deadline_;
  base::CallbackListSubscription service_loaded_subscription_;
  base::ScopedObservation<ProfileManager, ProfileManagerObserver>
      profile_manager_observation_{this};
  base::WeakPtrFactory<DeferredProfileEngineApply> weak_factory_{this};
};

void AtriumSettingsWindow::ApplyProfileEngine(const std::string& profile_id,
                                              const std::string& engine_id) {
  // Before the inline attempt too: an earlier wait that outlived its pick
  // would otherwise land its engine after this one.
  pending_engine_applies_.erase(profile_id);
  if (SetProfileDefaultSearchEngine(profile_id, engine_id)) {
    return;
  }
  auto& slot = pending_engine_applies_[profile_id];
  slot = std::make_unique<DeferredProfileEngineApply>(
      profile_id, engine_id,
      base::BindOnce(&AtriumSettingsWindow::OnProfileEngineApplyFinished,
                     weak_factory_.GetWeakPtr(), profile_id));
  slot->Start();
}

void AtriumSettingsWindow::OnProfileEngineApplyFinished(
    const std::string& profile_id, bool applied) {
  pending_engine_applies_.erase(profile_id);
  if (!applied) {
    NotifyPaneOfProfileFailure("atriumSetProfileEngineApplyFailed", profile_id);
  }
}

size_t AtriumSettingsWindow::PendingEngineAppliesForTesting() const {
  return pending_engine_applies_.size();
}

// Self-deletes on either callback. Namespace-scope, not anonymous, so the
// header's friend declaration names this same type.
class ProfileDownloadDirPicker : public ui::SelectFileDialog::Listener {
 public:
  ProfileDownloadDirPicker(std::string profile_id,
                           base::WeakPtr<AtriumSettingsWindow> window)
      : profile_id_(std::move(profile_id)), window_(std::move(window)) {}

  void Show(gfx::NativeWindow owning_window,
           content::WebContents* policy_contents) {
    dialog_ = ui::SelectFileDialog::Create(
        this, std::make_unique<ChromeSelectFilePolicy>(policy_contents));
    dialog_->SelectFile(ui::SelectFileDialog::SELECT_EXISTING_FOLDER,
                        std::u16string(), base::FilePath(),
                        /*file_types=*/nullptr, /*file_type_index=*/0,
                        base::FilePath::StringType(), owning_window,
                        /*caller=*/nullptr);
  }

 private:
  // ui::SelectFileDialog::Listener:
  void FileSelected(const ui::SelectedFileInfo& file, int index) override {
    if (AtriumSettingsWindow* window = window_.get()) {
      window->OnProfileDownloadDirPicked(profile_id_, file.path());
    }
    delete this;
  }
  void FileSelectionCanceled() override { delete this; }

  std::string profile_id_;
  base::WeakPtr<AtriumSettingsWindow> window_;
  scoped_refptr<ui::SelectFileDialog> dialog_;
};

std::string WithProfileDetail(const std::string& spaces_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(spaces_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return spaces_json;
  }
  base::DictValue& root = parsed->GetDict();
  base::ListValue* profiles = root.FindList("profiles");
  if (!profiles) {
    return spaces_json;
  }
  const base::ListValue* spaces = root.FindList("spaces");
  for (base::Value& entry : *profiles) {
    if (!entry.is_dict()) {
      continue;
    }
    base::DictValue& profile = entry.GetDict();
    const std::string* id = profile.FindString("id");
    if (!id) {
      continue;
    }
    int space_count = 0;
    if (spaces) {
      for (const base::Value& space : *spaces) {
        const std::string* space_profile_id =
            space.is_dict() ? space.GetDict().FindString("profileID")
                            : nullptr;
        if (space_profile_id && *space_profile_id == *id) {
          ++space_count;
        }
      }
    }
    profile.Set("spaceCount", space_count);
    profile.Set("available", LoadedProfileForId(*id) != nullptr);
    // Skipped rather than false/empty when the getter cannot answer, so the
    // page can tell "off" from "cannot say".
    if (std::optional<bool> suggest = ProfileSuggestEnabled(*id)) {
      profile.Set("suggestEnabled", *suggest);
    }
    if (std::optional<base::FilePath> dir = ProfileDownloadDir(*id)) {
      profile.Set("downloadDir", dir->AsUTF8Unsafe());
    }
  }
  std::string out;
  base::JSONWriter::Write(*parsed, &out);
  return out;
}

// The themes live in their own store, so spaces.json carries none of this. A
// space with no theme gets a one-stop draft off its tint, which is what makes
// its first Save change nothing visible.
std::string WithSpaceThemes(const std::string& spaces_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(spaces_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return spaces_json;
  }
  base::ListValue* spaces = parsed->GetDict().FindList("spaces");
  if (!spaces) {
    return spaces_json;
  }
  for (base::Value& entry : *spaces) {
    if (!entry.is_dict()) {
      continue;
    }
    base::DictValue& space = entry.GetDict();
    const std::string* id = space.FindString("id");
    if (!id) {
      continue;
    }
    base::Value stored = ThemeValue(atrium_core_space_theme(id->c_str()));
    const std::string* tint = space.FindString("tint");
    base::Value draft =
        stored.is_none() && tint
            ? ThemeValue(atrium_core_single_stop_theme(tint->c_str()))
            : stored.Clone();
    space.Set("theme", std::move(stored));
    space.Set("themeDraft", std::move(draft));
  }
  std::string out;
  base::JSONWriter::Write(*parsed, &out);
  return out;
}

// static
void AtriumSettingsWindow::Show(const std::string& section) {
  if (Instance()) {
    Instance()->widget_->Show();
    Instance()->widget_->Activate();
    Instance()->SelectPane(section);
    return;
  }
  auto* window = new AtriumSettingsWindow();
  if (!window->Build()) {
    // The reserved chrome context lands a little after startup. Refusing here
    // rather than showing an empty frame; the next Cmd+, builds it.
    delete window;
    LOG(INFO) << "atrium: settings has no chrome context yet";
    return;
  }
  Instance() = window;
  window->widget_->Show();
  window->widget_->Activate();
  window->SelectPane(section);
}

void AtriumSettingsWindow::SelectPane(const std::string& section) {
  if (section.empty() || !web_contents_) {
    return;
  }
  if (!renderer_ready_) {
    pending_pane_ = section;
    return;
  }
  std::string json;
  base::JSONWriter::Write(base::Value(section), &json);
  VLOG(1) << "Atrium: settings opening on pane " << section;
  // JSON.parse rather than interpolation, the form every other push in the
  // tree uses. Nothing here enforces that the payload stays a JSON document,
  // and this executes at browser privilege.
  const std::u16string script =
      u"window.atriumSelectSettingsPane && atriumSelectSettingsPane(JSON.parse(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(json)) + u"))";
  web_contents_->GetPrimaryMainFrame()->ExecuteJavaScript(script,
                                                          base::NullCallback());
}

// static
bool AtriumSettingsWindow::IsShowing() {
  return Instance() != nullptr;
}

// static
void AtriumSettingsWindow::CloseForTesting() {
  if (Instance()) {
    Instance()->widget_->CloseNow();
  }
}

// static
void AtriumSettingsWindow::RepaintOverlayTheme() {
  if (Instance()) {
    Instance()->PushOverlayTheme();
  }
}

// static
void AtriumSettingsWindow::RefreshSpaces() {
  // The caller's own save already landed (it runs this as that save's reply),
  // so this only has to push - a second atrium_core_save here would write the
  // same edit twice.
  if (Instance()) {
    Instance()->PushSpaces();
  }
}

// static
AtriumSettingsWindow* AtriumSettingsWindow::ForTesting() {
  return Instance();
}

// static
std::unique_ptr<AtriumSettingsWindow> AtriumSettingsWindow::CreateForTesting() {
  return base::WrapUnique(new AtriumSettingsWindow());
}

// static
void AtriumSettingsWindow::SetCustomEndpointConnectionResultObserverForTesting(
    base::RepeatingCallback<void(bool, const std::string&)> observer) {
  CustomEndpointConnectionResultObserver() = std::move(observer);
}

content::WebContents* AtriumSettingsWindow::WebContentsForTesting() {
  return web_contents_.get();
}

views::Widget* AtriumSettingsWindow::WidgetForTesting() {
  return widget_.get();
}

int AtriumSettingsWindow::NonClientHitTestForTesting(const gfx::Point& point) {
  return widget_->non_client_view()->client_view()->NonClientHitTest(point);
}

AtriumSettingsWindow::AtriumSettingsWindow() = default;

AtriumSettingsWindow::~AtriumSettingsWindow() {
  // web_contents_delegate_ dies before the base dtor tears down web_contents_
  // and widget_'s SettingsClientView, which otherwise keeps a dangling
  // pointer to it.
  if (web_contents_) {
    web_contents_->SetDelegate(nullptr);
  }
  if (widget_ && widget_->non_client_view() &&
      widget_->non_client_view()->client_view()) {
    static_cast<SettingsClientView*>(widget_->non_client_view()->client_view())
        ->ClearDelegate();
  }
}

bool AtriumSettingsWindow::Build() {
  AtriumChromeContext* context = AtriumChromeContext::GetIfExists();
  if (!context) {
    return false;
  }
  web_contents_ = context->CreateChromeContents(
      GURL(std::string(chrome::kChromeUIAtriumURL).append("settings")));
  if (!web_contents_) {
    return false;
  }
  AttachToContents(web_contents_.get());

  // Overrides the context's default key-forwarding delegate to also track
  // the page's own drag region (SettingsWebContentsDelegate above).
  auto delegate = std::make_unique<SettingsWebContentsDelegate>();
  SettingsWebContentsDelegate* drag_delegate = delegate.get();
  web_contents_->SetDelegate(drag_delegate);
  web_contents_->SetSupportsDraggableRegions(true);
  web_contents_delegate_ = std::move(delegate);

  widget_ = std::make_unique<views::Widget>();
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW);
  params.name = "AtriumSettings";
  params.bounds = gfx::Rect(0, 0, kInitialWidth, kInitialHeight);
#if BUILDFLAG(IS_MAC)
  // Lets the sidebar vibrancy show through the page's transparent sidebar.
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
#endif

  widget_delegate_ = std::make_unique<views::WidgetDelegate>();
  widget_delegate_->SetCanActivate(true);
  widget_delegate_->SetCanResize(true);
  widget_delegate_->SetTitle(u"Atrium Settings");
  // No titlebar (below), so the page's own drag region is the window's only
  // drag handle - see SettingsClientView::NonClientHitTest.
  widget_delegate_->SetClientViewFactory(base::BindOnce(
      [](SettingsWebContentsDelegate* drag_delegate, views::Widget* widget,
        views::View* contents_view) -> std::unique_ptr<views::ClientView> {
        return std::make_unique<SettingsClientView>(widget, contents_view,
                                                    drag_delegate);
      },
      drag_delegate));
  auto container = std::make_unique<views::View>();
  container->SetLayoutManager(std::make_unique<views::FillLayout>());
  contents_container_ = widget_delegate_->SetContentsView(std::move(container));

  // built and named as the initially focused view BEFORE Init,
  // because SetInitiallyFocusedView refuses a delegate that already has a
  // widget.
  //
  // Without it the window opens with no focused view at all, and that is what
  // swallowed the keyboard: FocusManager::OnKeyEvent claims Tab for focus
  // traversal when nothing is focused, and a plain arrow reaches no one -
  // BridgedContentView is still first responder, so the page's own
  // RenderWidgetHostViewCocoa never sees the key. Naming the WebView here puts
  // native focus on the page when the window opens, and from there
  // WebView::SkipDefaultKeyEventProcessing leaves Tab and the arrows to the
  // page.
  auto web_view = std::make_unique<views::WebView>(
      Profile::FromBrowserContext(context->context()));
  web_view->SetWebContents(web_contents_.get());
  web_view_ = contents_container_->AddChildView(std::move(web_view));
  widget_delegate_->SetInitiallyFocusedView(web_view_);

  // Kept in the unique_ptr rather than handed to the widget, the same as
  // CommandBarWidget: this object outlives the widget's own teardown.
  params.delegate = widget_delegate_.get();

  widget_->Init(std::move(params));

  // this window hosts a live page, so its RenderWidgetHostViewCocoa
  // claims every key equivalent and answers the renderer asynchronously. Cmd+Q
  // therefore never reached the app main menu and the process outlived the
  // chord. The route has to leave before the web content; see
  // atrium_surface_quit_route_mac.h.
#if BUILDFLAG(IS_MAC)
  InstallQuitRouteOnSurfaceWindow(widget_.get());
  RemoveSettingsWindowTitlebar(widget_.get());
  InstallSettingsWindowVibrancy(widget_.get());
  ClearPageBackground();
#endif

  widget_->AddObserver(this);
  widget_->CenterWindow(gfx::Size(kInitialWidth, kInitialHeight));

  VLOG(1) << "Atrium: settings window built";
  return true;
}

void AtriumSettingsWindow::OnSurfaceMessage(std::string_view name,
                                            const base::ListValue& args) {
  if (name == "settingsReady") {
    renderer_ready_ = true;
    VLOG(1) << "Atrium: settings surface ready";
    ClearPageBackground();
    PushSettings();
    PushRouting();
    PushCustomEndpointApiKeyStatus();
    PushAlwaysOnTop();
    PushOverlayTheme();
    CheckAiOnDeviceAvailability();
    CheckDefaultBrowser();
    if (pending_pane_) {
      const std::string pane = std::move(*pending_pane_);
      pending_pane_.reset();
      SelectPane(pane);
    }
    return;
  }
  if (name == "settingsSidebarHiddenChanged") {
    if (args.empty() || !args[0].is_bool()) {
      LOG(WARNING) << "Atrium: settingsSidebarHiddenChanged carried no bool";
      return;
    }
    HandleSidebarHiddenChanged(args[0].GetBool());
    return;
  }
  if (name == "settingsMakeDefaultBrowser") {
    HandleMakeDefaultBrowser();
    return;
  }
  if (name == "settingsAlwaysOnTopChanged") {
    if (args.empty() || !args[0].is_bool()) {
      LOG(WARNING) << "Atrium: settingsAlwaysOnTopChanged carried no bool";
      return;
    }
    SetAlwaysOnTop(args[0].GetBool());
    return;
  }
  if (name == "settingsChanged") {
    if (args.size() < 2 || !args[0].is_string() || !args[1].is_int()) {
      LOG(WARNING) << "Atrium: settingsChanged carried no settings object "
                      "or save id";
      return;
    }
    SaveSettings(args[0].GetString(), args[1].GetInt());
    return;
  }
  if (name == "settingsRoutingRulesChanged") {
    if (args.empty() || !args[0].is_string()) {
      LOG(WARNING) << "Atrium: settingsRoutingRulesChanged carried no "
                      "routing object";
      return;
    }
    SaveRouting(args[0].GetString());
    return;
  }
  if (name == "settingsHotkeysChanged") {
    if (args.empty() || !args[0].is_string()) {
      LOG(WARNING) << "Atrium: settingsHotkeysChanged carried no hotkeys "
                      "object";
      return;
    }
    SaveHotkeys(args[0].GetString());
    return;
  }
  if (name == "settingsCustomEndpointApiKeyChanged") {
    if (args.empty() || !args[0].is_string()) {
      LOG(WARNING) << "Atrium: settingsCustomEndpointApiKeyChanged carried no "
                      "key string";
      return;
    }
    HandleCustomEndpointApiKeyChanged(args[0].GetString());
    return;
  }
  if (name == "settingsCheckCustomEndpointConnection") {
    if (args.empty() || !args[0].is_string()) {
      LOG(WARNING) << "Atrium: settingsCheckCustomEndpointConnection carried "
                      "no URL string";
      return;
    }
    HandleCheckCustomEndpointConnection(
        args[0].GetString(),
        args.size() > 1 && args[1].is_string() ? args[1].GetString()
                                               : std::string(),
        args.size() > 2 && args[2].is_int() ? args[2].GetInt() : 0);
    return;
  }
  if (name == "settingsOpenCredits") {
    Browser* into = BrowserForOpenedPage();
    if (!into) {
      VLOG(1) << "Atrium: licenses click refused - no realised Atrium window";
      return;
    }
    // The URL is fixed here rather than carried in the message: the renderer
    // asks for the licenses page, it does not get to name a destination.
    chrome::AddTabAt(into, GURL(chrome::kChromeUICreditsURL), -1,
                     /*foreground=*/true);
    return;
  }
  if (name == "settingsSpaceArchiveTtlChanged") {
    if (args.size() < 2 || !args[0].is_int() || !args[1].is_string()) {
      LOG(WARNING) << "Atrium: settingsSpaceArchiveTtlChanged carried bad "
                      "arguments";
      return;
    }
    const std::string ttl = args[1].GetString();
    // Null rather than an empty C string reads the same way to the core, but
    // an empty c_str() is never null - the two have to be told apart here.
    const bool changed = atrium_core_set_space_archive_ttl(
        args[0].GetInt(), ttl.empty() ? nullptr : ttl.c_str());
    if (changed) {
      base::ThreadPool::PostTask(
          FROM_HERE,
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
          base::BindOnce([] {
            if (atrium_core_save() < 0) {
              LOG(WARNING) << "Atrium: a space's archive TTL could not be "
                              "written; it is in memory only and a relaunch "
                              "loses it";
            }
          }));
    }
    return;
  }
  if (name == "settingsSpaceRenamed") {
    if (args.size() < 2 || !args[0].is_int() || !args[1].is_string()) {
      LOG(WARNING) << "Atrium: settingsSpaceRenamed carried bad arguments";
      return;
    }
    const std::string new_name = args[1].GetString();
    if (atrium_core_rename_space(args[0].GetInt(), new_name.c_str())) {
      base::ThreadPool::PostTask(
          FROM_HERE,
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
          base::BindOnce([] {
            if (atrium_core_save() < 0) {
              LOG(WARNING) << "Atrium: a space's rename could not be "
                              "written; it is in memory only and a relaunch "
                              "loses it";
            }
          }));
    }
    return;
  }
  if (name == "settingsSpaceCreated") {
    // Same realised-window guard as settingsOpenCredits: the new space needs
    // a real profile to belong to, and this window has none of its own.
    Browser* into = BrowserForOpenedPage();
    if (!into) {
      VLOG(1) << "Atrium: New Space refused - no realised Atrium window";
      return;
    }
    char* raw = atrium_core_create_space(
        "New Space", into->profile()->GetBaseName().MaybeAsASCII().c_str());
    if (!raw) {
      return;
    }
    atrium_core_string_free(raw);
    // The new space's id and order come from the core, not from anything
    // this window already holds - only a re-read shows it.
    SaveSpacesThenPush(
        "Atrium: a new space could not be written; it is in memory only and a "
        "relaunch loses it");
    return;
  }
  if (name == "settingsSpaceOpened") {
    if (args.empty() || !args[0].is_int()) {
      LOG(WARNING) << "Atrium: settingsSpaceOpened carried bad arguments";
      return;
    }
    Browser* into = BrowserForOpenedPage();
    if (!into) {
      VLOG(1) << "Atrium: Open refused - no realised Atrium window";
      return;
    }
    if (BrowserController* controller =
            BrowserController::ControllerForBrowser(into)) {
      controller->SelectSpace(args[0].GetInt());
    }
    into->window()->Activate();
    return;
  }
  if (name == "settingsThemePreview" || name == "settingsThemeChanged") {
    if (args.empty() || !args[0].is_string()) {
      LOG(WARNING) << "Atrium: " << name << " carried no space id";
      return;
    }
    // A missing second argument reads the same as an explicit null - both
    // mean "no spec", which each handler resolves its own way.
    const base::Value* spec = args.size() > 1 ? &args[1] : nullptr;
    if (name == "settingsThemePreview") {
      HandleThemePreview(args[0].GetString(), spec);
    } else {
      HandleThemeCommit(args[0].GetString(), spec);
    }
    return;
  }
  if (name == "settingsProfileAdded") {
    char* raw = atrium_core_add_profile();
    if (!raw) {
      return;
    }
    atrium_core_string_free(raw);
    // The new profile's id and order come from the core, not from anything
    // this window already holds - only a re-read shows it.
    SaveSpacesThenPush(
        "Atrium: a new profile could not be written; it is in memory only and "
        "a relaunch loses it");
    return;
  }
  if (name == "settingsImportPreview" || name == "settingsImportRun") {
    if (args.empty() || !args[0].is_string() ||
        ImportSourceLabel(args[0].GetString()).empty()) {
      LOG(WARNING) << "Atrium: " << name << " named a source this build lacks";
      return;
    }
    const std::string source = args[0].GetString();
    const base::FilePath dir = ImportSourceDir(source);
    if (name == "settingsImportPreview") {
      StartImportPreview(source, dir);
      return;
    }
    base::FilePath app_support;
    base::PathService::Get(base::DIR_APP_DATA, &app_support);
    // The run saves on its own, and the reply is not bound to this window,
    // so closing Settings mid-run loses nothing.
    SettingsWriteRunner()->PostTaskAndReplyWithResult(
        FROM_HERE,
        base::BindOnce(
            &RunImport, source, dir,
            SpacesFilePath(DevOnlyPathOverride("ATRIUM_SPACES_FILE"),
                           app_support),
            RoutingFilePath(DevOnlyPathOverride("ATRIUM_ROUTING_FILE"),
                            app_support),
            ResolveSettingsFilePath()),
        base::BindOnce(&AtriumSettingsWindow::OnImportFinished,
                       AtriumBrowserWindow::LastSettingsJson()));
    return;
  }
  if (name == "settingsProfileRenamed") {
    if (args.size() < 2 || !args[0].is_string() || !args[1].is_string()) {
      LOG(WARNING) << "Atrium: settingsProfileRenamed carried bad arguments";
      return;
    }
    const std::string profile_id = args[0].GetString();
    if (!ProfileBootstrap::IsSafeProfileId(profile_id)) {
      VLOG(1) << "Atrium: settings names an unknown profile";
      return;
    }
    const std::string new_name = args[1].GetString();
    if (atrium_core_rename_profile(profile_id.c_str(), new_name.c_str())) {
      base::ThreadPool::PostTask(
          FROM_HERE,
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
          base::BindOnce([] {
            if (atrium_core_save() < 0) {
              LOG(WARNING) << "Atrium: a profile's rename could not be "
                              "written; it is in memory only and a relaunch "
                              "loses it";
            }
          }));
    }
    return;
  }
  if (name == "settingsProfileDeleted") {
    if (args.empty() || !args[0].is_string()) {
      LOG(WARNING) << "Atrium: settingsProfileDeleted carried bad arguments";
      return;
    }
    const std::string profile_id = args[0].GetString();
    if (!ProfileBootstrap::IsSafeProfileId(profile_id)) {
      VLOG(1) << "Atrium: settings names an unknown profile";
      return;
    }
    if (atrium_core_delete_profile(profile_id.c_str())) {
      // The removed row and the spaces that pointed at it both come from the
      // core - only a re-read shows either.
      SaveSpacesThenPush(
          "Atrium: a profile's deletion could not be written; it is in memory "
          "only and a relaunch loses it");
    }
    return;
  }
  if (name == "settingsSpaceProfileChanged") {
    if (args.size() < 2 || !args[0].is_int() || !args[1].is_string()) {
      LOG(WARNING) << "Atrium: settingsSpaceProfileChanged carried bad "
                      "arguments";
      return;
    }
    const std::string profile_id = args[1].GetString();
    if (!ProfileBootstrap::IsSafeProfileId(profile_id)) {
      VLOG(1) << "Atrium: settings names an unknown profile";
      return;
    }
    if (atrium_core_set_space_profile(args[0].GetInt(), profile_id.c_str())) {
      // Both profiles' spaceCount move - only a re-read shows either.
      SaveSpacesThenPush(
          "Atrium: a space's profile could not be written; it is in memory "
          "only and a relaunch loses it");
    }
    return;
  }
  if (name == "settingsProfileSearchEngineChanged") {
    if (args.size() < 2 || !args[0].is_string() || !args[1].is_string()) {
      LOG(WARNING) << "Atrium: settingsProfileSearchEngineChanged carried "
                      "bad arguments";
      return;
    }
    const std::string profile_id = args[0].GetString();
    if (!ProfileBootstrap::IsSafeProfileId(profile_id)) {
      VLOG(1) << "Atrium: settings names an unknown profile";
      return;
    }
    const std::string engine = args[1].GetString();
    const bool stored_changed = atrium_core_set_profile_search_engine(
        profile_id.c_str(), engine.empty() ? nullptr : engine.c_str());
    // The stored value and the profile's live default provider drift apart
    // whenever an apply misses, and re-picking the engine already on disk is
    // how a user retries - so the apply must not hang off `stored_changed`.
    if (StoredProfileEngineIs(profile_id, engine)) {
      ApplyProfileEngine(profile_id, engine);
    }
    if (stored_changed) {
      SaveSpacesThenPush(
          "Atrium: a profile's search engine could not be written; it is in "
          "memory only and a relaunch loses it");
    } else {
      // Nothing new to write, but the pane rebuilt its select from the last
      // pushed document and needs to see the stored engine again.
      PushSpaces();
    }
    return;
  }
  if (name == "settingsProfileHibernateChanged") {
    if (args.size() < 3 || !args[0].is_string() || !args[1].is_string() ||
        !args[2].is_string()) {
      LOG(WARNING) << "Atrium: settingsProfileHibernateChanged carried bad "
                      "arguments";
      return;
    }
    const std::string profile_id = args[0].GetString();
    if (!ProfileBootstrap::IsSafeProfileId(profile_id)) {
      VLOG(1) << "Atrium: settings names an unknown profile";
      return;
    }
    const std::string mode = args[1].GetString();
    const std::string threshold = args[2].GetString();
    const bool mode_changed = atrium_core_set_profile_hibernate(
        profile_id.c_str(), mode.empty() ? nullptr : mode.c_str());
    const bool threshold_changed = atrium_core_set_profile_hib_threshold(
        profile_id.c_str(), threshold.empty() ? nullptr : threshold.c_str());
    if (mode_changed || threshold_changed) {
      base::ThreadPool::PostTask(
          FROM_HERE,
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
          base::BindOnce([] {
            if (atrium_core_save() < 0) {
              LOG(WARNING) << "Atrium: a profile's hibernation policy could "
                              "not be written; it is in memory only and a "
                              "relaunch loses it";
            }
          }));
    }
    return;
  }
  if (name == "settingsProfileSuggestChanged") {
    if (args.size() < 2 || !args[0].is_string() || !args[1].is_bool()) {
      LOG(WARNING) << "Atrium: settingsProfileSuggestChanged carried bad "
                      "arguments";
      return;
    }
    const std::string profile_id = args[0].GetString();
    if (!ProfileBootstrap::IsSafeProfileId(profile_id)) {
      VLOG(1) << "Atrium: settings names an unknown profile";
      return;
    }
    SetProfileSuggestEnabled(profile_id, args[1].GetBool());
    return;
  }
  if (name == "settingsProfileDownloadDirPick") {
    if (args.empty() || !args[0].is_string()) {
      LOG(WARNING) << "Atrium: settingsProfileDownloadDirPick carried bad "
                      "arguments";
      return;
    }
    const std::string profile_id = args[0].GetString();
    if (!ProfileBootstrap::IsSafeProfileId(profile_id)) {
      VLOG(1) << "Atrium: settings names an unknown profile";
      return;
    }
    HandleProfileDownloadDirPick(profile_id);
    return;
  }
  if (name == "settingsProfileOpenPage") {
    if (args.size() < 2 || !args[0].is_string() || !args[1].is_string()) {
      LOG(WARNING) << "Atrium: settingsProfileOpenPage carried bad arguments";
      return;
    }
    const std::string profile_id = args[0].GetString();
    if (!ProfileBootstrap::IsSafeProfileId(profile_id)) {
      VLOG(1) << "Atrium: settings names an unknown profile";
      return;
    }
    HandleProfileOpenPage(profile_id, args[1].GetString());
    return;
  }
  if (name == "settingsPaneSelected") {
    // The pane is remembered by the page for now. Panes get content later, and
    // that is when a selection becomes worth persisting.
    if (args.empty() || !args[0].is_string()) {
      LOG(WARNING) << "Atrium: settingsPaneSelected carried no pane name";
      return;
    }
    VLOG(1) << "Atrium: settings pane selected " << args[0].GetString();
    return;
  }
}

// The values come from the Rust core, which is the only thing that reads this
// store now - Swift used to.
//
// The read goes to the thread pool. It has to: reading it on the UI thread
// aborts the browser with "Function marked as blocking was called from a scope
// that disallows blocking", which is exactly what the first version of this
// did. A settings window that paints its labels first and fills in the values
// a beat later is the correct trade, not a compromise.
void AtriumSettingsWindow::PushSettings() {
  if (!renderer_ready_ || !web_contents_) {
    return;
  }
  base::FilePath app_support;
  base::PathService::Get(base::DIR_APP_DATA, &app_support);

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&LoadSettingsJSON, ResolveSettingsFilePath()),
      base::BindOnce(&AtriumSettingsWindow::OnSettingsLoaded,
                     weak_factory_.GetWeakPtr()));

  // The Inputs pane needs the hotkeys, which are a different store. Read on
  // the same trip rather than waiting for that pane to be opened: both files
  // are small, and a pane that fills in when you click it reads as a stall.
  std::string hotkeys_override = DevOnlyPathOverride("ATRIUM_HOTKEYS_FILE");
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&LoadHotkeysJSON,
                     HotkeysFilePath(hotkeys_override, app_support)),
      base::BindOnce(&AtriumSettingsWindow::OnHotkeysLoaded,
                     weak_factory_.GetWeakPtr()));

  // The Spaces pane edits spaces, so it needs the whole document rather than
  // one window's sidebar model.
  PushSpaces();
}

// Chained, not posted beside the write: atrium_core_save fsyncs on one worker
// and PushSpaces re-reads spaces.json on another, so an unordered read can
// carry pre-edit bytes that nothing later replaces.
void AtriumSettingsWindow::SaveSpacesThenPush(
    const char* write_failed,
    base::OnceCallback<void(bool)> then) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::BLOCK_SHUTDOWN},
      base::BindOnce([] { return atrium_core_save() >= 0; }),
      base::BindOnce(&AtriumSettingsWindow::OnSpacesSaved,
                     weak_factory_.GetWeakPtr(), write_failed,
                     std::move(then)));
}

void AtriumSettingsWindow::OnSpacesSaved(const char* write_failed,
                                         base::OnceCallback<void(bool)> then,
                                         bool wrote) {
  if (!wrote) {
    LOG(WARNING) << write_failed;
  }
  PushSpaces();
  std::move(then).Run(wrote);
}

void AtriumSettingsWindow::PushSpaces() {
  if (!renderer_ready_ || !web_contents_) {
    return;
  }
  base::FilePath app_support;
  base::PathService::Get(base::DIR_APP_DATA, &app_support);
  std::string spaces_override = DevOnlyPathOverride("ATRIUM_SPACES_FILE");
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&LoadSpacesJSON,
                     SpacesFilePath(spaces_override, app_support)),
      base::BindOnce(&AtriumSettingsWindow::OnSpacesLoaded,
                     weak_factory_.GetWeakPtr(), ++spaces_generation_));
}

void AtriumSettingsWindow::PushRouting() {
  if (!renderer_ready_ || !web_contents_) {
    return;
  }
  base::FilePath app_support;
  base::PathService::Get(base::DIR_APP_DATA, &app_support);
  std::string routing_override = DevOnlyPathOverride("ATRIUM_ROUTING_FILE");
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&LoadRoutingJSON,
                     RoutingFilePath(routing_override, app_support)),
      base::BindOnce(&AtriumSettingsWindow::OnRoutingLoaded,
                     weak_factory_.GetWeakPtr()));
}

void AtriumSettingsWindow::HandleThemePreview(const std::string& space_id,
                                              const base::Value* spec) {
  if (!IsKnownSpaceId(space_id)) {
    LOG(WARNING) << "Atrium: a theme preview named an unknown space";
    return;
  }
  if (!spec || !spec->is_dict()) {
    RevertLiveThemePreview();
    return;
  }
  BrowserController* controller = ControllerForOpenedPage();
  if (!controller) {
    VLOG(1) << "Atrium: a theme preview found no realised Atrium window";
    return;
  }
  std::string theme_json;
  base::JSONWriter::Write(*spec, &theme_json);
  AtriumBrowserWindow* painted =
      controller->PreviewSpaceTheme(space_id, theme_json);
  if (!painted) {
    return;
  }
  preview_window_ = painted->GetWeakPtr();
}

void AtriumSettingsWindow::HandleThemeCommit(const std::string& space_id,
                                             const base::Value* spec) {
  if (!IsKnownSpaceId(space_id)) {
    LOG(WARNING) << "Atrium: a theme save named an unknown space";
    return;
  }
  BrowserController* controller = ControllerForOpenedPage();
  if (!controller) {
    VLOG(1) << "Atrium: a theme save found no realised Atrium window";
    return;
  }
  std::string theme_json;
  const bool has_spec = spec && spec->is_dict();
  if (has_spec) {
    base::JSONWriter::Write(*spec, &theme_json);
  }
  if (!controller->SetSpaceTheme(space_id, has_spec ? &theme_json : nullptr)) {
    return;
  }
  // The store now holds what the window is about to show, so a close has
  // nothing left to undo.
  preview_window_.reset();
  // The one save for both stores the edit marked dirty. The paint waits for
  // it, so the window never fades in a theme the file does not hold.
  SaveSpacesThenPush(
      "Atrium: theme save failed - it is in memory only and a relaunch loses "
      "it",
      base::BindOnce(&AtriumSettingsWindow::OnThemeSaved,
                     weak_factory_.GetWeakPtr(), space_id));
}

void AtriumSettingsWindow::OnThemeSaved(const std::string& space_id,
                                        bool wrote) {
  if (!wrote) {
    return;  // OnSpacesSaved already named the failure.
  }
  VLOG(1) << "Atrium: theme saved";
  if (BrowserController* controller = ControllerForOpenedPage()) {
    controller->ApplySavedSpaceTheme(space_id);
  }
}

void AtriumSettingsWindow::RevertLiveThemePreview() {
  AtriumBrowserWindow* window = preview_window_.get();
  preview_window_.reset();
  if (!window) {
    return;
  }
  if (BrowserController* controller =
          BrowserController::ControllerForBrowser(window->browser())) {
    controller->RevertThemePreview(window);
  }
}

void AtriumSettingsWindow::HandleProfileOpenPage(const std::string& profile_id,
                                                 const std::string& page) {
  const std::string url = ProfilePageUrl(page);
  if (url.empty()) {
    VLOG(1) << "Atrium: settingsProfileOpenPage named an unknown page "
            << page;
    return;
  }
  Browser* into = BrowserForProfileId(profile_id);
  if (!into) {
    VLOG(1) << "Atrium: no window could be opened for profile '" << profile_id
            << "'";
    NotifyPaneOfProfileFailure("atriumSettingsProfilePageRefused", profile_id);
    return;
  }
  chrome::AddTabAt(into, GURL(url), -1, /*foreground=*/true);
}

// The browser-to-page half of a route that failed on one profile - a click
// that does nothing is indistinguishable from a route that never ran.
void AtriumSettingsWindow::NotifyPaneOfProfileFailure(
    const std::string& js_function, const std::string& profile_id) {
  if (!web_contents_) {
    return;
  }
  std::string quoted_id;
  base::JSONWriter::Write(base::Value(profile_id), &quoted_id);
  web_contents_->GetPrimaryMainFrame()->ExecuteJavaScript(
      base::UTF8ToUTF16("window." + js_function + " && " + js_function + "(" +
                        quoted_id + ")"),
      base::NullCallback());
}

void AtriumSettingsWindow::HandleProfileDownloadDirPick(
    const std::string& profile_id) {
  (new ProfileDownloadDirPicker(profile_id, weak_factory_.GetWeakPtr()))
      ->Show(widget_->GetNativeWindow(), web_contents_.get());
}

void AtriumSettingsWindow::OnProfileDownloadDirPicked(std::string profile_id,
                                                      base::FilePath dir) {
  if (SetProfileDownloadDir(profile_id, dir)) {
    PushSpaces();
  }
}

// The mirror of PushSettings, and it resolves the path through the same helper
// for the same reason: ATRIUM_SETTINGS_FILE is what a scenario stages, so a
// write that resolved differently would edit a file the window is not showing.
//
// The write blocks and fsyncs, so it goes off the UI thread. BLOCK_SHUTDOWN
// follows ApplySidebarGesture: the edit most likely to be lost is the last one
// before quit. The reply is best-effort by contrast - it only repaints - so a
// shutdown that drops it costs nothing.
//
// A SEQUENCED runner rather than a bare thread-pool post, and that is
// correctness rather than tidiness. atrium_core_save_settings holds no lock: it
// reads the file, merges and renames, once per call. Two toggles made faster
// than a write completes would run in parallel, the older one could rename
// last, and its value would silently replace the newer - with both saves
// reporting 1. Sequenced tasks run one at a time in posting order, which is the
// order the user clicked in.
void AtriumSettingsWindow::SaveSettings(std::string settings_json,
                                        int32_t save_id) {
  // Read before the string is moved away. The pref and the windows are only
  // touched once the file has taken the values - see OnSettingsSaved.
  std::optional<bool> confirm_to_quit =
      ConfirmToQuitFromSettings(settings_json);
  std::optional<bool> external_links_open_in_little =
      ExternalLinksOpenInLittleFromSettings(settings_json);
  WindowAppearance appearance = WindowAppearanceFromSettings(settings_json);
  std::optional<bool> memory_saver_enabled =
      MemorySaverEnabledFromSettings(settings_json);
  std::optional<int> memory_saver_threshold_minutes =
      MemorySaverThresholdMinutesFromSettings(settings_json);
  std::optional<std::string> notification_timeout =
      AtriumBrowserWindow::NotificationTimeoutFromSettings(settings_json);
  std::optional<std::pair<std::string, bool>> skylight =
      AtriumBrowserWindow::SkylightFromSettings(settings_json);
  std::optional<bool> ai_enabled =
      AtriumBrowserWindow::AiEnabledFromSettings(settings_json);
  std::optional<bool> auto_summarize =
      AtriumBrowserWindow::AutoSummarizeFromSettings(settings_json);
  std::optional<bool> vim_mode_enabled =
      AtriumBrowserWindow::VimModeEnabledFromSettings(settings_json);
  std::optional<std::string> translate_language =
      AtriumBrowserWindow::TranslateLanguageFromSettings(settings_json);
  base::DictValue swipe_tuning =
      AtriumBrowserWindow::SwipeTuningFromSettings(settings_json);
  std::optional<bool> chrome_page_tint =
      AtriumBrowserWindow::ChromePageTintFromSettings(settings_json);
  std::optional<std::string> app_icon_variant =
      AtriumBrowserWindow::AppIconVariantFromSettings(settings_json);
  // Before any mirror runs: a bridge catching up on its service's load reads
  // this copy, and a stale one re-applies the list this save changes.
  AtriumBrowserWindow::SetLastSettingsJson(settings_json);
  // Mirrored here rather than gated on the write's own success like the
  // fields above: engines are keyed by id, so a save the write later declines
  // leaves the service no worse off than any other field it also skipped.
  if (const std::optional<std::vector<MirroredEngine>> engines =
          SearchEnginesFromSettings(settings_json)) {
    MirrorEnginesEverywhere(*engines);
  }
  // One task on the key write's runner, so the clear runs before a key typed
  // after this save. A failed delete skips the write and reports failure.
  SettingsWriteRunner()->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&SaveSettingsFailClosed, std::move(settings_json),
                     base::BindPostTaskToCurrentDefault(base::BindOnce(
                         &AtriumSettingsWindow::
                             PushCustomEndpointApiKeyStatusToInstance))),
      base::BindOnce(&AtriumSettingsWindow::OnSettingsSaved,
                     weak_factory_.GetWeakPtr(), confirm_to_quit,
                     external_links_open_in_little, std::move(appearance),
                     memory_saver_enabled, memory_saver_threshold_minutes,
                     std::move(notification_timeout), std::move(skylight),
                     ai_enabled, auto_summarize, vim_mode_enabled,
                     std::move(translate_language), std::move(swipe_tuning),
                     chrome_page_tint,
                     std::move(app_icon_variant), save_id));
}

void AtriumSettingsWindow::SaveRouting(std::string routing_json) {
  if (!RoutingRulesAreValid(routing_json)) {
    LOG(WARNING) << "Atrium: the routing surface sent a rule this build "
                    "refuses - unknown match type, empty pattern, or a "
                    "target space that does not exist; nothing was written";
    CallPage(u"window.atriumSetRoutingSaveResult && "
            u"atriumSetRoutingSaveResult(0)");
    return;
  }
  base::FilePath app_support;
  base::PathService::Get(base::DIR_APP_DATA, &app_support);
  std::string routing_override = DevOnlyPathOverride("ATRIUM_ROUTING_FILE");
  base::FilePath path = RoutingFilePath(routing_override, app_support);
  SettingsWriteRunner()->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&SaveRoutingJSON, path, routing_json),
      base::BindOnce(&AtriumSettingsWindow::OnRoutingSaved,
                     weak_factory_.GetWeakPtr(), std::move(routing_json)));
}

// Arms the live cache with what the write actually left on disk, rather
// than trusting the surface's own payload - a declined write must not route a
// tab by rules that were never saved.
void AtriumSettingsWindow::OnRoutingSaved(std::string routing_json,
                                          int32_t code) {
  switch (code) {
    case kSaveWritten:
      VLOG(1) << "Atrium: routing rules written";
      SetRoutingRulesJSON(std::move(routing_json));
      break;
    case kSaveUnusableArguments:
      LOG(WARNING) << "Atrium: the routing surface sent an object the core "
                      "could not use; nothing was written";
      break;
    case kSaveWriteFailed:
      LOG(WARNING) << "Atrium: the routing write failed; the edit is in the "
                      "surface only and a relaunch loses it";
      break;
    case kSaveDeclined:
      LOG(WARNING) << "Atrium: routing.json is at a schema this build cannot "
                      "read; the edit was declined and the file left alone";
      break;
    default:
      LOG(WARNING) << "Atrium: unknown routing save result " << code;
      break;
  }
  CallPage(base::UTF8ToUTF16(
      "window.atriumSetRoutingSaveResult && "
      "atriumSetRoutingSaveResult(" +
      base::NumberToString(code) + ")"));
}

// A payload the core cannot read is written as sent, which is what a build
// without the reset did with every payload.
std::string HotkeysFileAfterEdit(const std::string& edit_json) {
  char* raw = atrium_core_query("hotkeys-edit", edit_json.c_str(),
                                /*migration_instant=*/0.0);
  if (!raw) {
    return edit_json;
  }
  std::string out(raw);
  atrium_core_string_free(raw);
  return out;
}

// Every preset's chord table, by name. Read from the core rather than listed
// here, so the pane and the merge never describe different Arcs.
std::string HotkeyPresetTables() {
  char* raw = atrium_core_query("hotkey-presets", "",
                                /*migration_instant=*/0.0);
  if (!raw) {
    return "{}";
  }
  std::string out(raw);
  atrium_core_string_free(raw);
  return out;
}

// A file the core cannot read merges to nothing, so the map keeps what it has
// rather than being handed a payload that would unbind half of it.
std::string HotkeysMergedForNativeMap(const std::string& file_json) {
  char* raw = atrium_core_query("hotkeys", file_json.c_str(),
                                /*migration_instant=*/0.0);
  if (!raw) {
    return std::string();
  }
  std::string out(raw);
  atrium_core_string_free(raw);
  return out;
}

void AtriumSettingsWindow::SaveHotkeys(std::string hotkeys_json) {
  if (!HotkeysAreValid(hotkeys_json)) {
    LOG(WARNING) << "Atrium: the hotkeys surface sent an id or combo this "
                    "build refuses; nothing was written";
    CallPage(u"window.atriumSetHotkeysSaveResult && "
            u"atriumSetHotkeysSaveResult(0)");
    return;
  }
  base::FilePath app_support;
  base::PathService::Get(base::DIR_APP_DATA, &app_support);
  std::string hotkeys_override = DevOnlyPathOverride("ATRIUM_HOTKEYS_FILE");
  base::FilePath path = HotkeysFilePath(hotkeys_override, app_support);
  // A reset is not a rebind: the pane's payload still names the action, and
  // what lands must not, or today's shipped chord is pinned as a user value.
  hotkeys_json = HotkeysFileAfterEdit(hotkeys_json);
  // The map gets the merged view, the file gets the entry-shaped one. Handing
  // the map what is written would leave a reset action on its old chord.
  std::string for_native_map = HotkeysMergedForNativeMap(hotkeys_json);
  SettingsWriteRunner()->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&SaveHotkeysJSON, path, std::move(hotkeys_json)),
      base::BindOnce(&AtriumSettingsWindow::OnHotkeysSaved,
                     weak_factory_.GetWeakPtr(), std::move(for_native_map)));
}

// The Inputs pane's write completes the hotkey path: SetFromJSON had
// only ever run at launch, so a rebind here used to need a relaunch to act.
void AtriumSettingsWindow::OnHotkeysSaved(std::string native_map_json,
                                          int32_t code) {
  switch (code) {
    case kSaveWritten:
      VLOG(1) << "Atrium: hotkeys written";
#if BUILDFLAG(IS_MAC)
      NativeHotkeyMap::Get()->SetFromJSON(native_map_json);
#endif  // BUILDFLAG(IS_MAC)
      break;
    case kSaveUnusableArguments:
      LOG(WARNING) << "Atrium: the hotkeys surface sent an object the core "
                      "could not use; nothing was written";
      break;
    case kSaveWriteFailed:
      LOG(WARNING) << "Atrium: the hotkeys write failed; the edit is in the "
                      "surface only and a relaunch loses it";
      break;
    case kSaveDeclined:
      LOG(WARNING) << "Atrium: hotkeys.json is at a schema this build cannot "
                      "read; the edit was declined and the file left alone";
      break;
    default:
      LOG(WARNING) << "Atrium: unknown hotkeys save result " << code;
      break;
  }
  CallPage(base::UTF8ToUTF16(
      "window.atriumSetHotkeysSaveResult && "
      "atriumSetHotkeysSaveResult(" +
      base::NumberToString(code) + ")"));
}

namespace {

bool AppearanceValuesDiffer(const WindowAppearance& a,
                            const WindowAppearance& b) {
  return !(a == b);
}

}  // namespace

SettingsApplyChanges DetermineSettingsApplyChanges(
    const SettingsApplyFields& current,
    const std::optional<SettingsApplyFields>& previous) {
  SettingsApplyChanges changes;
  if (!previous.has_value()) {
    changes.confirm_to_quit = current.confirm_to_quit.has_value();
    changes.external_links_open_in_little =
        current.external_links_open_in_little.has_value();
    changes.appearance = true;
    changes.memory_saver_enabled = current.memory_saver_enabled.has_value();
    changes.memory_saver_threshold_minutes =
        current.memory_saver_threshold_minutes.has_value();
    changes.notification_timeout = current.notification_timeout.has_value();
    changes.skylight = current.skylight.has_value();
    changes.ai_enabled = current.ai_enabled.has_value();
    changes.auto_summarize = current.auto_summarize.has_value();
    changes.vim_mode_enabled = current.vim_mode_enabled.has_value();
    changes.translate_language = current.translate_language.has_value();
    changes.swipe_tuning = !current.swipe_tuning.empty();
    changes.chrome_page_tint = current.chrome_page_tint.has_value();
    changes.app_icon_variant = current.app_icon_variant.has_value();
    return changes;
  }
  const SettingsApplyFields& prev = *previous;
  changes.confirm_to_quit = current.confirm_to_quit.has_value() &&
                            current.confirm_to_quit != prev.confirm_to_quit;
  changes.external_links_open_in_little =
      current.external_links_open_in_little.has_value() &&
      current.external_links_open_in_little !=
          prev.external_links_open_in_little;
  changes.appearance =
      AppearanceValuesDiffer(current.appearance, prev.appearance);
  changes.memory_saver_enabled =
      current.memory_saver_enabled.has_value() &&
      current.memory_saver_enabled != prev.memory_saver_enabled;
  changes.memory_saver_threshold_minutes =
      current.memory_saver_threshold_minutes.has_value() &&
      current.memory_saver_threshold_minutes !=
          prev.memory_saver_threshold_minutes;
  changes.notification_timeout =
      current.notification_timeout.has_value() &&
      current.notification_timeout != prev.notification_timeout;
  changes.skylight =
      current.skylight.has_value() && current.skylight != prev.skylight;
  changes.ai_enabled =
      current.ai_enabled.has_value() && current.ai_enabled != prev.ai_enabled;
  changes.auto_summarize = current.auto_summarize.has_value() &&
                           current.auto_summarize != prev.auto_summarize;
  changes.vim_mode_enabled = current.vim_mode_enabled.has_value() &&
                             current.vim_mode_enabled != prev.vim_mode_enabled;
  changes.translate_language =
      current.translate_language.has_value() &&
      current.translate_language != prev.translate_language;
  changes.swipe_tuning = !current.swipe_tuning.empty() &&
                         current.swipe_tuning != prev.swipe_tuning;
  changes.chrome_page_tint = current.chrome_page_tint.has_value() &&
                             current.chrome_page_tint != prev.chrome_page_tint;
  changes.app_icon_variant =
      current.app_icon_variant.has_value() &&
      current.app_icon_variant != prev.app_icon_variant;
  return changes;
}

// A failed save reaches the USER, not only the log.
//
// The surface has already repainted the row, so a failure in silence leaves
// them believing an edit the file never took - and -2 is the one with nothing
// to retry: settings.json is at a schema this build cannot read, and stays
// unwritable until it is moved aside. That case is reachable while the window
// shows values, because the settings reader answers such a file with defaults
// rather than refusing it.
void AtriumSettingsWindow::OnSettingsSaved(
    std::optional<bool> confirm_to_quit,
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
    int32_t code) {
  switch (code) {
    case kSaveWritten: {
      VLOG(1) << "Atrium: settings written";
      // The page resends the whole object every save, so has_value() alone
      // was true on every save regardless of which row moved.
      SettingsApplyFields current;
      current.confirm_to_quit = confirm_to_quit;
      current.external_links_open_in_little = external_links_open_in_little;
      current.appearance = appearance;
      current.memory_saver_enabled = memory_saver_enabled;
      current.memory_saver_threshold_minutes = memory_saver_threshold_minutes;
      current.notification_timeout = notification_timeout;
      current.skylight = skylight;
      current.ai_enabled = ai_enabled;
      current.auto_summarize = auto_summarize;
      current.vim_mode_enabled = vim_mode_enabled;
      current.translate_language = translate_language;
      current.swipe_tuning = swipe_tuning.Clone();
      current.chrome_page_tint = chrome_page_tint;
      current.app_icon_variant = app_icon_variant;
      const SettingsApplyChanges changes = DetermineSettingsApplyChanges(
          current, last_applied_settings_fields_);

      // the one field in this store the browser itself has to act
      // on. Here rather than at the click, so a refused write leaves the pref
      // where the page puts the row back to - the surface and the quit gate
      // fail together or not at all.
      if (changes.confirm_to_quit) {
        SetConfirmToQuitPref(*confirm_to_quit);
      }
      // The same shape, for the same reason. The value openURLs
      // reads is seeded at startup and lives in the process, so without this
      // the row would persist a key the running browser keeps ignoring until
      // the next launch.
      if (changes.external_links_open_in_little) {
        SetExternalLinksOpenInLittle(*external_links_open_in_little);
      }
      // the three the WINDOWS have to act on, on the same terms and
      // for the same reason - a refused write must not leave the sidebar on a
      // side the file does not say.
      if (changes.appearance) {
        AtriumBrowserWindow::SetWindowAppearance(appearance);
        SetConfirmQuitPanelAppearance(appearance.opaque_surround,
                                      appearance.surround_argb);
        // This window is not one of SetWindowAppearance's live windows -
        // it wears overlay.css but pushes its own theme.
        PushOverlayTheme();
      }
      // the same terms again - MemorySaverModePolicy is the actual
      // discard engine, and its two prefs are the only thing it reads.
      if (changes.memory_saver_enabled) {
        SetMemorySaverEnabledPref(*memory_saver_enabled);
      }
      if (changes.memory_saver_threshold_minutes) {
        SetMemorySaverThresholdMinutesPref(*memory_saver_threshold_minutes);
      }
      // the toast stack's own gate - same terms again.
      if (changes.notification_timeout) {
        AtriumBrowserWindow::SetNotificationTimeoutStep(
            *notification_timeout);
      }
      // the backdrop's own gate - same terms again.
      if (changes.skylight) {
        AtriumBrowserWindow::SetSkylight(skylight->first, skylight->second);
      }
      // the AI kill switch's own gate - same terms again, so a
      // refused write leaves every open panel exactly as it was.
      if (changes.ai_enabled) {
        AtriumBrowserWindow::SetAiEnabled(*ai_enabled);
      }
      // the Auto summarize switch's own gate - same terms again,
      // so only the next panel open reads the new value.
      if (changes.vim_mode_enabled) {
        AtriumBrowserWindow::SetVimModeEnabled(*vim_mode_enabled);
      }
      if (changes.auto_summarize) {
        AtriumBrowserWindow::SetAiPanelAutoSummarize(*auto_summarize);
      }
      if (changes.translate_language) {
        AtriumBrowserWindow::SetAiPanelTranslateLanguage(*translate_language);
      }
      // the page tint's own gate - the one that also restarts
      // ambilight sampling on every live window.
      if (changes.chrome_page_tint) {
        AtriumBrowserWindow::SetChromePageTint(*chrome_page_tint);
      }
      // The Dock icon's own gate - same terms again, so a refused write
      // leaves the Dock on the variant the file still names.
      if (changes.app_icon_variant) {
        AtriumBrowserWindow::SetAppIconVariant(*app_icon_variant);
      }
      // the swipe presets' own gate - same terms again.
      if (changes.swipe_tuning) {
        AtriumBrowserWindow::SetSwipeTuning(std::move(swipe_tuning));
      }
      last_applied_settings_fields_ = std::move(current);
      break;
    }
    case kSaveUnusableArguments:
      LOG(WARNING) << "Atrium: the settings surface sent an object the core "
                      "could not use; nothing was written";
      break;
    case kSaveWriteFailed:
      LOG(WARNING) << "Atrium: the settings write failed; the edit is in the "
                      "surface only and a relaunch loses it";
      break;
    case kSaveDeclined:
      LOG(WARNING) << "Atrium: settings.json is at a schema this build cannot "
                      "read; the edit was declined and the file left alone";
      break;
    default:
      LOG(WARNING) << "Atrium: unknown settings save result " << code;
      break;
  }
  // The page decides what each code says and puts the row back when the file
  // did not take it. A number rather than a sentence, so the surface's words
  // stay with the rest of its words.
  CallPage(
      base::UTF8ToUTF16("window.atriumSetSaveResult && atriumSetSaveResult(" +
                        base::NumberToString(code) + ", " +
                        base::NumberToString(save_id) + ")"));
}

void AtriumSettingsWindow::OnSpacesLoaded(int generation, std::string json) {
  if (generation != spaces_generation_) {
    return;
  }
  if (json.empty()) {
    return;
  }
  // Sidebar order first: the panes number the list they draw, and the core
  // reads every index they send back as a sidebar position.
  json = WithSpaceThemes(WithProfileDetail(SpacesInSidebarOrder(json)));
  VLOG(1) << "Atrium: spaces pushed, " << json.size() << " bytes";
  CallPage(u"window.atriumSetSpaces && atriumSetSpaces(JSON.parse(" +
          base::UTF8ToUTF16(base::GetQuotedJSONString(json)) + u"))");
  if (!pending_space_index_) {
    return;
  }
  const int index = *pending_space_index_;
  pending_space_index_.reset();
  CallPage(base::UTF8ToUTF16(
      "window.atriumSelectSettingsSpace && atriumSelectSettingsSpace(" +
      base::NumberToString(index) + ")"));
}

void AtriumSettingsWindow::OnHotkeysLoaded(std::string json) {
  if (json.empty()) {
    return;
  }
  VLOG(1) << "Atrium: hotkeys pushed, " << json.size() << " bytes";
  // Without them the pane cannot tell a rebound row from one still on its
  // shipped chord, and offers to reset a row that has nothing to reset.
  // No path means no file read: an absent file loads as exactly the defaults.
  const std::string defaults = LoadHotkeysJSON(base::FilePath());
  // The pane applies a picked preset from what it already holds, so the tables
  // travel with the push rather than on the click.
  const std::string presets = HotkeyPresetTables();
  CallPage(u"window.atriumSetHotkeys && atriumSetHotkeys(JSON.parse(" +
          base::UTF8ToUTF16(base::GetQuotedJSONString(json)) + u"), JSON.parse(" +
          base::UTF8ToUTF16(base::GetQuotedJSONString(defaults)) + u"), JSON.parse(" +
          base::UTF8ToUTF16(base::GetQuotedJSONString(presets)) + u"))");
}

void AtriumSettingsWindow::OnRoutingLoaded(std::string json) {
  if (json.empty()) {
    return;
  }
  VLOG(1) << "Atrium: routing rules pushed, " << json.size() << " bytes";
  CallPage(u"window.atriumSetRoutingRules && atriumSetRoutingRules(JSON.parse(" +
          base::UTF8ToUTF16(base::GetQuotedJSONString(json)) + u"))");
}

void AtriumSettingsWindow::StartImportPreview(const std::string& source,
                                              const base::FilePath& dir) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&PreviewImport, source, dir),
      base::BindOnce(&AtriumSettingsWindow::OnImportPreviewed,
                     weak_factory_.GetWeakPtr(), source));
}

void AtriumSettingsWindow::OnImportPreviewed(std::string source,
                                             std::string answer) {
  if (answer.empty()) {
    return;
  }
  VLOG(1) << "Atrium: import " << source << " previewed";
  CallPage(u"window.atriumSetImportPreview && atriumSetImportPreview(JSON.parse(" +
          base::UTF8ToUTF16(base::GetQuotedJSONString(answer)) + u"))");
}

// static
void AtriumSettingsWindow::OnImportFinished(std::string settings_at_start,
                                            ImportOutcome outcome) {
  if (!outcome.saved_routing.empty()) {
    SetRoutingRulesJSON(outcome.saved_routing);
  }
  if (!outcome.saved_settings.empty()) {
    // Posted behind the stale save on the same runner, so this one lands last.
    std::string resave = ImportSettingsToResave(
        settings_at_start, AtriumBrowserWindow::LastSettingsJson(),
        outcome.saved_settings);
    if (!resave.empty()) {
      outcome.saved_settings = resave;
      SettingsWriteRunner()->PostTaskAndReplyWithResult(
          FROM_HERE,
          base::BindOnce(&SaveSettingsFailClosed, std::move(resave),
                         base::BindPostTaskToCurrentDefault(base::BindOnce(
                             &AtriumSettingsWindow::
                                 PushCustomEndpointApiKeyStatusToInstance))),
          base::BindOnce([](int32_t code) {
            LOG_IF(WARNING, code != kSaveWritten)
                << "Atrium: the imported search engines were not saved "
                   "again after a settings edit, code "
                << code;
          }));
    }
    // Before the mirror, like SaveSettings: a bridge that catches up on its
    // service's load reads this copy.
    AtriumBrowserWindow::SetLastSettingsJson(outcome.saved_settings);
    if (const std::optional<std::vector<MirroredEngine>> engines =
            SearchEnginesFromSettings(outcome.saved_settings)) {
      MirrorEnginesEverywhere(*engines);
    }
  }
  PushSidebarsEverywhere();
  if (outcome.answer.find(R"("error")") != std::string::npos) {
    if (Instance()) {
      Instance()->OnImportRan(std::move(outcome));
    }
    return;
  }
  // Off SettingsWriteRunner, so a large History read does not hold up later
  // settings saves. A quit skips a read that has not started yet.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN},
      base::BindOnce(&ReadOutcomeHistory, std::move(outcome)),
      base::BindOnce(&AtriumSettingsWindow::OnImportHistoryRead));
}

// static
void AtriumSettingsWindow::OnImportHistoryRead(ImportOutcome outcome) {
  for (ImportedHistory& batch : outcome.history) {
    WriteImportedHistory(std::move(batch));
  }
  if (Instance()) {
    Instance()->OnImportRan(std::move(outcome));
  }
}

void AtriumSettingsWindow::OnImportRan(ImportOutcome outcome) {
  if (outcome.answer.find(R"("error")") == std::string::npos) {
    VLOG(1) << "Atrium: import " << outcome.source << " applied";
  } else {
    LOG(WARNING) << "Atrium: the " << ImportSourceLabel(outcome.source)
                 << " import failed";
  }
  PushSpaces();
  if (!outcome.saved_routing.empty()) {
    OnRoutingLoaded(outcome.saved_routing);
  }
  // The page holds the whole settings object and sends it back on the next
  // edit, so a stale copy there would save the old engine list over the new.
  if (!outcome.saved_settings.empty()) {
    OnSettingsLoaded(outcome.saved_settings);
  }
  // Re-reads the source's files. The counts come from them only, not from
  // Atrium.
  StartImportPreview(outcome.source, ImportSourceDir(outcome.source));
  CallPage(u"window.atriumSetImportResult && atriumSetImportResult(JSON.parse(" +
          base::UTF8ToUTF16(base::GetQuotedJSONString(outcome.answer)) +
          u"))");
}

void AtriumSettingsWindow::PushSidebarsEverywhere() {
  GlobalBrowserCollection::GetInstance()->ForEach(
      [](BrowserWindowInterface* window) {
        Browser* browser = window->GetBrowserForMigrationOnly();
        if (browser && BrowserController::ControllerForBrowser(browser) &&
            AtriumBrowserWindow::HasRealisedWindow(browser)) {
          static_cast<AtriumBrowserWindow*>(browser->window())
              ->PushSidebarSnapshot();
        }
        return true;
      });
}

void AtriumSettingsWindow::OnSettingsLoaded(std::string json) {
  if (!renderer_ready_ || !web_contents_) {
    return;  // The window closed while the read was in flight.
  }
  if (json.empty()) {
    LOG(WARNING) << "Atrium: settings could not be read; the surface shows "
                    "its own defaults";
    return;
  }
  last_settings_json_ = std::move(json);
  PushSettingsWithAvailability();
}

void AtriumSettingsWindow::CheckAiOnDeviceAvailability() {
#if BUILDFLAG(IS_MAC)
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::WithBaseSyncPrimitives()},
      base::BindOnce(&atrium_ai::IsOnDeviceSummarizeAvailable),
      base::BindOnce(&AtriumSettingsWindow::OnAiOnDeviceAvailabilityChecked,
                     weak_factory_.GetWeakPtr()));
#else
  OnAiOnDeviceAvailabilityChecked(false);
#endif
}

void AtriumSettingsWindow::OnAiOnDeviceAvailabilityChecked(bool available) {
  ai_on_device_available_ = available;
  PushSettingsWithAvailability();
}

void AtriumSettingsWindow::PushSettingsWithAvailability() {
  if (!last_settings_json_) {
    return;
  }
  std::string json = *last_settings_json_;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (parsed && parsed->is_dict()) {
    base::DictValue& dict = parsed->GetDict();
    if (ai_on_device_available_.has_value()) {
      dict.Set("aiOnDeviceAvailable", *ai_on_device_available_);
    }
    if (default_browser_.has_value()) {
      dict.Set("defaultBrowser", *default_browser_);
    }
    // A run with every window closed leaves the key out and the switch off.
    if (BrowserController* controller = ControllerForOpenedPage()) {
      if (AtriumBrowserWindow* window = controller->SidebarWindow();
          window && window->HasSidebar()) {
        dict.Set("sidebarHidden", window->IsSidebarCollapsed());
      }
    }
    base::JSONWriter::Write(*parsed, &json);
  }
  VLOG(1) << "Atrium: settings pushed, " << json.size() << " bytes";
  CallPage(u"window.atriumSetSettings && atriumSetSettings(JSON.parse(" +
          base::UTF8ToUTF16(base::GetQuotedJSONString(json)) + u"))");
}

void AtriumSettingsWindow::CheckDefaultBrowser() {
  base::MakeRefCounted<shell_integration::DefaultBrowserWorker>()
      ->StartCheckIsDefault(
          base::BindOnce(&AtriumSettingsWindow::OnDefaultBrowserChecked,
                         weak_factory_.GetWeakPtr()));
}

void AtriumSettingsWindow::OnDefaultBrowserChecked(
    shell_integration::DefaultWebClientState state) {
  // A LaunchServices failure or another install mode reads as unknown, not
  // as a confirmed no.
  switch (state) {
    case shell_integration::IS_DEFAULT:
      default_browser_ = "yes";
      break;
    case shell_integration::NOT_DEFAULT:
      default_browser_ = "no";
      break;
    default:
      default_browser_ = "unknown";
      break;
  }
  VLOG(1) << "Atrium: default browser check answered " << *default_browser_;
  PushSettingsWithAvailability();
}

void AtriumSettingsWindow::HandleMakeDefaultBrowser() {
  VLOG(1) << "Atrium: asking the system to make Atrium the default browser";
  base::MakeRefCounted<shell_integration::DefaultBrowserWorker>()
      ->StartSetAsDefault(
          base::BindOnce(&AtriumSettingsWindow::OnDefaultBrowserChecked,
                         weak_factory_.GetWeakPtr()));
}

void AtriumSettingsWindow::HandleSidebarHiddenChanged(bool hidden) {
  BrowserController* controller = ControllerForOpenedPage();
  AtriumBrowserWindow* window = controller ? controller->SidebarWindow() : nullptr;
  if (!window || !window->HasSidebar()) {
    VLOG(1) << "Atrium: settingsSidebarHiddenChanged with no sidebar window";
    return;
  }
  if (window->IsSidebarCollapsed() != hidden) {
    controller->ToggleSidebar();
  }
}

void AtriumSettingsWindow::HandleCustomEndpointApiKeyChanged(
    std::string api_key) {
  // The second lock: the row that sends this is absent from
  // the page while off, but a message that reaches here anyway must not
  // touch the keychain either.
  if (!AtriumBrowserWindow::AiEnabled()) {
    VLOG(1) << "Atrium: custom endpoint API key change refused - AI is off";
    return;
  }
#if BUILDFLAG(IS_MAC)
  SettingsWriteRunner()->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&StoreCustomEndpointApiKey, std::move(api_key)),
      base::BindOnce(&AtriumSettingsWindow::OnCustomEndpointApiKeySet,
                     weak_factory_.GetWeakPtr()));
#else
  OnCustomEndpointApiKeySet(false);
#endif
}

// static
void AtriumSettingsWindow::PushCustomEndpointApiKeyStatusToInstance() {
  if (Instance()) {
    Instance()->PushCustomEndpointApiKeyStatus();
  }
}

void AtriumSettingsWindow::OnCustomEndpointApiKeySet(bool ok) {
  if (!ok) {
    LOG(WARNING) << "Atrium: the custom endpoint API key could not be saved";
  }
  PushCustomEndpointApiKeyStatus();
}

void AtriumSettingsWindow::SetAlwaysOnTop(bool floating) {
  if (!widget_) {
    return;
  }
  always_on_top_ = floating;
  widget_->SetZOrderLevel(floating ? ui::ZOrderLevel::kFloatingWindow
                                   : ui::ZOrderLevel::kNormal);
  // The level alone is not enough, as shown on the
  // quit-confirm panel: a browser window in native full screen owns its own
  // macOS Space, and nothing outside that Space draws over it at any level.
  widget_->SetCanAppearInExistingFullscreenSpaces(floating);
  VLOG(1) << "Atrium: settings window always-on-top " << floating;
  PushAlwaysOnTop();
}

void AtriumSettingsWindow::PushAlwaysOnTop() {
  CallPage(base::UTF8ToUTF16(
      "window.atriumSetSettingsAlwaysOnTop && "
      "atriumSetSettingsAlwaysOnTop(" +
      std::string(always_on_top_ ? "true" : "false") + ")"));
}

void AtriumSettingsWindow::PushOverlayTheme() {
  if (!renderer_ready_ || !web_contents_ ||
      !web_contents_->GetPrimaryMainFrame()) {
    return;
  }
  const WindowAppearance& appearance = AtriumBrowserWindow::CurrentAppearance();
  // Native app view and the surround colour are ignored: settings always
  // looks like a macOS system window.
#if BUILDFLAG(IS_MAC)
  const SkColor ground = SystemWindowBackgroundColor(
      widget_->GetColorMode() == ui::ColorProviderKey::ColorMode::kDark);
#else
  const SkColor ground =
      widget_->GetColorProvider()->GetColor(ui::kColorPrimaryBackground);
#endif
  const SkColor ink =
      color_utils::GetColorWithMaxContrast(SkColorSetA(ground, SK_AlphaOPAQUE));
  // The settings surface wears the accent it is offering, so the System choice
  // has to reach it too, and by the slug's own route rather than as a class.
  const std::string theme = appearance.overlay_theme.value_or(std::string());
  const bool system = theme == atrium::kSystemOverlayAccent;
  web_contents_->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(atrium::OverlayThemeScript(
          /*tint=*/0, /*has_tint=*/false, /*native=*/true,
          static_cast<uint32_t>(ground), ink, system ? std::string() : theme,
          system ? atrium::SystemOverlayAccentTokens()
                 : std::optional<atrium::OverlayAccentTokens>())),
      base::NullCallback(), ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void AtriumSettingsWindow::PushCustomEndpointApiKeyStatus() {
  if (!renderer_ready_ || !web_contents_) {
    return;
  }
#if BUILDFLAG(IS_MAC)
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&atrium::HasCustomEndpointApiKey),
      base::BindOnce(&AtriumSettingsWindow::OnCustomEndpointApiKeyStatusRead,
                     weak_factory_.GetWeakPtr()));
#else
  OnCustomEndpointApiKeyStatusRead(false);
#endif
}

void AtriumSettingsWindow::OnCustomEndpointApiKeyStatusRead(bool has_key) {
  CallPage(base::UTF8ToUTF16(
      "window.atriumSetCustomEndpointApiKeyStatus && "
      "atriumSetCustomEndpointApiKeyStatus(" +
      std::string(has_key ? "true" : "false") + ")"));
}

void AtriumSettingsWindow::HandleCheckCustomEndpointConnection(
    std::string url,
    std::string model,
    int timeout_seconds) {
  if (!AtriumBrowserWindow::AiEnabled()) {
    VLOG(1) << "Atrium: custom endpoint connection check refused - AI is off";
    return;
  }
  GURL parsed(url);
  if (!parsed.is_valid() || !parsed.SchemeIs(url::kHttpsScheme)) {
    PushCustomEndpointConnectionResult(false, "Not a valid https:// URL");
    return;
  }

  network::mojom::ResolveHostParametersPtr parameters =
      network::mojom::ResolveHostParameters::New();
  // Intentionally a HostPortPair, the same choice DnsProbeRunner makes, so
  // this does not also trigger an HTTPS DNS resource record query.
  network::mojom::HostResolverHostPtr host =
      network::mojom::HostResolverHost::NewHostPortPair(
          net::HostPortPair::FromURL(parsed));

  custom_endpoint_resolver_ = network::SimpleHostResolver::Create(
      g_browser_process->system_network_context_manager()->GetContext());
  custom_endpoint_resolver_->ResolveHost(
      std::move(host), net::NetworkAnonymizationKey::CreateTransient(),
      std::move(parameters),
      base::BindOnce(
          [](base::WeakPtr<AtriumSettingsWindow> self, GURL url,
             std::string model, int timeout_seconds, int result,
             const net::ResolveErrorInfo& resolve_error_info,
             const net::AddressList& resolved_addresses,
             const net::HostResolverEndpointResults& endpoint_results) {
            if (!self) {
              return;
            }
            if (result != net::OK) {
              self->PushCustomEndpointConnectionResult(
                  false, "Could not resolve that host");
              return;
            }
            std::vector<net::IPAddress> addresses;
            for (const net::IPEndPoint& endpoint :
                 resolved_addresses.endpoints()) {
              addresses.push_back(endpoint.address());
            }
            self->OnCustomEndpointAddressChecked(
                std::move(url), std::move(model), timeout_seconds,
                !AnyResolvedAddressIsLocal(addresses));
          },
          weak_factory_.GetWeakPtr(), parsed, std::move(model),
          timeout_seconds));
}

void AtriumSettingsWindow::OnCustomEndpointAddressChecked(
    GURL url,
    std::string model,
    int timeout_seconds,
    bool safe_to_connect) {
  custom_endpoint_resolver_.reset();
  if (!safe_to_connect) {
    PushCustomEndpointConnectionResult(
        false, "That address is not reachable from here");
    return;
  }
  network::mojom::URLLoaderFactory* loader_factory =
      g_browser_process->system_network_context_manager()
          ->GetURLLoaderFactory();
  SettingsWriteRunner()->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](const GURL& probed_url) -> std::optional<std::string> {
            const std::optional<std::string> saved_url =
                SavedEndpointUrlFromJson(
                    LoadSettingsJSON(ResolveSettingsFilePath()));
            if (!saved_url || !ProbeMayCarryApiKey(probed_url, *saved_url)) {
              return std::nullopt;
            }
            return GetCustomEndpointApiKey();
          },
          url),
      base::BindOnce(&AtriumSettingsWindow::OnCustomEndpointProbeApiKeyFetched,
                     weak_factory_.GetWeakPtr(), url, std::move(model),
                     timeout_seconds, loader_factory));
}

void AtriumSettingsWindow::OnCustomEndpointProbeApiKeyFetched(
    GURL url,
    std::string model,
    int timeout_seconds,
    network::mojom::URLLoaderFactory* loader_factory,
    std::optional<std::string> api_key) {
  StartCustomEndpointProbe(url, model, api_key, loader_factory,
                           timeout_seconds);
}

void AtriumSettingsWindow::StartCustomEndpointProbeForTesting(
    const GURL& url,
    const std::string& model,
    const std::optional<std::string>& api_key,
    network::mojom::URLLoaderFactory* loader_factory,
    int timeout_seconds) {
  StartCustomEndpointProbe(url, model, api_key, loader_factory,
                           timeout_seconds);
}

void AtriumSettingsWindow::StartCustomEndpointProbe(
    const GURL& url,
    const std::string& model,
    const std::optional<std::string>& api_key,
    network::mojom::URLLoaderFactory* loader_factory,
    int timeout_seconds) {
  auto resource_request = std::make_unique<network::ResourceRequest>();
  resource_request->url = url;
  resource_request->method = "POST";
  resource_request->load_flags = net::LOAD_DISABLE_CACHE;
  resource_request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  if (api_key && !api_key->empty()) {
    resource_request->headers.SetHeader(
        net::HttpRequestHeaders::kAuthorization, "Bearer " + *api_key);
  }

  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("atrium_custom_endpoint_probe", R"(
        semantics {
          sender: "Atrium Custom Endpoint Connection Check"
          description:
            "Confirms the user's own BYOK URL (Settings > AI > Endpoint) "
            "answers a chat-completions request before the AI panel relies "
            "on it. It sends the same shape of request the panel sends, "
            "because a URL that merely answers can still be the wrong one."
          trigger: "The user clicks Test connection in Settings > AI."
          data: "A fixed one-word prompt - no page content - and the "
            "keychain-stored API key as a bearer credential, sent only to "
            "the endpoint URL the user configured."
          destination: OTHER
        }
        policy {
          cookies_allowed: NO
          setting: "Only reached while AI is enabled and a custom endpoint "
            "URL is set."
          policy_exception_justification:
              "Not implemented, considered not useful - this is a user "
              "initiated check of a URL the user typed themselves."
        })");
  custom_endpoint_probe_loader_ = network::SimpleURLLoader::Create(
      std::move(resource_request), traffic_annotation);
  custom_endpoint_probe_loader_->SetTimeoutDuration(
      timeout_seconds > 0 ? base::Seconds(timeout_seconds)
                          : kCustomEndpointProbeTimeout);
  // the resolve above cannot bind this connection, because the
  // loader resolves again under its own key. This re-checks the address the
  // probe really reached, the same second line the summarize path now has.
  custom_endpoint_probe_loader_->SetURLLoaderFactoryOptions(
      network::mojom::kURLLoadOptionBlockLocalRequest);
  // refused outright rather than pinned - it is safe to
  // delete the loader from inside this callback, which is what cancels it.
  custom_endpoint_probe_loader_->SetOnRedirectCallback(base::BindRepeating(
      [](base::WeakPtr<AtriumSettingsWindow> self,
         const GURL& url_before_redirect,
         const net::RedirectInfo& redirect_info,
         const network::mojom::URLResponseHead& response_head,
         std::vector<std::string>* removed_headers) {
        if (self) {
          self->OnCustomEndpointRedirectRefused();
        }
      },
      weak_factory_.GetWeakPtr()));

  custom_endpoint_probe_loader_->AttachStringForUpload(
      atrium_ai::BuildCustomEndpointChatRequestBody(
          model, kProbePageText, std::string(), kProbeMaxTokens),
      "application/json");
  custom_endpoint_probe_loader_->DownloadToString(
      loader_factory,
      base::BindOnce(
          [](base::WeakPtr<AtriumSettingsWindow> self,
             std::optional<std::string> response_body) {
            if (!self) {
              return;
            }
            network::SimpleURLLoader* loader =
                self->custom_endpoint_probe_loader_.get();
            const int net_error = loader->NetError();
            const int response_code =
                loader->ResponseInfo() && loader->ResponseInfo()->headers
                    ? loader->ResponseInfo()->headers->response_code()
                    : 0;
            self->custom_endpoint_probe_loader_.reset();
            // Not gated on response_code: an endpoint that sends headers and
            // then stalls has a status already, and the shape check below
            // would call that a malformed answer rather than a slow one.
            if (net_error == net::ERR_TIMED_OUT) {
              self->PushCustomEndpointConnectionResult(
                  false, "That endpoint was too slow to answer");
              return;
            }
            if (net_error != net::OK && response_code == 0) {
              self->PushCustomEndpointConnectionResult(
                  false, "Could not reach that endpoint");
              return;
            }
            if (response_code < 200 || response_code >= 300) {
              self->PushCustomEndpointConnectionResult(
                  false, "That endpoint answered " +
                             base::NumberToString(response_code));
              return;
            }
            if (!response_body ||
                !atrium_ai::ParseCustomEndpointChatResponse(*response_body)) {
              self->PushCustomEndpointConnectionResult(
                  false, "That URL answered, but not with chat completions");
              return;
            }
            self->PushCustomEndpointConnectionResult(true, "");
          },
          weak_factory_.GetWeakPtr()),
      kMaxCustomEndpointProbeResponseBytes);
}

void AtriumSettingsWindow::OnCustomEndpointRedirectRefused() {
  custom_endpoint_probe_loader_.reset();
  PushCustomEndpointConnectionResult(false,
                                     "That endpoint redirected elsewhere");
}

void AtriumSettingsWindow::PushCustomEndpointConnectionResult(
    bool reachable,
    std::string error) {
  if (CustomEndpointConnectionResultObserver()) {
    CustomEndpointConnectionResultObserver().Run(reachable, error);
  }
  std::string payload =
      reachable ? "{\"reachable\":true}"
               : "{\"reachable\":false,\"error\":" +
                     base::GetQuotedJSONString(error) + "}";
  CallPage(base::UTF8ToUTF16(
      "window.atriumSetCustomEndpointConnectionStatus && "
      "atriumSetCustomEndpointConnectionStatus(" +
      payload + ")"));
}

void AtriumSettingsWindow::OnWidgetActivationChanged(views::Widget* widget,
                                                     bool active) {
  // a scenario asking what Cmd+Q does to THIS window has to be able
  // to show the window actually held key when the chord landed. Without a line
  // here the premise is only inferable from a screenshot.
  if (active) {
    VLOG(1) << "Atrium: settings window gained key";
  }
}

void AtriumSettingsWindow::ClearPageBackground() {
#if BUILDFLAG(IS_MAC)
  if (content::RenderWidgetHostView* rwhv =
          web_contents_->GetRenderWidgetHostView()) {
    rwhv->SetBackgroundColor(SK_ColorTRANSPARENT);
  }
  web_contents_->SetPageBaseBackgroundColor(SK_ColorTRANSPARENT);
#endif
}

// A macOS light/dark switch lands here, and the pushed ground has to follow.
void AtriumSettingsWindow::OnWidgetThemeChanged(views::Widget* widget) {
  PushOverlayTheme();
}

void AtriumSettingsWindow::OnWidgetDestroying(views::Widget* widget) {
  widget->RemoveObserver(this);
  // Nothing else would: an unsaved scrub is painted on another window and
  // held by no file, and this page is about to stop existing.
  RevertLiveThemePreview();
  // The window IS the object's lifetime: a closed settings window leaves
  // nothing behind, and the next Show builds a fresh one.
  if (Instance() == this) {
    Instance() = nullptr;
  }
  // Cannot delete inside the callback - the widget is still unwinding.
  base::SequencedTaskRunner::GetCurrentDefault()->DeleteSoon(FROM_HERE, this);
}

bool ProbeMayCarryApiKey(const GURL& probed_url, const std::string& saved_url) {
  const GURL saved(saved_url);
  return saved.is_valid() && probed_url == saved;
}

bool SavedEndpointUrlChanged(
    const std::optional<std::string>& old_settings_file,
    const std::string& new_settings_json) {
  const std::optional<std::string> new_url =
      SavedEndpointUrlFromJson(new_settings_json);
  if (!new_url) {
    return true;
  }
  std::string old_url;
  if (old_settings_file) {
    const std::optional<std::string> parsed_old =
        SavedEndpointUrlFromJson(*old_settings_file);
    if (!parsed_old) {
      return true;
    }
    old_url = *parsed_old;
  }
  if (old_url.empty()) {
    return !new_url->empty();
  }
  const GURL old_parsed(old_url);
  const GURL new_parsed(*new_url);
  if (old_parsed.is_valid() && new_parsed.is_valid()) {
    return old_parsed != new_parsed;
  }
  return old_url != *new_url;
}

bool ApiKeyWriteAllowed(const std::string& api_key,
                        const std::optional<std::string>& settings_file) {
  return api_key.empty() || HasSavedEndpointUrl(settings_file);
}

bool ShouldDeleteOrphanApiKey(const std::optional<std::string>& settings_file,
                              bool file_unreadable,
                              base::FunctionRef<bool()> has_key) {
  return !file_unreadable && !HasSavedEndpointUrl(settings_file) && has_key();
}

void DeleteOrphanCustomEndpointApiKey() {
#if BUILDFLAG(IS_MAC)
  SettingsWriteRunner()->PostTask(
      FROM_HERE, base::BindOnce([] {
        bool unreadable = false;
        const std::optional<std::string> file =
            ReadSettingsFileRaw(ResolveSettingsFilePath(), &unreadable);
        if (ShouldDeleteOrphanApiKey(
                file, unreadable, [] { return HasCustomEndpointApiKey(); })) {
          LOG_IF(WARNING, !SetCustomEndpointApiKey(""))
              << "Atrium: an orphan custom endpoint API key could not be "
                 "deleted";
        }
      }));
#endif
}

int32_t SaveSettingsClearingKey(
    const std::optional<std::string>& old_settings_file,
    const std::string& new_settings_json,
    base::FunctionRef<bool()> delete_key,
    base::FunctionRef<int32_t()> write) {
  if (SavedEndpointUrlChanged(old_settings_file, new_settings_json) &&
      !delete_key()) {
    return kSaveWriteFailed;
  }
  return write();
}

}  // namespace atrium
