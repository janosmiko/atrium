// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_overview_controller.mm
#include "chrome/browser/ui/atrium/atrium_overview_controller.h"

#include <optional>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/json/string_escape.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_downloads_overview.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_tabs.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "ui/base/text/bytes_formatting.h"

namespace atrium {

namespace {

// Matches atrium_browser_window.cc's NotesInstantNow.
double NotesInstantNow() {
  return base::Time::Now().InSecondsFSinceUnixEpoch() - 978307200.0;
}

// A chip's name and color, by sidebar index - the slice of
// BuildSidebarTabsJSON's model SpaceForDownloadTab needs.
struct SpaceChipSummary {
  std::string name;
  std::string accent;
};

// One model build and parse per downloads push, not one per item.
std::vector<SpaceChipSummary> SidebarSpaceChips(Browser* browser) {
  std::vector<SpaceChipSummary> chips;
  const std::optional<base::Value> model = base::JSONReader::Read(
      BuildSidebarTabsJSON(browser), base::JSON_PARSE_RFC);
  const base::ListValue* spaces =
      model && model->is_dict() ? model->GetDict().FindList("spaces") : nullptr;
  if (!spaces) {
    return chips;
  }
  chips.reserve(spaces->size());
  for (const base::Value& space : *spaces) {
    const base::DictValue* chip = space.GetIfDict();
    const std::string* name = chip ? chip->FindString("name") : nullptr;
    const std::string* color = chip ? chip->FindString("color") : nullptr;
    chips.push_back(
        {name ? *name : std::string(), color ? *color : std::string()});
  }
  return chips;
}

// Downloads carry no Rust-side row, so id -> index -> chip is walked here,
// the same steps SpaceAccentColor takes for a woken snoozed tab. `chips` is
// SidebarSpaceChips' result, bound once per push rather than rebuilt here.
std::optional<DownloadItemSpace> SpaceForDownloadTab(
    const std::vector<SpaceChipSummary>& chips,
    content::WebContents* contents) {
  char* raw_id = atrium_core_space_id_for_tab(TabId(contents));
  if (!raw_id) {
    return std::nullopt;
  }
  const std::string space_id(raw_id);
  atrium_core_string_free(raw_id);

  char* raw_slot = atrium_core_space_slot_for_id(space_id.c_str());
  if (!raw_slot) {
    return std::nullopt;
  }
  const std::optional<base::Value> slot =
      base::JSONReader::Read(raw_slot, base::JSON_PARSE_RFC);
  atrium_core_string_free(raw_slot);
  const std::optional<int> index =
      slot && slot->is_dict() ? slot->GetDict().FindInt("index") : std::nullopt;
  if (!index || *index < 0 || static_cast<size_t>(*index) >= chips.size()) {
    return std::nullopt;
  }
  const SpaceChipSummary& chip = chips[*index];
  return DownloadItemSpace{space_id, chip.name, chip.accent};
}

}  // namespace

OverviewController::OverviewController(AtriumBrowserWindow* parent)
    : parent_(parent),
      downloads_push_throttle_(
          base::BindRepeating(&OverviewController::PostPushDownloadsList,
                              base::Unretained(this)),
          base::BindRepeating(&AtriumBrowserWindow::IsOverviewShowing,
                              base::Unretained(parent))) {
  // Not the reserved OTR context's manager - that one has no download
  // delegate wired up and crashes on GetDownloadManager().
  if (content::DownloadManager* manager =
          parent_->browser()->profile()->GetDownloadManager()) {
    download_notifier_ =
        std::make_unique<download::AllDownloadItemNotifier>(manager, this);
  }
}

OverviewController::~OverviewController() = default;

void OverviewController::PushAllLists() {
  PushNotesList();
  PushShelfList();
  PushSpacesList();
  PushArchivedList();
  PushSnoozedList();
  PushDownloadsList();
}

void OverviewController::SelectSection(const std::string& section) {
  // The section name, not just "selected": every pane render pushes all six
  // lists, so a push line cannot tell a deep link from the default section.
  VLOG(1) << "Atrium: overview section selected " << section;
  const std::u16string script =
      u"window.atriumSelectOverviewSection && "
      u"window.atriumSelectOverviewSection(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(section)) + u")";
  RunInOverviewSurfaces(script);
}

void OverviewController::PushNotesList() {
  char* raw = atrium_core_notes_overview();
  const std::string json = raw ? raw : "[]";
  if (raw) {
    atrium_core_string_free(raw);
  }
  // JSON.parse rather than interpolation - same posture as
  // PushNoteToNotesContents: note bodies are user-authored text.
  const std::u16string script =
      u"window.atriumSetNotesList && window.atriumSetNotesList(JSON.parse(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(json)) + u"))";
  RunInOverviewSurfaces(script);
}

void OverviewController::RefreshShelfList() {
  PushShelfList();
}

void OverviewController::RunInOverviewSurfaces(const std::u16string& script) {
  if (content::WebContents* sidebar = parent_->sidebar_contents()) {
    sidebar->GetPrimaryMainFrame()->ExecuteJavaScript(script,
                                                      base::NullCallback());
  }
}

