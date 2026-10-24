// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_favorite_parking.cc
#include "chrome/browser/ui/atrium/atrium_favorite_parking.h"

#include <utility>

#include "base/functional/bind.h"
#include "content/public/browser/web_contents.h"
#include "third_party/atrium/core/include/atrium_core.h"

namespace atrium {

namespace {

std::optional<base::TimeDelta>& KeepAliveOverride() {
  static std::optional<base::TimeDelta> keep_alive;
  return keep_alive;
}

}  // namespace

std::optional<base::TimeDelta> FavoriteKeepAliveFromSettings(
    const std::string& settings_json) {
  const double seconds =
      atrium_core_favorite_keep_alive_seconds(settings_json.c_str());
  if (seconds < 0.0) {
    return std::nullopt;
  }
  return base::Seconds(seconds);
}

AtriumFavoriteParking::Entry::Entry() = default;
AtriumFavoriteParking::Entry::~Entry() = default;

AtriumFavoriteParking::AtriumFavoriteParking() = default;
AtriumFavoriteParking::~AtriumFavoriteParking() = default;

void AtriumFavoriteParking::Park(const std::string& favorite_url,
                                 std::unique_ptr<content::WebContents> contents,
                                 base::TimeDelta keep_alive) {
  auto entry = std::make_unique<Entry>();
  entry->muted_by_park = !contents->IsAudioMuted();
  if (entry->muted_by_park) {
    contents->SetAudioMuted(true);
  }
  entry->contents = std::move(contents);
  // Unretained: the timer is inside an entry this object owns.
  entry->timer.Start(FROM_HERE, KeepAliveOverride().value_or(keep_alive),
                     base::BindOnce(&AtriumFavoriteParking::Drop,
                                    base::Unretained(this), favorite_url));
  entries_[favorite_url] = std::move(entry);
}

std::unique_ptr<content::WebContents> AtriumFavoriteParking::Take(
    const std::string& favorite_url) {
  auto it = entries_.find(favorite_url);
  if (it == entries_.end()) {
    return nullptr;
  }
  std::unique_ptr<Entry> entry = std::move(it->second);
  entries_.erase(it);
  if (entry->muted_by_park) {
    entry->contents->SetAudioMuted(false);
  }
  return std::move(entry->contents);
}

void AtriumFavoriteParking::Drop(const std::string& favorite_url) {
  entries_.erase(favorite_url);
}

void AtriumFavoriteParking::Clear() {
  entries_.clear();
}

content::WebContents* AtriumFavoriteParking::ParkedForTesting(
    const std::string& favorite_url) const {
  auto it = entries_.find(favorite_url);
  return it == entries_.end() ? nullptr : it->second->contents.get();
}

// static
void AtriumFavoriteParking::SetKeepAliveForTesting(
    std::optional<base::TimeDelta> keep_alive) {
  KeepAliveOverride() = keep_alive;
}

}  // namespace atrium
