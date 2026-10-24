// Copyright 2026 The Atrium Authors

#include "chrome/browser/ui/atrium/atrium_chrome_contents_observer.h"

#include "base/logging.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"

namespace atrium {

AtriumChromeContentsObserver::AtriumChromeContentsObserver(
    content::WebContents* contents)
    : content::WebContentsObserver(contents),
      window_started_(base::TimeTicks::Now()) {}

AtriumChromeContentsObserver::~AtriumChromeContentsObserver() = default;

void AtriumChromeContentsObserver::PrimaryMainFrameRenderProcessGone(
    base::TerminationStatus status) {
  // Shutdown kills every renderer on purpose and the surface's widget is
  // already coming down, so a reload here rebuilds a RenderWidgetHostView onto
  // a parent ui::Layer with no compositor - a CHECK in BrowserCompositorMac.
  if (browser_shutdown::HasShutdownStarted()) {
    return;
  }
  const base::TimeTicks now = base::TimeTicks::Now();
  if (now - window_started_ > kReloadWindow) {
    window_started_ = now;
    reloads_in_window_ = 0;
  }

  if (++reloads_in_window_ > kMaxReloads) {
    LOG(ERROR) << "atrium: chrome renderer died " << reloads_in_window_
               << " times within " << kReloadWindow.InSeconds()
               << "s; leaving the surface blank rather than reload-looping. "
                  "status=" << static_cast<int>(status);
    return;
  }

  LOG(WARNING) << "atrium: chrome renderer died (status="
               << static_cast<int>(status) << "), reload "
               << reloads_in_window_ << "/" << kMaxReloads;
  web_contents()->GetController().Reload(content::ReloadType::NORMAL,
                                         /*check_for_repost=*/false);
}

}  // namespace atrium
