// Copyright 2026 The Atrium Authors

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_TABS_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_TABS_H_

#include <map>
#include <string>
#include <string_view>

#include "base/functional/function_ref.h"
#include "base/values.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_model_source.h"

class Browser;
class GURL;
class Profile;
class TabStripModel;

namespace content {
class WebContents;
}

namespace atrium {

// Whether a URL the sidebar renderer handed over may be navigated to on the
// message alone. One declaration, because three verbs answering it
// differently is how one mirror ended up the outlier.
bool IsOrdinarySidebarUrl(const GURL& url);

// The favourite a tab was opened for, kept on the tab because its URL cannot
// answer it: a favourite that redirects commits a different URL, so both the
// click and the tile's selection missed it.
void SetFavoriteKeyForTab(content::WebContents* contents,
                          const std::string& favorite_url);
std::string FavoriteKeyForTab(content::WebContents* contents);

// Drops the claim, for a favourite that no longer exists. A tab that keeps a
// claim to a removed favourite is still treated as that favourite's page, so
// its tile binding outlives the tile.
void ClearFavoriteKeyForTab(content::WebContents* contents);

// The first page a favourite's tab committed, server redirects included.
// Recorded once, and only on a tab that carries a favourite key.
void NoteFavoriteLandingForTab(content::WebContents* contents, const GURL& url);
GURL FavoriteLandingForTab(content::WebContents* contents);

// Whether `current` is still the favourite's own page: its saved URL or the
// page it landed on. The fragment does not count, the path and query do.
bool IsFavoriteAtHome(const GURL& current,
                      const GURL& saved,
                      const GURL& landing);

// The live overlays, in the form production uses: applied to ONE parsed
// model, in place. BuildSidebarTabsJSON parses once, runs them all, and
// serialises once.
//
// The `std::string` wrappers further down do the same thing through a private
// parse and write each. They exist for the tests, which drive one stage at a
// time, and are NOT the production path - chaining them cost three extra JSON
// round trips over the whole model on every push.
//
// MergeLiveSelectionInto is no longer called from BuildSidebarTabsJSON - its
// URL fallback let a favourite tile light up for a tab that never claimed
// it. Kept for the tests below.
void MergeLiveSelectionInto(base::DictValue& root,
                            const std::string& active_id,
                            const std::string& active_favorite);
void MergeLiveDiscardedInto(
    base::DictValue& root,
    const std::map<std::string, bool>& discarded_by_id);
void MergeFavoriteClaimsInto(
    base::DictValue& root,
    const std::map<std::string, std::string>& tab_id_by_favorite);

// How many times a merge has parsed and reserialised a model. Test-only, and
// the guard against a return to string-chained stages: the production path
// bumps it zero times.
int& SidebarMergeRoundTripsForTesting();

// Applies one overlay to a model held as a STRING, which costs a parse and a
// write. Test-only, so a test can measure the chained form against the
// production one.
std::string MergeThroughStringForTesting(
    std::string model,
    base::FunctionRef<void(base::DictValue&)> merge);

// Exposed for AtriumSidebarTabsTest only - production code reaches this
// through BuildSidebarTabsJSON.
std::string MergeLiveDiscarded(std::string model, TabStripModel* strip);

// the tab each favourite claimed, by favourite URL. The core pairs
// the two by URL, which a redirect has already moved. The claim outlives it.
std::map<std::string, std::string> FavoriteClaimsFor(TabStripModel* strip);

// It used to drop that tab's ordinary row as well. That shifted every gesture
// past the removed row, because the surface counts what it was sent and the
// core resolves an index against its own list.
std::string MergeFavoriteClaims(
    std::string model,
    const std::map<std::string, std::string>& tab_id_by_favorite);

// Drops the cached cross-window tab list, so the next TabsLiveInOtherWindows
// walks the windows again.
//
// Call it whenever a tab appears, leaves, changes window, or navigates.
// BrowserController does, from OnTabStripModelChanged and from the per-tab
// PrimaryPageChanged. Cheap - it does not rebuild.
void InvalidateLiveTabsCache();

// Every tab live in another window sharing `browser`'s profile, flagged
// elsewhere, with its favourite claim. LiveStrip appends these to the window's
// own strip so the core can mark a row or tile whose tab is next door.
//
// Profile-scoped because that is the boundary a page cannot cross: a
// WebContents is bound to its creating BrowserContext, so a row whose page
// lives in another profile's window can never be handed over and must not be
// drawn as though it could be.
//
// Cached across windows: a single shared change that pushes in N windows
// walks the windows once, not once per window.
std::vector<LiveTab> TabsLiveInOtherWindows(Browser* browser);

// The sidebar's tab list as the JSON shape sidebar.js renders.
//
// Built from TabStripModel, which C++ already owns - so the HTML sidebar shows
// real tabs with NO new ABI export. The favourites, spaces and folders in
// that shape stay empty here: they live in
// the Swift model and still need a producer.
std::string BuildSidebarTabsJSON(Browser* browser);

// Whether this window belongs to no space. One declaration, so the snapshot
// and the tint cannot disagree - BrowserController::PersistsTabs is the same
// rule on the write path.
bool IsIncognitoWindow(Browser* browser);

// Whether this surface message ends in a write to the document or one of its
// stores. A renderer sends whatever name it likes, so hiding a menu item
// refuses nothing - this is where the refusal lives.
bool SurfaceMessageWritesDocument(std::string_view name);

// Whether a window on `window_profile`, showing rows that belong to
// `shown_profile`, may write. A slot can present another profile's browser, so
// either end being off the record makes the write somebody's private session.
bool ProfilesMayWriteDocument(Profile* window_profile, Profile* shown_profile);

// This window's own tabs with no space behind them. Answers for a shut core,
// and for an incognito window - whose rows would otherwise be drawn as a
// space's rows, from a document it must never reach.
std::string BuildFlatSidebarJSON(TabStripModel* strip, Profile* profile);

// The accent colour a woken tab's toast needs, read off BuildSidebarTabsJSON
// rather than a second ABI export. Shared so the timed and manual wake
// paths can't resolve it differently.
std::string SpaceAccentColor(Browser* browser, int space_index);

// Loads this profile's spaces.json into the Rust core, so BuildSidebarTabsJSON
// can build a model without touching the disk.
//
// Returns immediately - the read is posted to a thread that may block, because
// doing it on the UI thread is a fatal DCHECK. Until it lands, the sidebar
// falls back exactly as it does today, so an early push is not a failure.
//
// Safe to call more than once; the core ignores a second open of the same path.
void OpenAtriumCoreForProfile(Profile* profile);

// the same open, done synchronously, for the one caller that
// cannot wait - startup has to know which profiles the launch needs before it
// creates a window. Blocks, so the caller must be somewhere that is allowed
// to. True when the document is open, including when it already was.
bool OpenAtriumCoreBlocking();

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_TABS_H_
