// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_visit_thumbnail_store.cc
#include "chrome/browser/ui/atrium/atrium_visit_thumbnail_store.h"

#include <utility>

#include "base/no_destructor.h"

namespace atrium {

VisitThumbnailStore::VisitThumbnailStore(size_t capacity)
    : capacity_(capacity) {}

VisitThumbnailStore::~VisitThumbnailStore() = default;

void VisitThumbnailStore::AddStop(uint64_t tab_id) {
  if (capacity_ == 0) {
    return;
  }
  TabThumbnails& tab = tabs_[tab_id];
  ++tab.serial;
  tab.stops.emplace_back();
  while (tab.stops.size() > capacity_) {
    tab.stops.pop_front();
  }
}

uint64_t VisitThumbnailStore::StopSerial(uint64_t tab_id) const {
  auto it = tabs_.find(tab_id);
  return it == tabs_.end() ? 0 : it->second.serial;
}

bool VisitThumbnailStore::SetNewestThumbnail(uint64_t tab_id,
                                             uint64_t expected_serial,
                                             std::string data_url) {
  if (data_url.empty()) {
    return false;
  }
  auto it = tabs_.find(tab_id);
  if (it == tabs_.end() || it->second.stops.empty() ||
      it->second.serial != expected_serial) {
    return false;
  }
  it->second.stops.back() = std::move(data_url);
  return true;
}

std::string_view VisitThumbnailStore::ThumbnailAt(uint64_t tab_id,
                                                  size_t index) const {
  auto it = tabs_.find(tab_id);
  if (it == tabs_.end() || index >= it->second.stops.size()) {
    return std::string_view();
  }
  return it->second.stops[index];
}

void VisitThumbnailStore::Forget(uint64_t tab_id) {
  tabs_.erase(tab_id);
}

size_t VisitThumbnailStore::StopCount(uint64_t tab_id) const {
  auto it = tabs_.find(tab_id);
  return it == tabs_.end() ? 0 : it->second.stops.size();
}

VisitThumbnailStore& VisitThumbnails() {
  static base::NoDestructor<VisitThumbnailStore> store(kMaxVisitThumbnails);
  return *store;
}

}  // namespace atrium
