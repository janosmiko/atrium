// Copyright 2026 The Atrium Authors
#include "chrome/browser/ui/atrium/atrium_downloads_overview.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "components/download/public/common/download_item.h"
#include "components/download/public/common/simple_download_manager.h"
#include "content/public/browser/download_item_utils.h"
#include "content/public/browser/download_manager.h"
#include "ui/base/text/bytes_formatting.h"

namespace atrium {

namespace {

// ByteSize's signed constructor is consteval - DownloadItem's byte counts are
// runtime int64_t, so this clamps and converts to the unsigned overload.
base::ByteSize NonNegativeByteSize(int64_t bytes) {
  return base::ByteSize(static_cast<uint64_t>(std::max<int64_t>(bytes, 0)));
}

}  // namespace

bool ShouldIncludeInDownloadsOverview(download::DownloadItem* item) {
  if (!item || item->IsTransient()) {
    return false;
  }
  return item->GetState() != download::DownloadItem::CANCELLED;
}

base::DictValue DownloadItemToOverviewDict(
    download::DownloadItem* item,
    const SpaceForWebContents& resolve_space) {
  const bool active = item->GetState() == download::DownloadItem::IN_PROGRESS;
  const base::Time received =
      active || item->GetEndTime().is_null() ? item->GetStartTime()
                                              : item->GetEndTime();

  base::DictValue dict;
  dict.Set("id", item->GetGuid());
  dict.Set("name", item->GetTargetFilePath().BaseName().AsUTF8Unsafe());
  dict.Set("status", active ? "active" : "complete");
  dict.Set("receivedAt",
           received.InMillisecondsFSinceUnixEpochIgnoringNull());
  if (active) {
    const int percent = item->PercentComplete();
    const int64_t total_bytes = item->GetTotalBytes();
    // No Content-Length from the server: Chromium answers 0 and -1 here.
    if (total_bytes <= 0 || percent < 0) {
      dict.Set("indeterminate", true);
      dict.Set("captionActive",
               base::UTF16ToUTF8(ui::FormatBytes(
                   NonNegativeByteSize(item->GetReceivedBytes()))));
    } else {
      dict.Set("progress", percent);
      dict.Set("captionActive",
               base::UTF16ToUTF8(ui::FormatBytes(
                   NonNegativeByteSize(item->GetReceivedBytes()))) +
                   " of " +
                   base::UTF16ToUTF8(ui::FormatBytes(
                       NonNegativeByteSize(total_bytes))));
    }
  } else {
    dict.Set("sizeLabel", base::UTF16ToUTF8(ui::FormatBytes(
                              NonNegativeByteSize(item->GetReceivedBytes()))));
  }

  if (!resolve_space.is_null()) {
    if (content::WebContents* contents =
            content::DownloadItemUtils::GetOriginalWebContents(item)) {
      if (std::optional<DownloadItemSpace> space =
              resolve_space.Run(contents)) {
        dict.Set("spaceId", space->id);
        dict.Set("spaceName", space->name);
        dict.Set("spaceAccent", space->accent);
      }
    }
  }
  return dict;
}

std::string BuildDownloadsOverviewJson(
    content::DownloadManager* manager,
    const SpaceForWebContents& resolve_space) {
  base::ListValue list;
  if (manager) {
    download::SimpleDownloadManager::DownloadVector items;
    manager->GetAllDownloads(&items);
    for (download::DownloadItem* item : items) {
      if (ShouldIncludeInDownloadsOverview(item)) {
        list.Append(DownloadItemToOverviewDict(item, resolve_space));
      }
    }
  }
  std::string json;
  base::JSONWriter::Write(list, &json);
  return json;
}

void DispatchDownloadsOverviewMessage(content::DownloadManager* manager,
                                      std::string_view message_name,
                                      const std::string& guid) {
  if (!manager) {
    return;
  }
  download::DownloadItem* item = manager->GetDownloadByGuid(guid);
  if (!item) {
    return;
  }
  if (message_name == "downloadsShowInFinder") {
    item->ShowDownloadInShell();
  } else if (message_name == "downloadsRemove") {
    item->Remove();
  } else if (message_name == "downloadsDeleteFromDisk") {
    item->DeleteFile(base::DoNothing());
    item->Remove();
  }
}

namespace {
constexpr base::TimeDelta kDownloadsPushWindow = base::Milliseconds(250);
}  // namespace

DownloadsPushThrottle::DownloadsPushThrottle(
    base::RepeatingClosure push,
    base::RepeatingCallback<bool()> is_showing)
    : push_(std::move(push)), is_showing_(std::move(is_showing)) {}

DownloadsPushThrottle::~DownloadsPushThrottle() = default;

void DownloadsPushThrottle::OnCreatedOrRemoved() {
  if (is_showing_.Run()) {
    Push();
  }
}

void DownloadsPushThrottle::OnUpdated(bool state_changed) {
  if (!is_showing_.Run()) {
    return;
  }
  if (state_changed) {
    Push();
    return;
  }
  if (window_timer_.IsRunning()) {
    dirty_ = true;
    return;
  }
  Push();
  StartWindow();
}

void DownloadsPushThrottle::Push() {
  dirty_ = false;
  push_.Run();
}

void DownloadsPushThrottle::StartWindow() {
  window_timer_.Start(FROM_HERE, kDownloadsPushWindow,
                      base::BindOnce(&DownloadsPushThrottle::OnWindowEnd,
                                     base::Unretained(this)));
}

void DownloadsPushThrottle::OnWindowEnd() {
  if (!std::exchange(dirty_, false) || !is_showing_.Run()) {
    return;
  }
  Push();
  StartWindow();
}

}  // namespace atrium
