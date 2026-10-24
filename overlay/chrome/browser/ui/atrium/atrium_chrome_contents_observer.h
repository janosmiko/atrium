// Copyright 2026 The Atrium Authors

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_CHROME_CONTENTS_OBSERVER_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_CHROME_CONTENTS_OBSERVER_H_

#include "base/process/kill.h"
#include "base/time/time.h"
#include "content/public/browser/web_contents_observer.h"

namespace content {
class WebContents;
}

namespace atrium {

// Recovers a chrome surface whose renderer died.
//
// Every chrome surface shares one renderer process (see AtriumChromeContext),
// which is what keeps the layer to a single process - but it also means one
// crash takes down the sidebar, foyer, command bar, quick menu and settings
// at once.
// Recovery is therefore not optional the way it is for a dismissible bubble.
// Peek is not on this list: it shows the previewed tab's own WebContents, not
// a chrome-layer route.
//
// Deliberately unlike upstream: WebUIContentsWrapper handles the same signal by
// closing the UI (webui_contents_wrapper.cc:285-288), which is right for a
// bubble the user can reopen. Atrium's chrome layer is the window furniture -
// closing it would leave a window with no sidebar and no way to get one back.
class AtriumChromeContentsObserver : public content::WebContentsObserver {
 public:
  explicit AtriumChromeContentsObserver(content::WebContents* contents);
  AtriumChromeContentsObserver(const AtriumChromeContentsObserver&) = delete;
  AtriumChromeContentsObserver& operator=(const AtriumChromeContentsObserver&) =
      delete;
  ~AtriumChromeContentsObserver() override;

  // content::WebContentsObserver:
  void PrimaryMainFrameRenderProcessGone(
      base::TerminationStatus status) override;

 private:
  // A renderer that crashes on load would otherwise reload forever, burning a
  // core and filling the log. After this many crashes inside the window below,
  // stop reloading and leave the surface blank - a blank sidebar is bad, a
  // spinning one is worse and hides the original crash.
  static constexpr int kMaxReloads = 3;
  static constexpr base::TimeDelta kReloadWindow = base::Minutes(1);

  int reloads_in_window_ = 0;
  base::TimeTicks window_started_;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_CHROME_CONTENTS_OBSERVER_H_
