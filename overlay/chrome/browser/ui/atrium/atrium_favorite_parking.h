// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_favorite_parking.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_FAVORITE_PARKING_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_FAVORITE_PARKING_H_

#include <map>
#include <memory>
#include <optional>
#include <string>

#include "base/time/time.h"
#include "base/timer/timer.h"

namespace content {
class WebContents;
}

namespace atrium {

// How long settings.json keeps a closed favourite loaded, or nullopt when the
// keep-alive is off.
std::optional<base::TimeDelta> FavoriteKeepAliveFromSettings(
    const std::string& settings_json);

// The pages of closed favourites, kept loaded so a tile click shows the same
// page again. At most one page per favourite.
class AtriumFavoriteParking {
 public:
  AtriumFavoriteParking();
  AtriumFavoriteParking(const AtriumFavoriteParking&) = delete;
  AtriumFavoriteParking& operator=(const AtriumFavoriteParking&) = delete;
  ~AtriumFavoriteParking();

  // Deletes a page already parked for `favorite_url`.
  void Park(const std::string& favorite_url,
            std::unique_ptr<content::WebContents> contents,
            base::TimeDelta keep_alive);

  // Null when nothing is parked for `favorite_url`.
  std::unique_ptr<content::WebContents> Take(const std::string& favorite_url);

  void Drop(const std::string& favorite_url);
  void Clear();

  content::WebContents* ParkedForTesting(const std::string& favorite_url) const;

  // Replaces every keep-alive time, so a test does not wait minutes.
  static void SetKeepAliveForTesting(std::optional<base::TimeDelta> keep_alive);

 private:
  struct Entry {
    Entry();
    ~Entry();
    std::unique_ptr<content::WebContents> contents;
    bool muted_by_park = false;
    base::OneShotTimer timer;
  };

  std::map<std::string, std::unique_ptr<Entry>> entries_;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_FAVORITE_PARKING_H_
