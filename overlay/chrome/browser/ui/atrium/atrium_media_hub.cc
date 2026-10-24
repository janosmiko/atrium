// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_media_hub.cc
#include "chrome/browser/ui/atrium/atrium_media_hub.h"

#include <algorithm>
#include <list>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/memory/ptr_util.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_collection.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/global_media_controls/media_notification_service.h"
#include "chrome/browser/ui/global_media_controls/media_notification_service_factory.h"
#include "components/global_media_controls/public/media_item_manager.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "services/media_session/public/cpp/media_metadata.h"
#include "url/gurl.h"

namespace atrium {

namespace {

using media_session::mojom::MediaSessionAction;

bool HasAction(const std::vector<MediaSessionAction>& actions,
               MediaSessionAction action) {
  return std::ranges::find(actions, action) != actions.end();
}

base::DictValue RecordToDict(const MediaRecord& record) {
  base::DictValue dict;
  // A decimal string, the same shape a sidebar row's "id" carries.
  dict.Set("id", base::NumberToString(record.tab_id));
  dict.Set("title", record.title);
  dict.Set("faviconURL", record.favicon_url);
  dict.Set("host", record.host);
  dict.Set("playing", record.playing);
  dict.Set("muted", record.muted);
  dict.Set("canPrev", record.can_prev);
  dict.Set("canNext", record.can_next);
  dict.Set("canSeek", record.can_seek);
  dict.Set("pictureInPicture", record.picture_in_picture);
  dict.Set("volume", record.volume);
  return dict;
}

std::map<Profile*, std::unique_ptr<AtriumMediaHub>>& Hubs() {
  static base::NoDestructor<std::map<Profile*, std::unique_ptr<AtriumMediaHub>>>
      hubs;
  return *hubs;
}

std::string FaviconUrlFor(content::WebContents* contents) {
  content::NavigationEntry* entry =
      contents->GetController().GetLastCommittedEntry();
  if (!entry || !entry->GetFavicon().valid) {
    return std::string();
  }
  return entry->GetFavicon().url.spec();
}

}  // namespace

MediaRecordList::MediaRecordList(std::string profile_id)
    : profile_id_(std::move(profile_id)) {}

MediaRecordList::~MediaRecordList() = default;

bool MediaRecordList::Upsert(const MediaRecord& record) {
  if (record.profile_id != profile_id_) {
    return false;
  }
  auto it = std::ranges::find(records_, record.tab_id, &MediaRecord::tab_id);
  if (it == records_.end()) {
    records_.push_back(record);
  } else {
    *it = record;
  }
  return true;
}

bool MediaRecordList::Remove(uint64_t tab_id) {
  return std::erase_if(records_, [tab_id](const MediaRecord& record) {
           return record.tab_id == tab_id;
         }) > 0;
}

bool MediaRecordList::Owns(uint64_t tab_id) const {
  return std::ranges::any_of(records_, [tab_id](const MediaRecord& record) {
    return record.tab_id == tab_id;
  });
}

void MediaRecordList::Clear() {
  records_.clear();
}

std::string MediaRecordList::ToJson() const {
  base::ListValue list;
  for (const MediaRecord& record : records_) {
    list.Append(RecordToDict(record));
  }
  std::string json;
  base::JSONWriter::Write(list, &json);
  return json;
}

std::string MediaProfileKey(Profile* profile) {
  std::string key = profile->GetBaseName().AsUTF8Unsafe();
  if (profile->IsOffTheRecord()) {
    key += "/otr";
  }
  return key;
}

AtriumMediaHub::TabWatch::TabWatch(AtriumMediaHub* hub,
                                   content::WebContents* contents)
    : content::WebContentsObserver(contents), hub_(hub) {
  content::MediaSession::Get(contents)->AddObserver(
      receiver_.BindNewPipeAndPassRemote());
}

AtriumMediaHub::TabWatch::~TabWatch() = default;

void AtriumMediaHub::TabWatch::MediaSessionInfoChanged(
    media_session::mojom::MediaSessionInfoPtr info) {
  content::WebContents* contents = web_contents();
  auto it = contents ? hub_->tab_volumes_.find(TabId(contents))
                     : hub_->tab_volumes_.end();
  // Audio focus enforcement is off outside ChromeOS, so the audio focus
  // manager unducks every session on any tab's play, pause, resume, or stop.
  if (it != hub_->tab_volumes_.end() && it->second < 1.0 &&
      info->state !=
          media_session::mojom::MediaSessionInfo::SessionState::kDucking) {
    if (content::MediaSession* session =
            content::MediaSession::GetIfExists(contents)) {
      session->SetDuckingVolumeMultiplier(it->second);
      session->StartDucking();
    }
  }
  hub_->ScheduleRebuild();
}

void AtriumMediaHub::TabWatch::MediaSessionMetadataChanged(
    const std::optional<media_session::MediaMetadata>& metadata) {
  hub_->ScheduleRebuild();
}

void AtriumMediaHub::TabWatch::MediaSessionActionsChanged(
    const std::vector<MediaSessionAction>& actions) {
  hub_->ScheduleRebuild();
}

void AtriumMediaHub::TabWatch::MediaSessionImagesChanged(
    const base::flat_map<media_session::mojom::MediaSessionImageType,
                         std::vector<media_session::MediaImage>>& images) {
  hub_->ScheduleRebuild();
}

void AtriumMediaHub::TabWatch::MediaSessionPositionChanged(
    const std::optional<media_session::MediaPosition>& position) {}

void AtriumMediaHub::TabWatch::WebContentsDestroyed() {
  hub_->ScheduleRebuild();
}

void AtriumMediaHub::TabWatch::TitleWasSet(content::NavigationEntry* entry) {
  hub_->ScheduleRebuild();
}

// static
AtriumMediaHub* AtriumMediaHub::ForProfile(Profile* profile) {
  if (!profile) {
    return nullptr;
  }
  auto it = Hubs().find(profile);
  if (it != Hubs().end()) {
    return it->second.get();
  }
  MediaNotificationService* service =
      MediaNotificationServiceFactory::GetForProfile(profile);
  if (!service || !service->media_item_manager()) {
    return nullptr;
  }
  auto hub = base::WrapUnique(new AtriumMediaHub(profile));
  AtriumMediaHub* raw = hub.get();
  Hubs()[profile] = std::move(hub);
  return raw;
}

AtriumMediaHub::AtriumMediaHub(Profile* profile)
    : profile_(profile),
      item_manager_(MediaNotificationServiceFactory::GetForProfile(profile)
                        ->media_item_manager()),
      records_(MediaProfileKey(profile)) {
  item_manager_->AddObserver(this);
  profile_->AddObserver(this);
}

AtriumMediaHub::~AtriumMediaHub() {
  if (item_manager_) {
    item_manager_->RemoveObserver(this);
  }
  if (profile_) {
    profile_->RemoveObserver(this);
  }
}

void AtriumMediaHub::OnItemListChanged() {
  ScheduleRebuild();
}

// A rebuild can drop the very TabWatch whose mojo callback asked for it, so it
// never runs inside one.
void AtriumMediaHub::ScheduleRebuild() {
  if (rebuild_pending_) {
    return;
  }
  rebuild_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&AtriumMediaHub::Rebuild,
                                weak_factory_.GetWeakPtr()));
}

