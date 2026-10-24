// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_overview_controller.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_OVERVIEW_CONTROLLER_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_OVERVIEW_CONTROLLER_H_

#include <memory>
#include <string>
#include <string_view>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "chrome/browser/ui/atrium/atrium_downloads_overview.h"
#include "components/download/content/public/all_download_item_notifier.h"

namespace content {
class DownloadManager;
}

namespace atrium {

class AtriumBrowserWindow;

// Owns no window: Overview is a pane of the sidebar's rail, so every push runs
// in the SIDEBAR's document. The widget this replaces covered the page
// File name lags the class - a rename needs a patch regen.
class OverviewController : public download::AllDownloadItemNotifier::Observer {
 public:
  // `parent` outlives this object (owns it via AtriumBrowserWindow's
  // overview_controller_ member).
  explicit OverviewController(AtriumBrowserWindow* parent);
  ~OverviewController() override;

  OverviewController(const OverviewController&) = delete;
  OverviewController& operator=(const OverviewController&) = delete;

  // Every list the rail pane's sections read, sent in one burst. Called when
  // the rail settles on the Overview pane.
  void PushAllLists();

  // The rail pane's messages, forwarded by the sidebar's surface host.
  void OnSurfaceMessage(std::string_view name, const base::ListValue& args);

  // re-pushes the Shelf list after a shelve from OUTSIDE this
  // route (the sidebar's context menu or edge drop zone).
  void RefreshShelfList();

  // Pushes the Archived overview list. Public because Clear
  // archives from the sidebar and the Overview has to catch up.
  void PushArchivedList();

  // Public for the same reason PushArchivedList is: a space reorder arrives at
  // the sidebar and the Overview has to catch up.
  void PushSpacesList();

  // Switches the rail to `section`, for a deep link such as the shelf toast.
  void SelectSection(const std::string& section);

 private:
  // Runs `script` in the sidebar document, which hosts the Overview pane.
  void RunInOverviewSurfaces(const std::u16string& script);

  void PushNotesList();
  void PushShelfList();
  void PushSnoozedList();
  void PushDownloadsList();

  // Defers PushDownloadsList() by one task - see OnDownloadRemoved's comment.
  void PostPushDownloadsList();

  // download::AllDownloadItemNotifier::Observer: pushes only while the
  // Overview shows (PushAllLists covers the open), byte ticks throttled.
  void OnDownloadCreated(content::DownloadManager* manager,
                        download::DownloadItem* item) override;
  void OnDownloadUpdated(content::DownloadManager* manager,
                        download::DownloadItem* item) override;
  void OnDownloadRemoved(content::DownloadManager* manager,
                        download::DownloadItem* item) override;

  // Restore and Wake's shared reopen step. A negative `space_index` means the
  // originating space is gone, so the active one is left alone.
  void ReopenInOriginatingSpace(int32_t space_index, const std::string& url);

  const raw_ptr<AtriumBrowserWindow> parent_;

  // Null for the lifetime of a context with no DownloadManager.
  std::unique_ptr<download::AllDownloadItemNotifier> download_notifier_;

  DownloadsPushThrottle downloads_push_throttle_;
  bool push_pending_ = false;

  // Must be last - see base/memory/weak_ptr.h.
  base::WeakPtrFactory<OverviewController> weak_factory_{this};
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_OVERVIEW_CONTROLLER_H_
