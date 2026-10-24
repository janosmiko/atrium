// Copyright 2026 The Atrium Authors

#include "chrome/browser/ui/atrium/atrium_sidebar_tabs.h"

#include <algorithm>
#include <map>
#include <ranges>
#include <set>

#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/supports_user_data.h"
#include "base/task/thread_pool.h"
#include "base/time/time.h"
#include "chrome/browser/ui/atrium/atrium_settings_source.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_model_source.h"
#include "base/functional/function_ref.h"
#include "base/memory/raw_ptr.h"
#include "base/no_destructor.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/resource_coordinator/lifecycle_unit_state.mojom.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_external.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_favicon_source.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "url/gurl.h"
#include "url/url_constants.h"

namespace atrium {

bool IsOrdinarySidebarUrl(const GURL& url) {
  return url.is_valid() &&
         (url.SchemeIs(url::kHttpScheme) || url.SchemeIs(url::kHttpsScheme) ||
          url.SchemeIs(url::kAboutScheme));
}

namespace {

class FavoriteKeyData : public base::SupportsUserData::Data {
 public:
  FavoriteKeyData(std::string url, GURL landing)
      : url_(std::move(url)), landing_(std::move(landing)) {}
  const std::string& url() const { return url_; }
  const GURL& landing() const { return landing_; }
  void set_landing(GURL landing) { landing_ = std::move(landing); }