void OverviewController::PushSpacesList() {
  // The sidebar's own snapshot, not a second source: the Overview is its own
  // WebContents and cannot read the sidebar's, but both must agree about which
  // spaces exist and in what order.
  const std::string model = atrium::BuildSidebarTabsJSON(parent_->browser());
  std::optional<base::Value> parsed =
      base::JSONReader::Read(model, base::JSON_PARSE_RFC);
  std::string json = "[]";
  if (parsed && parsed->is_dict()) {
    if (const base::ListValue* spaces = parsed->GetDict().FindList("spaces")) {
      base::JSONWriter::Write(*spaces, &json);
    }
  }
  // JSON.parse rather than interpolation - a space name is user-entered, the
  // same treatment PushShelfList gives page titles.
  const std::u16string script =
      u"window.atriumSetOverviewSpaces && "
      u"window.atriumSetOverviewSpaces(JSON.parse(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(json)) + u"))";
  // Byte count, not a bare "pushed": every one of the four faults behind
  // the empty Spaces section produced no push at all, and a push
  // carrying "[]" looks the same on screen as one that never ran.
  VLOG(1) << "Atrium: overview spaces push " << json.size() << " bytes";
  RunInOverviewSurfaces(script);
}

void OverviewController::PushShelfList() {
  char* raw = atrium_core_shelf_overview();
  const std::string json = raw ? raw : "[]";
  if (raw) {
    atrium_core_string_free(raw);
  }
  // JSON.parse rather than interpolation - shelf titles are page titles, the
  // same attacker-controlled text PushNotesList already treats this way.
  const std::u16string script =
      u"window.atriumSetShelf && window.atriumSetShelf(JSON.parse(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(json)) + u"))";
  RunInOverviewSurfaces(script);
}

void OverviewController::PushArchivedList() {
  // No icon-scope push: the sidebar takes its scope from the snapshot, whose
  // profile is the slot's presenter. Only the retired widget's own document
  // needed one.
  char* raw = atrium_core_archived_overview();
  const std::string json = raw ? raw : "[]";
  if (raw) {
    atrium_core_string_free(raw);
  }
  const std::u16string script =
      u"window.atriumSetArchived && window.atriumSetArchived(JSON.parse(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(json)) + u"))";
  RunInOverviewSurfaces(script);
}

void OverviewController::PushSnoozedList() {
  char* raw = atrium_core_snoozed_overview();
  const std::string json = raw ? raw : "[]";
  if (raw) {
    atrium_core_string_free(raw);
  }
  const std::u16string script =
      u"window.atriumSetSnoozed && window.atriumSetSnoozed(JSON.parse(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(json)) + u"))";
  RunInOverviewSurfaces(script);
}

void OverviewController::PushDownloadsList() {
  push_pending_ = false;
  content::DownloadManager* manager =
      download_notifier_ ? download_notifier_->GetManager() : nullptr;
  Browser* browser = parent_->browser();
  const SpaceForWebContents resolve_space =
      browser ? base::BindRepeating(&SpaceForDownloadTab,
                                    SidebarSpaceChips(browser))
              : SpaceForWebContents();
  const std::string json = BuildDownloadsOverviewJson(manager, resolve_space);
  const std::u16string script =
      u"window.atriumSetDownloads && window.atriumSetDownloads(JSON.parse(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(json)) + u"))";
  RunInOverviewSurfaces(script);
}

void OverviewController::OnDownloadCreated(content::DownloadManager* manager,
                                       download::DownloadItem* item) {
  downloads_push_throttle_.OnCreatedOrRemoved();
}

void OverviewController::OnDownloadUpdated(content::DownloadManager* manager,
                                       download::DownloadItem* item) {
  downloads_push_throttle_.OnUpdated(
      item->GetState() != download::DownloadItem::IN_PROGRESS ||
      item->IsPaused());
}

void OverviewController::OnDownloadRemoved(content::DownloadManager* manager,
                                       download::DownloadItem* item) {
  // Fires before GetAllDownloads() drops the item - post so the pushed
  // list reflects the removal instead of still including it.
  downloads_push_throttle_.OnCreatedOrRemoved();
}

void OverviewController::PostPushDownloadsList() {
  if (push_pending_) {
    return;
  }
  push_pending_ = true;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&OverviewController::PushDownloadsList,
                                weak_factory_.GetWeakPtr()));
}

void OverviewController::ReopenInOriginatingSpace(int32_t space_index,
                                              const std::string& url) {
  if (space_index >= 0) {
    if (BrowserController* controller =
            BrowserController::ControllerForBrowser(parent_->browser())) {
      controller->SelectSpace(space_index);
    }
  }
  parent_->OpenOrActivateUrl(url);
}