void AtriumMediaHub::OnProfileWillBeDestroyed(Profile* profile) {
  item_manager_->RemoveObserver(this);
  item_manager_ = nullptr;
  profile_->RemoveObserver(this);
  profile_ = nullptr;
  watches_.clear();
  // Erasing the map entry deletes `this`, so nothing may touch a member after.
  Hubs().erase(profile);
}

void AtriumMediaHub::Rebuild() {
  rebuild_pending_ = false;
  if (!item_manager_) {
    return;
  }
  // The item manager lists a session only while it actively plays, so a tab
  // paused from the sidebar would vanish mid-use. It picks which tabs to
  // watch, and a watch then lives until mediaClosed or the tab does.
  // ponytail: a tab whose media ended keeps its controller until it closes.
  for (const std::string& item_id : item_manager_->GetActiveItemIds()) {
    content::WebContents* contents =
        content::MediaSession::GetWebContentsFromRequestId(item_id);
    if (!contents || contents->GetBrowserContext() != profile_) {
      continue;
    }
    const uint64_t tab_id = TabId(contents);
    if (tab_id && !watches_.contains(tab_id)) {
      watches_[tab_id] = std::make_unique<TabWatch>(this, contents);
    }
  }

  records_.Clear();
  std::vector<uint64_t> gone;

  for (const auto& [tab_id, watch] : watches_) {
    content::WebContents* contents = watch->web_contents();
    content::MediaSession* session =
        contents ? content::MediaSession::GetIfExists(contents) : nullptr;
    media_session::mojom::MediaSessionInfoPtr info =
        session ? session->GetMediaSessionInfoSync() : nullptr;
    if (!info) {
      gone.push_back(tab_id);
      continue;
    }
    const std::vector<MediaSessionAction> actions =
        session->GetMediaSessionActionsSync();
    const media_session::MediaMetadata& metadata =
        session->GetMediaSessionMetadata();

    MediaRecord record;
    record.tab_id = tab_id;
    record.profile_id = records_.profile_id();
    record.title = base::UTF16ToUTF8(
        metadata.title.empty() ? contents->GetTitle() : metadata.title);
    record.favicon_url = FaviconUrlFor(contents);
    record.host = contents->GetLastCommittedURL().host();
    record.playing = info->playback_state ==
                     media_session::mojom::MediaPlaybackState::kPlaying;
    // The tab's own mute, not the page's: that is the one the browser can
    // always honour, and it is what the sidebar's button does.
    record.muted = contents->IsAudioMuted();
    record.can_prev = HasAction(actions, MediaSessionAction::kPreviousTrack);
    record.can_next = HasAction(actions, MediaSessionAction::kNextTrack);
    record.can_seek = HasAction(actions, MediaSessionAction::kSeekTo) ||
                      HasAction(actions, MediaSessionAction::kSeekForward);
    record.picture_in_picture =
        info->picture_in_picture_state ==
        media_session::mojom::MediaPictureInPictureState::kInPictureInPicture;
    auto volume_it = tab_volumes_.find(tab_id);
    record.volume = volume_it == tab_volumes_.end() ? 1.0 : volume_it->second;
    records_.Upsert(record);
  }

  for (uint64_t tab_id : gone) {
    watches_.erase(tab_id);
    tab_volumes_.erase(tab_id);
  }
  Push();
}

