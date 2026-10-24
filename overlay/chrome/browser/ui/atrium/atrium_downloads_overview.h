// Copyright 2026 The Atrium Authors

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_DOWNLOADS_OVERVIEW_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_DOWNLOADS_OVERVIEW_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "base/timer/timer.h"
#include "base/values.h"

namespace content {
class DownloadManager;
class WebContents;
}  // namespace content

namespace download {
class DownloadItem;
}

namespace atrium {

// The id, name and accent of the Atrium space a download's tab lives in.
struct DownloadItemSpace {
  std::string id;
  std::string name;
  std::string accent;
};

// nullopt for a tab that resolves to no space. Injected so this file stays
// free of atrium_core and BrowserController - the caller already depends on
// both.
using SpaceForWebContents =
    base::RepeatingCallback<std::optional<DownloadItemSpace>(
        content::WebContents*)>;

// Excludes cancelled and transient (e.g. save-page-as helper) downloads that
// would otherwise flash a row nobody asked to see in the Overview list.
bool ShouldIncludeInDownloadsOverview(download::DownloadItem* item);

// One list-item dict for window.atriumSetDownloads, matching
// downloads-section.js's expected item shape.
base::DictValue DownloadItemToOverviewDict(
    download::DownloadItem* item,
    const SpaceForWebContents& resolve_space = SpaceForWebContents());

// The window.atriumSetDownloads(items) argument for every download
// `manager` knows about. Serialized directly, not through atrium_core -
// downloads live in Chromium's DownloadManager, not the Rust core.
std::string BuildDownloadsOverviewJson(
    content::DownloadManager* manager,
    const SpaceForWebContents& resolve_space = SpaceForWebContents());

// Dispatches `message_name` (downloadsShowInFinder/downloadsRemove/
// downloadsDeleteFromDisk) to the item `guid` names on `manager`. A guid
// with no match is a no-op, so a stale id can't act on the wrong file.
void DispatchDownloadsOverviewMessage(content::DownloadManager* manager,
                                      std::string_view message_name,
                                      const std::string& guid);

// Rate-limits the list push a download's byte-progress ticks cause: the first
// tick pushes at once, later ticks in the 250 ms window collapse to one
// trailing push. Pushes only while `is_showing` is true.
class DownloadsPushThrottle {
 public:
  DownloadsPushThrottle(base::RepeatingClosure push,
                        base::RepeatingCallback<bool()> is_showing);
  ~DownloadsPushThrottle();

  DownloadsPushThrottle(const DownloadsPushThrottle&) = delete;
  DownloadsPushThrottle& operator=(const DownloadsPushThrottle&) = delete;

  void OnCreatedOrRemoved();

  // A state change (complete, paused, ...) skips the window.
  void OnUpdated(bool state_changed);

 private:
  void Push();
  void StartWindow();
  void OnWindowEnd();

  const base::RepeatingClosure push_;
  const base::RepeatingCallback<bool()> is_showing_;
  base::OneShotTimer window_timer_;
  bool dirty_ = false;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_DOWNLOADS_OVERVIEW_H_
