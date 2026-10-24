// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_media_hub.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_MEDIA_HUB_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_MEDIA_HUB_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "components/global_media_controls/public/media_item_manager_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "services/media_session/public/cpp/media_image.h"
#include "services/media_session/public/cpp/media_metadata.h"
#include "services/media_session/public/cpp/media_position.h"
#include "services/media_session/public/mojom/media_session.mojom.h"

class Profile;

namespace content {
class WebContents;
}

namespace global_media_controls {
class MediaItemManager;
}

namespace atrium {

// One tab that plays media, as the sidebar sees it.
struct MediaRecord {
  uint64_t tab_id = 0;
  std::string profile_id;
  std::string title;
  std::string favicon_url;
  std::string host;
  bool playing = false;
  bool muted = false;
  bool can_prev = false;
  bool can_next = false;
  bool can_seek = false;
  bool picture_in_picture = false;
  double volume = 1.0;
};

// The records of ONE profile. A record or a command naming another profile is
// refused here, so a private window can neither read nor drive another
// profile's playback.
class MediaRecordList {
 public:
  explicit MediaRecordList(std::string profile_id);
  MediaRecordList(const MediaRecordList&) = delete;
  MediaRecordList& operator=(const MediaRecordList&) = delete;
  ~MediaRecordList();

  // False when `record` names another profile - it is not stored.
  bool Upsert(const MediaRecord& record);
  bool Remove(uint64_t tab_id);
  bool Owns(uint64_t tab_id) const;
  void Clear();

  const std::vector<MediaRecord>& records() const { return records_; }
  const std::string& profile_id() const { return profile_id_; }

  std::string ToJson() const;

 private:
  const std::string profile_id_;
  std::vector<MediaRecord> records_;
};

// The string that separates one profile's records from another's. An
// incognito profile's GetBaseName() answers its parent's path, so the
// off-the-record flag has to be part of the key.
std::string MediaProfileKey(Profile* profile);

// Membership comes from MediaItemManager, already scoped to this profile. Every
// field comes from content::MediaSession, because the item manager's items
// carry none of them.
class AtriumMediaHub : public global_media_controls::MediaItemManagerObserver,
                       public ProfileObserver {
 public:
  // Created on first use, destroyed with `profile`. Null when the profile has
  // no media notification service (some test and system profiles).
  static AtriumMediaHub* ForProfile(Profile* profile);

  AtriumMediaHub(const AtriumMediaHub&) = delete;
  AtriumMediaHub& operator=(const AtriumMediaHub&) = delete;
  ~AtriumMediaHub() override;

  // The sidebar's eight commands. Each is a no-op for a tab this hub does not
  // hold, which is what makes a foreign-profile tab id inert.
  void PlayPause(uint64_t tab_id);
  void PreviousTrack(uint64_t tab_id);
  void NextTrack(uint64_t tab_id);
  void ToggleMute(uint64_t tab_id);
  void TogglePictureInPicture(uint64_t tab_id);
  void SetVolume(uint64_t tab_id, double level);
  void BackToTab(uint64_t tab_id);
  void CloseMedia(uint64_t tab_id);

  // A sidebar document that has just announced itself holds nothing yet, so
  // this push skips the "unchanged" guard.
  void PushForNewSurface();

  const MediaRecordList& records() const { return records_; }

  // global_media_controls::MediaItemManagerObserver:
  void OnItemListChanged() override;
  void OnMediaDialogOpened() override {}
  void OnMediaDialogClosed() override {}

  // ProfileObserver:
  void OnProfileWillBeDestroyed(Profile* profile) override;

 private:
  // One watched tab: a media session observer for the state changes and a
  // WebContents observer for the tab going away.
  class TabWatch : public media_session::mojom::MediaSessionObserver,
                   public content::WebContentsObserver {
   public:
    TabWatch(AtriumMediaHub* hub, content::WebContents* contents);
    ~TabWatch() override;

    // media_session::mojom::MediaSessionObserver:
    void MediaSessionInfoChanged(
        media_session::mojom::MediaSessionInfoPtr info) override;
    void MediaSessionMetadataChanged(
        const std::optional<media_session::MediaMetadata>& metadata) override;
    void MediaSessionActionsChanged(
        const std::vector<media_session::mojom::MediaSessionAction>& actions)
        override;
    void MediaSessionImagesChanged(
        const base::flat_map<media_session::mojom::MediaSessionImageType,
                             std::vector<media_session::MediaImage>>& images)
        override;
    void MediaSessionPositionChanged(
        const std::optional<media_session::MediaPosition>& position) override;

    // content::WebContentsObserver:
    void WebContentsDestroyed() override;
    void TitleWasSet(content::NavigationEntry* entry) override;

   private:
    const raw_ptr<AtriumMediaHub> hub_;
    mojo::Receiver<media_session::mojom::MediaSessionObserver> receiver_{this};
  };

  explicit AtriumMediaHub(Profile* profile);

  void ScheduleRebuild();
  void Rebuild();
  // Pushes only when the list differs from what the sidebars already hold.
  void Push();

  // The watched tab for `tab_id`, or null.
  content::WebContents* ContentsFor(uint64_t tab_id) const;

  raw_ptr<Profile> profile_;
  raw_ptr<global_media_controls::MediaItemManager> item_manager_;
  MediaRecordList records_;
  std::map<uint64_t, std::unique_ptr<TabWatch>> watches_;
  // content exposes no getter for the ducking multiplier, so the level SetVolume
  // last applied is kept here and re-read into each rebuilt record. Cleared
  // when the tab's watch goes away.
  std::map<uint64_t, double> tab_volumes_;
  std::string pushed_json_;
  bool rebuild_pending_ = false;
  base::WeakPtrFactory<AtriumMediaHub> weak_factory_{this};
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_MEDIA_HUB_H_