void AtriumMediaHub::PushForNewSurface() {
  Rebuild();
  pushed_json_ = records_.ToJson();
  AtriumBrowserWindow::PushMediaSessionsToProfile(profile_, pushed_json_);
}

void AtriumMediaHub::Push() {
  std::string json = records_.ToJson();
  if (json == pushed_json_) {
    return;
  }
  pushed_json_ = json;
  AtriumBrowserWindow::PushMediaSessionsToProfile(profile_, json);
}

content::WebContents* AtriumMediaHub::ContentsFor(uint64_t tab_id) const {
  if (!records_.Owns(tab_id)) {
    return nullptr;
  }
  auto it = watches_.find(tab_id);
  return it == watches_.end() ? nullptr : it->second->web_contents();
}

void AtriumMediaHub::PlayPause(uint64_t tab_id) {
  content::WebContents* contents = ContentsFor(tab_id);
  content::MediaSession* session =
      contents ? content::MediaSession::GetIfExists(contents) : nullptr;
  if (!session) {
    return;
  }
  media_session::mojom::MediaSessionInfoPtr info =
      session->GetMediaSessionInfoSync();
  if (info && info->playback_state ==
                  media_session::mojom::MediaPlaybackState::kPlaying) {
    session->Suspend(content::MediaSession::SuspendType::kUI);
  } else {
    session->Resume(content::MediaSession::SuspendType::kUI);
  }
}

