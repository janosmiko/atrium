// Copyright 2026 The Atrium Authors

#include "chrome/browser/ui/atrium/atrium_chrome_context.h"

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/supports_user_data.h"
#include "chrome/browser/browser_process.h"
#include "components/input/native_web_keyboard_event.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/atrium/atrium_chrome_contents_observer.h"
#include "chrome/common/webui_url_constants.h"
#include "content/public/browser/navigation_controller.h"
#include "ui/base/page_transition_types.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace atrium {

namespace {

AtriumChromeContext*& InstancePtr() {
  static AtriumChromeContext* instance = nullptr;
  return instance;
}

// The observer is a plain WebContentsObserver, not a WebContentsUserData, so
// it has no lifetime of its own. Holding it here ties it to the contents it
// watches without giving any of the five surface owners a member to carry.
class ChromeContentsObserverData : public base::SupportsUserData::Data {
 public:
  explicit ChromeContentsObserverData(content::WebContents* contents)
      : observer_(contents) {}

 private:
  AtriumChromeContentsObserver observer_;
};

// Address-of-static as the key, the standard Chromium idiom (see
// SurfaceHostData in atrium_surface_host.cc).
const void* const kChromeContentsObserverKey = &kChromeContentsObserverKey;

}  // namespace

ChromeSurfaceKeyForwarder::ChromeSurfaceKeyForwarder() = default;
ChromeSurfaceKeyForwarder::~ChromeSurfaceKeyForwarder() = default;

bool ChromeSurfaceKeyForwarder::HandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  // The hosting widget is not known until the surface is parented, so it is
  // looked up per event rather than stored.
  views::Widget* widget =
      views::Widget::GetTopLevelWidgetForNativeView(source->GetNativeView());
  if (!widget || !widget->GetFocusManager()) {
    return false;
  }
  return unhandled_keyboard_event_handler_.HandleKeyboardEvent(
      event, widget->GetFocusManager());
}

AtriumChromeContext::AtriumChromeContext() = default;
AtriumChromeContext::~AtriumChromeContext() = default;

// static
AtriumChromeContext* AtriumChromeContext::GetIfExists() {
  AtriumChromeContext* instance = InstancePtr();
  // Not ready until the profile has actually landed. Returning a half-built
  // instance would make IsWebUIEnabled compare against a null context and
  // silently enable the UI for every context, which is the failure this whole
  // class exists to prevent.
  return (instance && instance->profile_) ? instance : nullptr;
}

// static
void AtriumChromeContext::Start() {
  if (InstancePtr()) {
    return;
  }
  auto* instance = new AtriumChromeContext();
  InstancePtr() = instance;

  ProfileManager* manager = g_browser_process->profile_manager();
  if (!manager) {
    LOG(ERROR) << "atrium: no ProfileManager; chrome layer will stay disabled";
    return;
  }
  manager->CreateProfileAsync(
      ProfileManager::GetSystemProfilePath(),
      base::BindOnce(&AtriumChromeContext::OnProfileReady,
                     base::Unretained(instance)));
}

// static
void AtriumChromeContext::PostWhenReady(base::OnceClosure closure) {
  if (GetIfExists()) {
    std::move(closure).Run();
    return;
  }
  AtriumChromeContext* instance = InstancePtr();
  if (!instance) {
    // Start() has not run. Dropping is correct: without the dylib there is no
    // chrome layer, and a queued closure would never fire anyway.
    return;
  }
  instance->ready_callbacks_.push_back(std::move(closure));
}

void AtriumChromeContext::OnProfileReady(Profile* profile) {
  if (!profile) {
    LOG(ERROR) << "atrium: system profile creation failed; chrome layer will "
                  "stay disabled";
    return;
  }

  // GetOriginalProfile() is required, not defensive: ScopedProfileKeepAlive
  // does not support OTR profiles and DCHECKs if handed one - see the same
  // reasoning at chrome/browser/ui/views/profiles/profile_picker_view.cc
  // :614-621. TryAcquire rather than the constructor because ProfileManager
  // refuses a keep-alive on a profile already at refcount zero.
  keep_alive_ = ScopedProfileKeepAlive::TryAcquire(
      profile->GetOriginalProfile(),
      ProfileKeepAliveOrigin::kAtriumChromeWebUI);
  if (!keep_alive_) {
    LOG(ERROR) << "atrium: could not pin the chrome context alive; leaving the "
                  "chrome layer disabled rather than racing profile teardown";
    return;
  }

  site_instance_ =
      content::SiteInstance::CreateForURL(profile, GURL(chrome::kChromeUIAtriumURL));
  profile_ = profile;
  profile_observation_.Observe(profile);
  std::vector<base::OnceClosure> callbacks;
  callbacks.swap(ready_callbacks_);
  for (auto& callback : callbacks) {
    std::move(callback).Run();
  }

  LOG(INFO) << "atrium: chrome context ready, path="
            << profile->GetPath().AsUTF8Unsafe()
            << " otr=" << profile->IsOffTheRecord();
}

void AtriumChromeContext::OnProfileWillBeDestroyed(Profile* profile) {
  profile_observation_.Reset();
  profile_ = nullptr;
  site_instance_.reset();
}

content::BrowserContext* AtriumChromeContext::context() const {
  return profile_;
}

std::unique_ptr<content::WebContents> AtriumChromeContext::CreateChromeContents(
    const GURL& route) {
  CHECK(profile_) << "CreateChromeContents before the context is ready";
  content::WebContents::CreateParams params(profile_);
  // Always the shared SiteInstance - see the header for why this is what makes
  // the single-renderer property structural.
  params.site_instance = site_instance_;
  std::unique_ptr<content::WebContents> contents =
      content::WebContents::Create(params);
  CHECK_EQ(contents->GetBrowserContext(), profile_.get());

  // Every chrome surface funnels through here, so attaching once covers all of
  // them. Before the navigation below, so a renderer that dies on the very
  // first load is recovered too.
  contents->SetUserData(
      kChromeContentsObserverKey,
      std::make_unique<ChromeContentsObserverData>(contents.get()));

  // Without a delegate the surface swallows every key the renderer does not
  // handle. Surfaces that need their own delegate overwrite this.
  contents->SetDelegate(&key_forwarder_);

  // Navigate. Without this the WebContents exists but renders nothing - it
  // shows up as a page target with an empty URL and an empty title, which
  // looks like "the surface was never created" rather than "the surface was
  // created and never told where to go".
  content::NavigationController::LoadURLParams load(route);
  load.transition_type = ui::PAGE_TRANSITION_AUTO_TOPLEVEL;
  contents->GetController().LoadURLWithParams(load);
  return contents;
}

// static
AtriumChromeContext* AtriumChromeContext::CreateForTesting(Profile* profile) {
  CHECK(profile);
  AtriumChromeContext*& instance = InstancePtr();
  if (!instance) {
    instance = new AtriumChromeContext();
  }
  instance->site_instance_ = content::SiteInstance::CreateForURL(
      profile, GURL(chrome::kChromeUIAtriumURL));
  instance->profile_ = profile;
  return instance;
}

// static
void AtriumChromeContext::ResetForTesting() {
  AtriumChromeContext* instance = InstancePtr();
  if (!instance) {
    return;
  }
  instance->profile_ = nullptr;
  instance->site_instance_.reset();
  instance->ready_callbacks_.clear();
}

}  // namespace atrium
