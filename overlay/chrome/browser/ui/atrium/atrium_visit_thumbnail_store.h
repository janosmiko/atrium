// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_visit_thumbnail_store.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_VISIT_THUMBNAIL_STORE_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_VISIT_THUMBNAIL_STORE_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <string_view>

namespace atrium {

// Matches visits.rs MAX_VISITS, and must: a stop's picture is found by
// POSITION, so a store that evicted on another schedule would show every stop
// somebody else's page.
inline constexpr size_t kMaxVisitThumbnails = 60;

// One slot per stop on a tab's visit timeline, holding that stop's
// capture as a ready-to-render `data:` URL. UI thread only, no lock, like the
// visit recording it mirrors.
//
// Cap and eviction: sixty stops per tab, oldest dropped past that.
// Nothing here reaches disk, so a capture cannot outlive the process.
// Worst case per tab is sixty JPEGs at the ~240px encode atrium_capture.cc
// produces, a couple of megabytes.
class VisitThumbnailStore {
 public:
  explicit VisitThumbnailStore(size_t capacity);
  ~VisitThumbnailStore();

  VisitThumbnailStore(const VisitThumbnailStore&) = delete;
  VisitThumbnailStore& operator=(const VisitThumbnailStore&) = delete;

  // Call only when atrium_core_record_visit reports it pushed a stop. Any
  // other trigger and the two logs stop lining up.
  void AddStop(uint64_t tab_id);

  // Stops this tab has EVER recorded, which keeps counting past the cap. A
  // capture carries the serial it started at, so one landing after the tab
  // navigated again is refused rather than stamped onto the wrong stop.
  uint64_t StopSerial(uint64_t tab_id) const;

  bool SetNewestThumbnail(uint64_t tab_id,
                          uint64_t expected_serial,
                          std::string data_url);

  // Empty when that stop carries no capture, or does not exist.
  std::string_view ThumbnailAt(uint64_t tab_id, size_t index) const;

  // Beside the core's forget_visits on tab close. This is what bounds the map.
  void Forget(uint64_t tab_id);

  size_t StopCount(uint64_t tab_id) const;
  size_t TabCount() const { return tabs_.size(); }
  size_t capacity() const { return capacity_; }

 private:
  struct TabThumbnails {
    std::deque<std::string> stops;
    uint64_t serial = 0;
  };

  const size_t capacity_;
  std::map<uint64_t, TabThumbnails> tabs_;
};

// The one the capture path writes and the timeline reply reads.
VisitThumbnailStore& VisitThumbnails();

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_VISIT_THUMBNAIL_STORE_H_
