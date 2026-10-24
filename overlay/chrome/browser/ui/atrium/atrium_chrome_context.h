// Copyright 2026 The Atrium Authors

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_CHROME_CONTEXT_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_CHROME_CONTEXT_H_

#include <memory>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "content/public/browser/web_contents_delegate.h"
#include "ui/views/controls/webview/unhandled_keyboard_event_handler.h"

class GURL;
class Profile;

namespace content {
class BrowserContext;
class SiteInstance;
class WebContents;
}  // namespace content

namespace atrium {

// The one BrowserContext every Atrium chrome surface renders in.
//
// Atrium binds one BrowserController per profile per window slot, because
// Chromium binds a Browser to a single Profile and spaces are profile-scoped.
// If each surface rendered in its presenter's own profile, a five-profile
// session would carry five chrome renderer processes. Pinning all of them to
// ONE reserved context instead collapses the whole chrome layer to a single
// renderer, invariant over window and profile count.
//
// The context is the system profile's off-the-record profile.
// ProfileManager's async creation path already hands back the OTR profile for
// it - ShouldGoOffTheRecord returns true for IsSystemProfile()
// (chrome/browser/profiles/profile_manager.cc:442-450) - so there is no
// GetPrimaryOTRProfile call here. This is the same profile the profile picker
// runs in.
//
// Sends a chrome surface's unhandled keys back to the window that owns it.
//
// A WebContents with no delegate drops them: WebContentsImpl::
// HandleKeyboardEvent short-circuits on `delegate_ && ...`, so the renderer's
// unhandled ack never becomes a redispatch and the chord dies silently
// (Cmd+S died that way on the foyer). PeekWidget already
// forwards this way; this is the same forwarding for every surface built
// through CreateChromeContents.
class ChromeSurfaceKeyForwarder : public content::WebContentsDelegate {
 public:
  ChromeSurfaceKeyForwarder();
  ~ChromeSurfaceKeyForwarder() override;

  // content::WebContentsDelegate:
  bool HandleKeyboardEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event) override;

 private:
  views::UnhandledKeyboardEventHandler unhandled_keyboard_event_handler_;
};

// Never construct a Browser in this context: Profile::AllowsBrowserWindows()
// is false for the system profile.
class AtriumChromeContext : public ProfileObserver {
 public:
  // Null until Start()'s async profile creation completes. Callers must
  // tolerate that: RegisterChromeWebUIConfigs() runs long before
  // PreBrowserStart, and GetWebUIConfigList calls IsWebUIEnabled with
  // arbitrary contexts.
  static AtriumChromeContext* GetIfExists();

  // Kicks off async creation. Safe to call once, from PreBrowserStart.
  static void Start();

  // Runs `closure` once the context exists - immediately if it already does.
  //
  // Needed because creation is async and loses the race: measured 2026-08-03,
  // the first chrome-surface widget is constructed ~670ms BEFORE the context
  // lands. A surface that only checked GetIfExists() at construction would
  // silently fall back to its SwiftUI arm for the process lifetime, and the
  // failure is invisible - the surface renders, just from the wrong arm.
  static void PostWhenReady(base::OnceClosure closure);

  AtriumChromeContext(const AtriumChromeContext&) = delete;
  AtriumChromeContext& operator=(const AtriumChromeContext&) = delete;

  content::BrowserContext* context() const;
  content::SiteInstance* site_instance() const { return site_instance_.get(); }

  // Builds a chrome-surface WebContents on the shared SiteInstance.
  //
  // Passing the SiteInstance explicitly is what makes the one-process property
  // structural rather than best-effort: it bypasses the IsSuitableHost policy
  // walk and the random-existing-host choice in RenderProcessHostImpl
  // entirely, so no process-limit or site-isolation heuristic can split the
  // chrome layer across renderers.
  std::unique_ptr<content::WebContents> CreateChromeContents(const GURL& route);

  // Test-only seam. Binds the singleton to `profile` synchronously, standing
  // in for the async ProfileManager path, so CreateChromeContents can be
  // exercised in a unit test. ResetForTesting() must run before `profile`
  // dies - the context holds it as a raw_ptr.
  static AtriumChromeContext* CreateForTesting(Profile* profile);
  static void ResetForTesting();

 private:
  AtriumChromeContext();
  ~AtriumChromeContext() override;

  void OnProfileReady(Profile* profile);

  // The context never dies, but the profile does at shutdown.
  void OnProfileWillBeDestroyed(Profile* profile) override;

  std::vector<base::OnceClosure> ready_callbacks_;

  raw_ptr<Profile> profile_ = nullptr;
  std::unique_ptr<ScopedProfileKeepAlive> keep_alive_;
  base::ScopedObservation<Profile, ProfileObserver> profile_observation_{this};
  scoped_refptr<content::SiteInstance> site_instance_;
  // Outlives every surface: the context is a leaked singleton. A surface that
  // wants its own delegate (the peek card, the quick menu) just calls
  // SetDelegate again after CreateChromeContents returns.
  ChromeSurfaceKeyForwarder key_forwarder_;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_CHROME_CONTEXT_H_