 private:
  const std::string url_;
  GURL landing_;
};

// Address-of-static as the key, the same idiom TabId uses.
const void* const kFavoriteKeyDataKey = &kFavoriteKeyDataKey;

// Until real favicons are served (a chrome://atrium-favicon source, the same
// shape as chrome://atrium-exticon), each row gets a stable colour derived
// from its host - the lab's favHue trick. Stable per site, so rows keep their
// identity across re-pushes instead of flickering.
std::string ColorForHost(std::string_view host) {
  uint32_t hash = 2166136261u;
  for (char c : host) {
    hash = (hash ^ static_cast<uint8_t>(c)) * 16777619u;
  }
  return "hsl(" + base::NumberToString(hash % 360) + ",65%,55%)";
}

// Every row list a merge has to reach: the top-level lists, each folder's
// tabs, and the same again inside every space.
//
// All three merges walked this shape separately and identically. One walk is
// what keeps them from drifting apart - a row list added to the model in one
// merge and forgotten in another is invisible until a row renders wrong in
// one pane and right in the next.
//
// `favorites` is deliberately absent: only the selection merge touches it, and
// it keys on a URL rather than a tab id.
void ForEachRowList(base::DictValue& root,
                    base::FunctionRef<void(base::ListValue*)> mark) {
  mark(root.FindList("tabs"));
  mark(root.FindList("pinned"));
  auto mark_folders = [&mark](base::ListValue* folders) {
    if (!folders) {
      return;
    }
    for (base::Value& folder : *folders) {
      if (base::DictValue* dict = folder.GetIfDict()) {
        mark(dict->FindList("tabs"));
      }
    }
  };
  mark_folders(root.FindList("folders"));
  // A pinned folder keeps its members in its own `tabs`, inside `pinned`.
  mark_folders(root.FindList("pinned"));
  // Every space, not only the active one: each rail pane renders its own
  // space's rows, so stopping here would leave a row correct in one pane and
  // wrong in the pane beside it.
  if (base::ListValue* spaces = root.FindList("spaces")) {
    for (base::Value& space : *spaces) {
      if (base::DictValue* dict = space.GetIfDict()) {
        mark(dict->FindList("tabs"));
        mark(dict->FindList("pinned"));
        mark_folders(dict->FindList("folders"));
        mark_folders(dict->FindList("pinned"));
      }
    }
  }
}


// The url a row is joined on. The persisted row follows the COMMITTED url
// (BrowserController::PrimaryPageChanged), so keying the strip on the visible
// one made the two halves of one join read different things - and a row stayed
// cold for as long as its page kept loading.
std::string JoinUrlFor(content::WebContents* contents) {
  const GURL committed = contents->GetLastCommittedURL();
  return committed.is_empty() ? contents->GetVisibleURL().spec()
                              : committed.spec();
}

// Parses, applies `merge`, serialises back.
//
// The ONE place a merge stage costs a JSON round trip. Production does not use
// it: BuildSidebarTabsJSON parses once and runs every `*Into` stage over
// that tree. It stays because the string-taking wrappers below are what the
// unit tests drive, and because the counter it bumps is how a regression back
// to string-chained stages is caught.
//
// Returns `model` untouched when it does not parse or does not serialise, so a
// malformed snapshot degrades to "not merged" rather than to no sidebar.
std::string RoundTripThrough(std::string model,
                             base::FunctionRef<void(base::DictValue&)> merge) {
  SidebarMergeRoundTripsForTesting()++;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(model, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return model;
  }
  merge(parsed->GetDict());
  std::string json;
  base::JSONWriter::Write(parsed->GetDict(), &json);
  return json.empty() ? model : json;
}

}  // namespace

int& SidebarMergeRoundTripsForTesting() {
  static int count = 0;
  return count;
}

std::string MergeThroughStringForTesting(
    std::string model,
    base::FunctionRef<void(base::DictValue&)> merge) {
  return RoundTripThrough(std::move(model), merge);
}

void SetFavoriteKeyForTab(content::WebContents* contents,
                          const std::string& favorite_url) {
  if (contents && !favorite_url.empty()) {
    // A tab claimed after its first commit has already landed.
    GURL landing = contents->GetController().IsInitialNavigation()
                       ? GURL()
                       : contents->GetLastCommittedURL();
    contents->SetUserData(kFavoriteKeyDataKey,
                          std::make_unique<FavoriteKeyData>(
                              favorite_url, std::move(landing)));
  }
}

void ClearFavoriteKeyForTab(content::WebContents* contents) {
  if (contents) {
    contents->RemoveUserData(kFavoriteKeyDataKey);
  }
}

std::string FavoriteKeyForTab(content::WebContents* contents) {
  if (!contents) {
    return std::string();
  }
  auto* data = static_cast<FavoriteKeyData*>(
      contents->GetUserData(kFavoriteKeyDataKey));
  return data ? data->url() : std::string();
}

void NoteFavoriteLandingForTab(content::WebContents* contents,
                               const GURL& url) {
  // PrimaryPageChanged can fire for the initial empty document
  // (crbug.com/40207280), which must not lock in as the landing.
  if (!url.is_valid() || url.IsAboutBlank()) {
    return;
  }
  auto* data = contents ? static_cast<FavoriteKeyData*>(
                              contents->GetUserData(kFavoriteKeyDataKey))
                        : nullptr;
  if (data && data->landing().is_empty()) {
    data->set_landing(url);
  }
}

GURL FavoriteLandingForTab(content::WebContents* contents) {
  auto* data = contents ? static_cast<FavoriteKeyData*>(
                              contents->GetUserData(kFavoriteKeyDataKey))
                        : nullptr;
  return data ? data->landing() : GURL();
}

bool IsFavoriteAtHome(const GURL& current,
                      const GURL& saved,
                      const GURL& landing) {
  if (!current.is_valid()) {
    return false;
  }
  const GURL page = current.GetWithoutRef();
  return page == saved.GetWithoutRef() ||
         (landing.is_valid() && page == landing.GetWithoutRef());
}

// "discarded" reads live tab state, not eligibility - a Never-space tab is
// merely never swept, so it never gets this flag set true. Takes the strip
// directly, not a Browser, so a unit test can drive it without a full window.
// Marks the row whose id is the strip's ACTIVE tab.
//
// The Swift builder is pure and cannot know which tab a given window shows -
// selection belongs to the live strip, not to the persisted model. Overlaying
// it here keeps that purity instead of threading a window's state through the
// ABI to answer one boolean.
//
// An empty `active_id` marks every row unselected, which is what a window with
// no active tab means - a closed last tab, so it must run then too.
void MergeLiveSelectionInto(base::DictValue& root,
                            const std::string& active_id,
                            const std::string& active_favorite) {
  auto mark = [](base::ListValue* rows, const std::string& wanted) {
    if (!rows) {
      return;
    }
    for (base::Value& row : *rows) {
      if (base::DictValue* dict = row.GetIfDict()) {
        if (const std::string* id = dict->FindString("id")) {
          dict->Set("selected", !wanted.empty() && *id == wanted);
        }
      }
    }
  };
  ForEachRowList(root, [&mark, &active_id](base::ListValue* rows) {
    mark(rows, active_id);
  });
  // A tile is keyed by URL, so the id comparison above never matches one and
  // nothing selected the tile at all.
  mark(root.FindList("favorites"), active_favorite);
  // Every space's grid too: a favourite is profile-wide, so the same tile is
  // drawn in every pane, and the producer gives the active space's copy the
  // live selection the top level carries.
  if (base::ListValue* spaces = root.FindList("spaces")) {
    for (base::Value& space : *spaces) {
      if (base::DictValue* dict = space.GetIfDict()) {
        mark(dict->FindList("favorites"), active_favorite);
      }
    }
  }
}

void MergeLiveDiscardedInto(base::DictValue& root,
                            const std::map<std::string, bool>& discarded_by_id) {
  ForEachRowList(root, [&discarded_by_id](base::ListValue* rows) {
    if (!rows) {
      return;
    }
    for (base::Value& row : *rows) {
      if (base::DictValue* dict = row.GetIfDict()) {
        if (const std::string* id = dict->FindString("id")) {
          auto it = discarded_by_id.find(*id);
          if (it != discarded_by_id.end()) {
            dict->Set("discarded", it->second);
          }
        }
      }
    }
  });
}

std::map<std::string, bool> DiscardedByIdFor(TabStripModel* strip) {
  std::map<std::string, bool> discarded_by_id;
  if (!strip) {
    return discarded_by_id;
  }
  for (int i = 0; i < strip->count(); ++i) {
    content::WebContents* contents = strip->GetWebContentsAt(i);
    if (!contents) {
      continue;
    }
    auto* unit =
        resource_coordinator::TabLifecycleUnitExternal::FromWebContents(
            contents);
    discarded_by_id[base::NumberToString(TabId(contents))] =
        unit && unit->GetTabState() == mojom::LifecycleUnitState::DISCARDED;
  }
  return discarded_by_id;
}

std::string MergeLiveDiscarded(std::string model, TabStripModel* strip) {
  const std::map<std::string, bool> discarded_by_id = DiscardedByIdFor(strip);
  if (discarded_by_id.empty()) {
    return model;
  }
  return RoundTripThrough(
      std::move(model), [&discarded_by_id](base::DictValue& root) {
        MergeLiveDiscardedInto(root, discarded_by_id);
      });
}

void MergeFavoriteClaimsInto(
    base::DictValue& root,
    const std::map<std::string, std::string>& tab_id_by_favorite) {
  if (tab_id_by_favorite.empty()) {
    return;
  }
  // Only tiles the model actually draws. Claiming off the map alone would let
  // a favourite this window shows no tile for drop somebody else's row.
  std::set<std::string> claimed;
  auto bind_tiles = [&tab_id_by_favorite, &claimed](base::ListValue* tiles) {
    if (!tiles) {
      return;
    }
    for (base::Value& tile : *tiles) {
      base::DictValue* dict = tile.GetIfDict();
      const std::string* id = dict ? dict->FindString("id") : nullptr;
      if (!id) {
        continue;
      }
      auto it = tab_id_by_favorite.find(*id);
      if (it == tab_id_by_favorite.end()) {
        continue;
      }
      dict->Set("tabId", it->second);
      claimed.insert(it->second);
    }
  };
  bind_tiles(root.FindList("favorites"));
  // The ACTIVE space's pane only. A live tab belongs to the space the window
  // is showing, so lending its id to the same favourite drawn in a background
  // pane would make that tile activate a tab the pane does not hold.
  if (base::ListValue* spaces = root.FindList("spaces")) {
    const std::optional<int> active = root.FindInt("activeSpace");
    if (active && *active >= 0 &&
        static_cast<size_t>(*active) < spaces->size()) {
      if (base::DictValue* pane = (*spaces)[*active].GetIfDict()) {
        bind_tiles(pane->FindList("favorites"));
      }
    }
  }
  // The row STAYS. Erasing it here was a mistake with three faces: the surface
  // counts the rows it was sent and the core resolves an index against its own
  // list, so a row removed after the model was built shifted every gesture
  // past it. Dragging a tab to the pinned strip pinned a different one, and a
  // favourite dragged out left its page with no row anywhere, because the tab
  // still carried the claim. One list has to mean one thing to both sides
  //.
  VLOG(1) << "Atrium: bound " << claimed.size()
          << " favourite tile(s) to a live tab";
}

std::map<std::string, std::string> FavoriteClaimsFor(TabStripModel* strip) {
  std::map<std::string, std::string> tab_id_by_favorite;
  if (!strip) {
    return tab_id_by_favorite;
  }
  for (int i = 0; i < strip->count(); ++i) {
    content::WebContents* contents = strip->GetWebContentsAt(i);
    if (!contents) {
      continue;
    }
    const std::string key = FavoriteKeyForTab(contents);
    if (key.empty()) {
      continue;
    }
    // insert, not operator[]: two tabs claimed for one favourite means an
    // older claim survived, and a stable rule - first in strip order - stops
    // the tile pointing at a different tab on every push.
    tab_id_by_favorite.insert({key, base::NumberToString(TabId(contents))});
  }
  return tab_id_by_favorite;
}

std::string MergeFavoriteClaims(
    std::string model,
    const std::map<std::string, std::string>& tab_id_by_favorite) {
  return RoundTripThrough(
      std::move(model), [&tab_id_by_favorite](base::DictValue& root) {
        MergeFavoriteClaimsInto(root, tab_id_by_favorite);
      });
}

namespace {

// One tab, somewhere in this process. `browser` and `profile` are what a caller
// filters on; the url and id are what it needs.
//
// The two pointers are only ever COMPARED, never dereferenced, and they cannot
// outlive their objects: a window cannot be destroyed without its tabs leaving
// the strip first, and that clears the cached list. raw_ptr is here to catch
// that argument being wrong rather than because a deref is planned.
struct LiveTabAnywhere {
  raw_ptr<Browser> browser = nullptr;
  raw_ptr<Profile> profile = nullptr;
  std::string url;
  uint64_t id = 0;
  std::string claimed;
};

// Every window's tabs, in GlobalBrowserCollection order, rebuilt only when
// something moved.
//
// The list is shared across callers ON PURPOSE. Each window's map differs only
// by which window is excluded, so building one list and filtering it per caller
// turns a single shared-state change that pushes in N windows from N walks of
// every window into one. Before this, that event cost O(windows
// squared times tabs) with nothing capping it.
//
// STALENESS: the generation moves on a tab-strip change and on a primary-page
// change, which covers a tab appearing, leaving, moving window, or navigating.
// A same-document navigation - a fragment - moves neither, so a row can stay
// marked against the previous url until the next real change. That was already
// true of the uncached form's own matching, which compares full specs and so
// never matched a changed fragment either way.
int& LiveTabsGeneration() {
  static int generation = 0;
  return generation;
}

std::vector<LiveTabAnywhere>& LiveTabsCache() {
  static base::NoDestructor<std::vector<LiveTabAnywhere>> cache;
  return *cache;
}

const std::vector<LiveTabAnywhere>& AllLiveTabs() {
  std::vector<LiveTabAnywhere>* cache = &LiveTabsCache();
  static int built_at = -1;
  if (built_at == LiveTabsGeneration()) {
    return *cache;
  }
  cache->clear();
  // GlobalBrowserCollection::ForEach, not a BrowserList walk: BrowserList is
  // gone from desktop Chromium and the collection is what atrium already
  // observes for window creation (BrowserController::CollectionObserver).
  GlobalBrowserCollection::GetInstance()->ForEach(
      [cache](BrowserWindowInterface* other) {
        if (!other) {
          return true;
        }
        Browser* other_browser = other->GetBrowserForMigrationOnly();
        // Popups, DevTools and app windows never get a BrowserController
        // (BrowserController::CollectionObserver attaches one to normal
        // windows only), so their tabs must not join the live list either.
        if (!BrowserController::ControllerForBrowser(other_browser)) {
          return true;
        }
        TabStripModel* strip = other->GetTabStripModel();
        if (!strip) {
          return true;
        }
        for (int i = 0; i < strip->count(); ++i) {
          if (content::WebContents* contents = strip->GetWebContentsAt(i)) {
            cache->push_back({other_browser, other->GetProfile(),
                              JoinUrlFor(contents), TabId(contents),
                              FavoriteKeyForTab(contents)});
          }
        }
        return true;
      });
  built_at = LiveTabsGeneration();
  return *cache;
}

}  // namespace

void InvalidateLiveTabsCache() {
  ++LiveTabsGeneration();
  // The cache never dies, so a stale entry's raw_ptr dangles at exit unless it
  // goes now.
  LiveTabsCache().clear();
}

std::vector<LiveTab> TabsLiveInOtherWindows(Browser* browser) {
  std::vector<LiveTab> tabs;
  if (!browser) {
    return tabs;
  }
  for (const LiveTabAnywhere& tab : AllLiveTabs()) {
    if (tab.browser == browser || tab.profile != browser->profile()) {
      continue;
    }
    LiveTab away{tab.url, tab.id, false};
    away.claimed = tab.claimed;
    away.elsewhere = true;
    tabs.push_back(std::move(away));
  }
  return tabs;
}

// This window's live strip, in strip order. The JSON shape and the reason for
// it live in atrium_sidebar_model_source.h, where they can be tested.
std::vector<LiveTab> LiveStrip(Browser* browser) {
  std::vector<LiveTab> live;
  TabStripModel* model = browser ? browser->tab_strip_model() : nullptr;
  if (model) {
    // which favourite each claimed tab IS, so the core binds the
    // tile itself. Through FavoriteClaimsFor, not each tab's own key, so two
    // tabs claiming one favourite resolve to that map's single winner.
    std::map<std::string, std::string> favorite_by_tab_id;
    for (const auto& [favorite, tab_id] : FavoriteClaimsFor(model)) {
      favorite_by_tab_id.insert({tab_id, favorite});
    }
    for (int i = 0; i < model->count(); ++i) {
      content::WebContents* contents = model->GetWebContentsAt(i);
      if (!contents) {
        continue;
      }
      live.push_back(
          {JoinUrlFor(contents), TabId(contents), i == model->active_index()});
      const auto claim =
          favorite_by_tab_id.find(base::NumberToString(TabId(contents)));
      if (claim != favorite_by_tab_id.end()) {
        live.back().claimed = claim->second;
      }
    }
    // A window frozen on the tab another window took still SHOWS that tab,
    // as a snapshot. Its strip's own active index moved on to whatever the
    // detach auto-selected, and drawing selection from it moved the ring to
    // a row the user never chose.
    const uint64_t frozen = BrowserController::FrozenTabIdForBrowser(browser);
    for (LiveTab& away : TabsLiveInOtherWindows(browser)) {
      if (frozen != 0 && away.id == frozen) {
        for (LiveTab& tab : live) {
          tab.active = false;
        }
        away.active = true;
      }
      live.push_back(std::move(away));
    }
  }
  return live;
}

namespace {

// Where the document lives, and the instant a v1 file stamps archiveEpoch
// from. Shared by both entry points below so they cannot resolve differently -
// the document is process-global, so a second answer would be a second file.
base::FilePath SpacesPath() {
  // through the shared gate, so a shipped build ignores it like the
  // other five reads. This one used raw getenv, which is why the first sweep
  // missed it.
  const std::string override = DevOnlyPathOverride("ATRIUM_SPACES_FILE");
  base::FilePath app_support;
  // DIR_APP_DATA is ~/Library/Application Support on macOS, which is where
  // SpacesStore.defaultFileURL puts the store.
  base::PathService::Get(base::DIR_APP_DATA, &app_support);
  return SpacesFilePath(override, app_support);
}

// The wall clock is right here: this is a real load of a real user's file,
// unlike the corpus, which pins an instant so its bytes stay comparable.
double MigrationInstantNow() {
  return base::Time::Now().InSecondsFSinceUnixEpoch() - 978307200.0;
}

}  // namespace

bool OpenAtriumCoreBlocking() {
  if (atrium_core_is_open()) {
    return true;
  }
  const base::FilePath path = SpacesPath();
  if (path.empty()) {
    return false;
  }
  // startup asks the document which profiles a launch needs, and
  // it has to know BEFORE it creates any window - so unlike the sidebar's
  // open, this one cannot be posted and awaited. It runs where blocking is
  // still allowed, which is why the caller brackets it.
  return atrium_core_open(path.value().c_str(), MigrationInstantNow());
}

void OpenAtriumCoreForProfile(Profile* profile) {
  if (!profile || atrium_core_is_open()) {
    return;
  }
  const base::FilePath path = SpacesPath();
  if (path.empty()) {
    return;
  }

  // MayBlock, because atrium_core_open reads the file - the one thing this
  // whole design exists to keep off the UI thread. BEST_EFFORT: the sidebar
  // renders its fallback until this lands, so nothing waits on it.
  base::ThreadPool::PostTask(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
       base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(
          [](base::FilePath path) {
            if (!atrium_core_open(path.value().c_str(),
                                  MigrationInstantNow())) {
              LOG(WARNING) << "atrium: the model core could not open "
                           << path.value()
                           << "; the sidebar falls back to the tab list";
            }
          },
          path));
}

namespace {

// Which profile's icon cache the surface rendering this model may read. All
// chrome surfaces share one BrowserContext, so this token is the only thing
// that tells one profile's rows from another's.
void SetIconScope(base::DictValue& model, Browser* browser) {
  model.Set("iconScope",
            IconScopeFor(browser ? browser->profile() : nullptr));
  // A rail pane draws its own space's profile, and the space names it only by
  // id. Matched against loaded profiles, never built into a path. Those are
  // regular profiles only, so no off-the-record scope reaches a pane.
  base::ListValue* spaces = model.FindList("spaces");
  ProfileManager* manager = g_browser_process->profile_manager();
  if (!spaces || spaces->empty() || !manager) {
    return;
  }
  std::map<std::string, const std::string*> scopes;
  for (Profile* profile : manager->GetLoadedProfiles()) {
    scopes[profile->GetBaseName().MaybeAsASCII()] = &IconScopeFor(profile);
  }
  for (base::Value& space : *spaces) {
    base::DictValue* dict = space.GetIfDict();
    const std::string* id = dict ? dict->FindString("profileId") : nullptr;
    if (const auto it = id ? scopes.find(*id) : scopes.end();
        it != scopes.end()) {
      dict->Set("iconScope", *it->second);
    }
  }
  // The top level describes the active space. A push built mid-switch comes
  // from the browser the slot is leaving, whose profile is not that space's.
  const auto active =
      static_cast<size_t>(model.FindInt("activeSpace").value_or(0));
  const base::DictValue* shown =
      active < spaces->size() ? (*spaces)[active].GetIfDict() : nullptr;
  if (const std::string* scope =
          shown ? shown->FindString("iconScope") : nullptr) {
    model.Set("iconScope", *scope);
  }
}

}  // namespace

bool IsIncognitoWindow(Browser* browser) {
  return browser && browser->profile()->IsOffTheRecord();
}

bool ProfilesMayWriteDocument(Profile* window_profile, Profile* shown_profile) {
  // A null end is an unresolved one, and refusing is the wrong answer there:
  // an ordinary window whose target browser has gone would stop persisting.
  return (!window_profile || !window_profile->IsOffTheRecord()) &&
         (!shown_profile || !shown_profile->IsOffTheRecord());
}

bool SurfaceMessageWritesDocument(std::string_view name) {
  // Named rather than derived: nothing in a message distinguishes a write from
  // a read, and the alternative - refusing everything not on an allow list -
  // would take a sidebar's navigation and its clicks with it.
  static constexpr std::string_view kWriters[] = {
      "archivedListDelete",       "archivedListRestore",
      "foyerMoveToSpace",         "marginaliaNoteChanged",
      "notesBodyChanged",         "notesListCreate",
      "notesListDelete",          "notesListDismiss",
      // The sidebar's theme popover shares the settings window's two messages.
      // The commit one reaches atrium_core_save, so it is a write here too.
      "settingsThemeChanged",
      "shelfListAdd",             "shelfListRemove",
      "sidebarColdTabRemoved",    "sidebarFavoriteGrouped",
      "sidebarFavoriteMovedOut",  "sidebarFavoriteRemoved",
      "sidebarFavoriteReordered", "sidebarFavoriteShelved",
      "sidebarFolderCreated",     "sidebarFolderDeleted",
      "sidebarFolderIconSet",     "sidebarFolderMemberReordered",
      "sidebarFolderRenamed",
      "sidebarFolderReordered",
      "sidebarFolderToggled",     "sidebarPinnedReordered",
      "sidebarPinnedTabRemoved",  "sidebarPinnedToggled",
      "sidebarRestoreSlice",      "sidebarRowFavorited",
      "sidebarRunMoved",
      "sidebarSpaceCreated",      "sidebarSpaceIconSet",
      "sidebarSpaceRenamed",
      "sidebarSpaceReordered",
      "sidebarSpaceSelected",     "sidebarTabAddedToDock",
      "sidebarTabArchived",       "sidebarTabGrouped",
      "sidebarTabMirrored",       "sidebarTabMovedToSpace",
      "sidebarTabPinned",         "sidebarTabRenamed",
      "sidebarTabReordered",      "sidebarTabShelved",
      "sidebarTabSnoozed",        "sidebarTabUnpinned",
      "sidebarTabsArchivedBelow", "sidebarTabsCleared",
      "sidebarTimelineCommit",    "snoozedListDelete",
      "snoozedListWake",
      // The six media* commands are deliberately absent: playback is not part
      // of the saved tab document.
  };
  return std::ranges::find(kWriters, name) != std::ranges::end(kWriters);
}

std::string BuildFlatSidebarJSON(TabStripModel* strip, Profile* profile) {
  base::ListValue tabs;
  if (strip) {
    for (int i = 0; i < strip->count(); ++i) {
      content::WebContents* contents = strip->GetWebContentsAt(i);
      if (!contents) {
        continue;
      }
      base::DictValue tab;
      tab.Set("id", base::NumberToString(TabId(contents)));
      // A page title is attacker-controlled, so it crosses as data and lands
      // in the DOM through textContent (sidebar.js), never as markup.
      tab.Set("title", base::UTF16ToUTF8(contents->GetTitle()));
      tab.Set("color", ColorForHost(contents->GetVisibleURL().host()));
      tab.Set("selected", i == strip->active_index());
      auto* unit =
          resource_coordinator::TabLifecycleUnitExternal::FromWebContents(
              contents);
      tab.Set("discarded",
               unit && unit->GetTabState() ==
                           mojom::LifecycleUnitState::DISCARDED);
      tabs.Append(std::move(tab));
    }
  }

  base::DictValue snapshot;
  snapshot.Set("tabs", std::move(tabs));
  // Empty rather than absent: without the core there IS no space model, and
  // four invented ones read as real. The surface picks the rail off
  // this list's length, so saying "none" here is what keeps the rail away.
  snapshot.Set("spaces", base::ListValue());
  snapshot.Set("iconScope", IconScopeFor(profile));
  // The surface cannot tell "no spaces yet" from "no spaces ever" - the first
  // waits for a model, the second must never draw space furniture at all.
  const bool off_the_record = profile && profile->IsOffTheRecord();
  if (off_the_record) {
    snapshot.Set("incognito", true);
  }

  std::string json;
  base::JSONWriter::Write(snapshot, &json);
  // Whole lines rather than a shared prefix and a branch: a scenario greps the
  // source for the pattern it asserts, and an interpolated one is never found.
  if (off_the_record) {
    VLOG(1) << "Atrium: sidebar model is the FLAT TAB LIST - off the record";
  } else {
    VLOG(1) << "Atrium: sidebar model is the FLAT TAB LIST - no open core";
  }
  return json;
}

std::string BuildSidebarTabsJSON(Browser* browser) {
  // the Rust core is the producer. It knows what C++ alone cannot
  // - spaces, their order and tint, pinned tabs, tab groups. Before it, this
  // file invented four spaces and a folder to fill the gap. The fall-through
  // below now emits only the flat tab list C++ genuinely knows, which shows
  // LESS rather than something made up - a deliberate correction.
  //
  // This runs on the UI thread, so it MUST NOT read a file. It does not: the
  // document was loaded once by atrium_core_open, and this call builds from
  // memory. Reading spaces.json here instead is a fatal DCHECK, and it killed
  // the app three seconds after every launch on 2026-08-09.
  //
  // Nothing happens until something opens the core, so a window whose core is
  // shut answers with the flat list.

  // So does an incognito window, whatever the core's state: the core answers
  // with the document's spaces and their rows, and those belong to the profile
  // this window exists to stay out of.
  if (atrium_core_is_open() && !IsIncognitoWindow(browser)) {
    const std::string live = LiveStripJSON(LiveStrip(browser));
    if (char* raw = atrium_core_sidebar_model_cached(
            BrowserController::SpaceIndexForBrowser(browser), live.c_str())) {
      std::string model(raw);
      atrium_core_string_free(raw);
      // Which producer answered, in the log. Three sources render into the
      // same surface, and telling them apart from the rows alone is guesswork.
      VLOG(1) << "Atrium: sidebar model from the RUST CORE, " << model.size()
              << " bytes";
      // The core marks selection off the live strip's own `active` and
      // `claimed` flags, so C++ overlays nothing for it here.
      // ONE parse and ONE write for every overlay. Chaining
      // them by string ran JSONReader and JSONWriter over the whole model
      // three extra times per push, and a push is every navigation, tab open,
      // tab close, title change and discard.
      //
      // Order does not matter to the output: each stage sets its own keys, and
      // the remaining fields are read off `id` and `url`, which the core already
      // filled from the live strip it was handed.
      std::optional<base::Value> parsed =
          base::JSONReader::Read(model, base::JSON_PARSE_RFC);
      if (!parsed || !parsed->is_dict()) {
        // Unchanged rather than empty: a malformed snapshot degrades to "not
        // merged", never to no sidebar.
        return model;
      }
      base::DictValue& root = parsed->GetDict();
      SetIconScope(root, browser);
      if (const std::map<std::string, bool> discarded = DiscardedByIdFor(
              browser ? browser->tab_strip_model() : nullptr);
          !discarded.empty()) {
        MergeLiveDiscardedInto(root, discarded);
      }
      // After the other two, because this one REMOVES rows and they address
      // rows by id.
      if (const std::map<std::string, std::string> claims = FavoriteClaimsFor(
              browser ? browser->tab_strip_model() : nullptr);
          !claims.empty()) {
        MergeFavoriteClaimsInto(root, claims);
      }
      std::string merged;
      base::JSONWriter::Write(root, &merged);
      return merged.empty() ? model : merged;
    }
  }

  return BuildFlatSidebarJSON(browser ? browser->tab_strip_model() : nullptr,
                              browser ? browser->profile() : nullptr);
}

std::string SpaceAccentColor(Browser* browser, int space_index) {
  if (space_index < 0) {
    return std::string();
  }
  std::optional<base::Value> snapshot =
      base::JSONReader::Read(BuildSidebarTabsJSON(browser), base::JSON_PARSE_RFC);
  if (!snapshot || !snapshot->is_dict()) {
    return std::string();
  }
  const base::ListValue* spaces = snapshot->GetDict().FindList("spaces");
  if (!spaces || static_cast<size_t>(space_index) >= spaces->size()) {
    return std::string();
  }
  const base::DictValue* chip = (*spaces)[space_index].GetIfDict();
  if (!chip) {
    return std::string();
  }
  const std::string* color = chip->FindString("color");
  return color ? *color : std::string();
}

}  // namespace atrium