void OverviewController::OnSurfaceMessage(std::string_view name,
                                      const base::ListValue& args) {
  if (name == "overviewReady") {
    PushAllLists();
    return;
  }
  if (name == "notesListReady") {
    // The Notes section mounts lazily on its rail button's first click, after
    // overviewReady's push already ran and found no atriumSetNotesList to
    // call - this re-sends what that push silently dropped.
    PushNotesList();
    return;
  }
  if (name == "spacesListReady") {
    // Same lazy-mount gap as notesListReady, for the Spaces section.
    PushSpacesList();
    return;
  }
  if (name == "shelfListReady") {
    // Same lazy-mount gap as notesListReady, for the Shelf section.
    PushShelfList();
    return;
  }
  if (name == "archivedListReady") {
    // Same lazy-mount gap as notesListReady, for the Archived section.
    PushArchivedList();
    return;
  }
  if (name == "snoozedListReady") {
    // Same lazy-mount gap as notesListReady, for the Snoozed section.
    PushSnoozedList();
    return;
  }
  if ((name == "downloadsShowInFinder" || name == "downloadsRemove" ||
       name == "downloadsDeleteFromDisk") &&
      args.size() >= 1 && args[0].is_string()) {
    DispatchDownloadsOverviewMessage(
        download_notifier_ ? download_notifier_->GetManager() : nullptr, name,
        args[0].GetString());
    return;
  }
  if (name == "shelfListAdd" && args.size() >= 3 && args[0].is_string() &&
      args[1].is_string() && args[2].is_string()) {
    const std::string space_id =
        args.size() >= 4 && args[3].is_string() ? args[3].GetString() : std::string();
    char* raw = atrium_core_shelf_add(
        args[0].GetString().c_str(), args[1].GetString().c_str(),
        args[2].GetString().c_str(), space_id.empty() ? nullptr : space_id.c_str(),
        NotesInstantNow());
    if (raw) {
      atrium_core_string_free(raw);
    }
    PushShelfList();
    return;
  }
  if (name == "shelfListRemove" && args.size() >= 1 && args[0].is_string()) {
    atrium_core_shelf_remove(args[0].GetString().c_str());
    PushShelfList();
    return;
  }
  if (name == "shelfListOpen" && args.size() >= 2 && args[0].is_string() &&
      args[1].is_string()) {
    // args[0] is the shelf entry id, unused here - the URL is what a jump
    // resolves against, and the entry itself is never touched by opening it.
    parent_->OpenOrActivateUrl(args[1].GetString());
    return;
  }
  if (name == "notesListCreate") {
    char* raw = atrium_core_note_create(NotesInstantNow());
    if (raw) {
      atrium_core_string_free(raw);
    }
    PushNotesList();
    return;
  }
  if (name == "notesListDismiss" && args.size() >= 1 && args[0].is_string()) {
    atrium_core_note_dismiss(args[0].GetString().c_str());
    PushNotesList();
    return;
  }
  if (name == "notesListDelete" && args.size() >= 1 && args[0].is_string()) {
    atrium_core_note_delete(args[0].GetString().c_str(), NotesInstantNow());
    PushNotesList();
    return;
  }
  if (name == "notesListOpen" && args.size() >= 2 && args[0].is_string() &&
      args[1].is_int()) {
    const int space_index = args[1].GetInt();
    if (space_index >= 0) {
      parent_->OpenSpaceNoteBesideThisPage(space_index);
    } else {
      parent_->OpenFreestandingNoteBesideThisPage(args[0].GetString());
    }
    return;
  }
  if (name == "archivedListRestore" && args.size() >= 3 && args[0].is_string() &&
      args[1].is_string() && args[2].is_int()) {
    ReopenInOriginatingSpace(args[2].GetInt(), args[1].GetString());
    atrium_core_archived_remove(args[0].GetString().c_str());
    PushArchivedList();
    return;
  }
  if (name == "archivedListDelete" && args.size() >= 1 && args[0].is_string()) {
    atrium_core_archived_remove(args[0].GetString().c_str());
    PushArchivedList();
    return;
  }
  if (name == "snoozedListWake" && args.size() >= 5 && args[0].is_string() &&
      args[1].is_string() && args[2].is_int() && args[3].is_string() &&
      args[4].is_bool()) {
    const int space_index = args[2].GetInt();
    ReopenInOriginatingSpace(space_index, args[1].GetString());
    atrium_core_snoozed_remove(args[0].GetString().c_str());
    PushSnoozedList();
    // Mirrors the timed wake path (atrium_main_extra_parts.cc's
    // WakeDueSnoozedTabs): only alert when the row asked for one.
    if (args[4].GetBool()) {
      if (content::WebContents* active =
              parent_->browser()->tab_strip_model()->GetActiveWebContents()) {
        parent_->NotifySnoozedTabReturned(
            base::NumberToString(TabId(active)), args[3].GetString(),
            SpaceAccentColor(parent_->browser(), space_index));
      }
    }
    return;
  }
  if (name == "snoozedListDelete" && args.size() >= 1 && args[0].is_string()) {
    atrium_core_snoozed_remove(args[0].GetString().c_str());
    PushSnoozedList();
    return;
  }
}

}  // namespace atrium