void AtriumMediaHub::PreviousTrack(uint64_t tab_id) {
  content::WebContents* contents = ContentsFor(tab_id);
  if (content::MediaSession* session =
          contents ? content::MediaSession::GetIfExists(contents) : nullptr) {
    session->PreviousTrack();
  }
}

void AtriumMediaHub::NextTrack(uint64_t tab_id) {
  content::WebContents* contents = ContentsFor(tab_id);
  if (content::MediaSession* session =
          contents ? content::MediaSession::GetIfExists(contents) : nullptr) {
    session->NextTrack();
  }
}

void AtriumMediaHub::ToggleMute(uint64_t tab_id) {
  if (content::WebContents* contents = ContentsFor(tab_id)) {
    contents->SetAudioMuted(!contents->IsAudioMuted());
    ScheduleRebuild();
  }
}

void AtriumMediaHub::TogglePictureInPicture(uint64_t tab_id) {
  content::WebContents* contents = ContentsFor(tab_id);
  content::MediaSession* session =
      contents ? content::MediaSession::GetIfExists(contents) : nullptr;
  if (!session) {
    return;
  }
  media_session::mojom::MediaSessionInfoPtr info =
      session->GetMediaSessionInfoSync();
  if (info && info->picture_in_picture_state ==
                  media_session::mojom::MediaPictureInPictureState::
                      kInPictureInPicture) {
    session->ExitPictureInPicture();
  } else {
    session->EnterPictureInPicture();
  }
}

void AtriumMediaHub::SetVolume(uint64_t tab_id, double level) {
  content::WebContents* contents = ContentsFor(tab_id);
  content::MediaSession* session =
      contents ? content::MediaSession::GetIfExists(contents) : nullptr;
  if (!session) {
    return;
  }
  level = std::clamp(level, 0.0, 1.0);
  // Stored before StartDucking() so TabWatch::MediaSessionInfoChanged, which
  // reapplies this level whenever the audio focus manager unducks the
  // session, already has it even if that notification fires synchronously.
  tab_volumes_[tab_id] = level;
  session->SetDuckingVolumeMultiplier(level);
  if (level < 1.0) {
    // StartDucking() returns early while the session already ducks, so it
    // never pushes the new multiplier and every drag step after the first one
    // left the audio where the first one put it.
    session->StopDucking();
    session->StartDucking();
  } else {
    session->StopDucking();
  }
  ScheduleRebuild();
}

void AtriumMediaHub::BackToTab(uint64_t tab_id) {
  content::WebContents* contents = ContentsFor(tab_id);
  if (!contents) {
    return;
  }
  BrowserWindowInterface* window =
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(contents);
  Browser* browser = window ? window->GetBrowserForMigrationOnly() : nullptr;
  BrowserController* controller =
      browser ? BrowserController::ControllerForBrowser(browser) : nullptr;
  if (!controller) {
    return;
  }
  BrowserController::ActivateTab(controller, tab_id);
  BrowserController::ActivateWindow(controller);
}

void AtriumMediaHub::CloseMedia(uint64_t tab_id) {
  content::WebContents* contents = ContentsFor(tab_id);
  if (!contents) {
    return;
  }
  content::MediaSession* session = content::MediaSession::GetIfExists(contents);
  media_session::mojom::MediaSessionInfoPtr info =
      session ? session->GetMediaSessionInfoSync() : nullptr;
  // Stop() DCHECKs on a session that already let its audio focus go.
  if (info && info->state != media_session::mojom::MediaSessionInfo::
                                 SessionState::kInactive) {
    session->Stop(content::MediaSession::SuspendType::kUI);
  }
  records_.Remove(tab_id);
  watches_.erase(tab_id);
  tab_volumes_.erase(tab_id);
  Push();
}

}  // namespace atrium
