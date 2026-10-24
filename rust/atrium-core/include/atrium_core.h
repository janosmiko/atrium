// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The C ABI of the Atrium model core. This is the only way C++ reaches the
// Rust crate.
//
// Hand-written on purpose: cbindgen is not vendored in the Chromium tree, so
// there is no generator to run. Every entry here has to match an
// `extern "C"` function in src/lib.rs by hand.

#ifndef THIRD_PARTY_ATRIUM_CORE_INCLUDE_ATRIUM_CORE_H_
#define THIRD_PARTY_ATRIUM_CORE_INCLUDE_ATRIUM_CORE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#else
// C++ has bool built in; a C consumer needs this for the calls below.
#include <stdbool.h>
#endif

// The shape of this header. Raise it with ABI_VERSION in src/lib.rs whenever
// a signature below changes. A C++ test compares this number against what the
// linked library reports, so the two drifting apart fails the build rather
// than corrupting a call at run time.
// Unsigned to match the uint32_t the function returns; a bare 1 is an int and
// comparing the two is a -Wsign-compare error under Chromium's -Werror.
#define ATRIUM_CORE_ABI_VERSION 65u

// Returns the ABI version the linked core was built with.
uint32_t atrium_core_abi_version(void);

// What a NULL `const char*` means. A mutation below REFUSES a NULL argument
// and returns false, or NULL for the ones returning a string. It does not
// substitute the empty string, because for a note or a title that is not an
// omission, it is a deletion - and losing a note to an unset pointer is worse
// than any refusal. To clear a field, pass "" and mean it.
//
// The exception is spelled out in the comment on each export that takes it:
// atrium_core_set_space_archive_ttl, atrium_core_set_profile_search_engine,
// atrium_core_set_profile_hibernate, atrium_core_set_profile_hib_threshold,
// the optional space_id of atrium_core_shelf_add, the two hexes of
// atrium_core_page_tint, the startup_behavior of atrium_core_launch_plan, and
// the theme_json of atrium_core_set_space_theme. Those read NULL as absent,
// because absent is a real state for them and the store already collapses ""
// to it.

// Loads a persisted store and returns the loaded state as JSON.
//
// `store` is one of: "spaces", "settings", "themes", "hotkeys", "routing",
// "command-bar-selection-memory" (fixtures/stores directory names), or
// "hotkey-action-ids", a computed list - `json` is ignored for that one.
//
// `migration_instant` is what a v1 spaces.json stamps `archiveEpoch` from,
// in seconds since Swift's reference date. It is a parameter rather than a
// clock read so two loads of one file produce the same bytes.
//
// Returns NULL on any failure, INCLUDING a file at a schema this build cannot
// read. That case is not the same as empty defaults, and the caller must not
// write anything back over such a file.
//
// Free a non-NULL result with atrium_core_string_free.
char* atrium_core_query(const char* store,
                        const char* json,
                        double migration_instant);

// The space a URL should open in, as a target space id, or NULL for no match.
//
// Takes routing.json's contents rather than reading the file: the caller owns
// the path and already loads it once at wiring time.
//
// NULL means "no rule claims this URL" and also every failure - unreadable
// input, a file that does not parse, a schema this build refuses. They are the
// same answer on purpose: a caller that cannot tell them apart still does the
// right thing, which is to open the URL where it was already going.
//
// Free a non-NULL result with atrium_core_string_free.
char* atrium_core_route_for_url(const char* routing_json, const char* url,
                                const char* source_space_id);

// The icon links in a page head, as {"base":...,"icons":[{"href","rel",
// "sizes","type"}]} ranked best first, or NULL for unreadable input. `html`
// is `len` bytes and need not be NUL-terminated.
//
// Free a non-NULL result with atrium_core_string_free.
char* atrium_core_icon_links(const char* html, size_t len);

// Where the space with `space_id` RENDERS, as {"index":N,"profileId":"..."}.
// `index` is a position in sidebar order, the same one
// atrium_core_set_active_space_index takes.
//
// Both facts in one call because a routing decision needs them together: asking
// twice could straddle a mutation and pair an index with another space's
// profile.
//
// NULL when nothing is open, when the argument is unreadable, or when no space
// carries that id - a rule naming a space the user has since deleted routes
// nothing rather than routing somewhere arbitrary.
//
// Free a non-NULL result with atrium_core_string_free.
char* atrium_core_space_slot_for_id(const char* space_id);

// The inverse: the id of the space at sidebar position index, or NULL past
// the rail's end. Free with atrium_core_string_free.
char* atrium_core_space_id_at(int32_t index);

// The id of the space holding the row this live tab is showing.
//
// NULL when nothing is open, when no row is bound to `tab_id`, or when the
// binding points at a row that has since gone. A tab with no row is not an
// error - it has simply not been announced yet.
//
// Free a non-NULL result with atrium_core_string_free.
char* atrium_core_space_id_for_tab(uint64_t tab_id);

// Builds the sidebar model for one window.
//
// `spaces_json` is the persisted spaces document. `live_tabs_json` is that
// window's live strip, as [{"url": "...", "id": 123}, ...] in strip order - an
// empty array is a window with no live tabs, which is ordinary: every row is
// cold and the sidebar still renders.
//
// `active_space_index` is which space this window's slot is showing. Pass a
// NEGATIVE value when you do not know, and the document's own activeSpaceID
// decides - that is the only answer the file carries, and it is what a fresh
// browser shows. An id naming a space that is gone falls back to the first.
//
// Live tab ids go to the ACTIVE space alone, and are handed out by consuming a
// queue rather than by lookup - a first-match-wins map gives every row sharing
// a URL the same tab, and each of those rows then activates the same one.
//
// Returns NULL on any failure, including a store file this build refuses to
// read. Free a non-NULL result with atrium_core_string_free.
char* atrium_core_sidebar_model(const char* spaces_json,
                                int32_t active_space_index,
                                const char* live_tabs_json,
                                double migration_instant);

// --- The in-memory document -------------------------------------------------
//
// atrium_core_sidebar_model above takes the file's contents on every call, so
// somebody has to read the file. On the UI thread that is a fatal DCHECK, and
// PushSidebarSnapshot runs there. These three calls move the read to a thread
// that is allowed to block and leave the snapshot itself pure.

// Reads the spaces store at `spaces_path` and keeps the parsed document.
//
// BLOCKING. Call it where blocking is allowed - startup, beside profile
// bootstrap - never from the UI thread.
//
// Opening the same path twice is a no-op and does NOT re-read; a different
// path replaces the document. Returns false when the file is missing,
// unparseable, or at a schema this build refuses to read, and a failed open
// leaves any previously loaded document alone.
bool atrium_core_open(const char* spaces_path, double migration_instant);

// Forgets the loaded document.
void atrium_core_close(void);

// True once a document is loaded. Use it to decide between the Rust model and
// the flat tab list - see ChooseSidebarModelSource.
bool atrium_core_is_open(void);

// Builds the sidebar model for one window FROM MEMORY. No file IO, so this is
// safe on the UI thread, which is the whole point of the pair.
//
// `active_space_index` and `live_tabs_json` mean what they do in
// atrium_core_sidebar_model.
//
// Returns NULL when no document is open. That is not an empty model: an empty
// sidebar looks exactly like lost data, so the caller falls back to the flat
// tab list instead. Free a non-NULL result with atrium_core_string_free.
char* atrium_core_sidebar_model_cached(int32_t active_space_index,
                                       const char* live_tabs_json);

// The loaded document's own active space index, for seeding a new window's
// slot. Returns -1 when no document is open.
int32_t atrium_core_active_space_index(void);

// Whether `url` is pinned in the window's space (active_space_index, negative
// for the document's active space), or favourited by `profile_id`.
// Auto-peek's one question; it used to go to Swift through
// UIBridge::IsPeekSourceTab. Returns false when no document is open, when
// either argument is NULL, and when either is not UTF-8.
//
// Pinned is per space and favourites are per profile, which is how each is
// keyed in the file. The URL comparison is exact - a prefix rule would make
// every page on a pinned site behave as pinned.
bool atrium_core_is_peek_source_url(int32_t active_space_index,
                                    const char* profile_id,
                                    const char* url);

// Whether `profile_id` favourites exactly `url`. Pinned rows do not count.
// The startup tab is claimed for its tile at restore when this says so.
// Returns false when no document is open, when either argument is NULL, and
// when either is not UTF-8.
bool atrium_core_is_favorite_url(const char* profile_id, const char* url);

// --- Writes -----------------------------------------------------------------
//
// The mutations the HTML sidebar sends. Each one changes the IN-MEMORY document
// and returns whether anything moved; none of them touches the disk, so they
// are safe on the UI thread, and the return value is the answer to "does the
// sidebar need re-pushing".
//
// false covers every way a gesture can fail to resolve - nothing open, an index
// the model no longer has, a group id that names nothing. They are deliberately
// not distinguished: the caller does the same thing for all of them, which is
// nothing.
//
// `active_space_index` is which space this window's slot is showing, and a
// NEGATIVE value means the document's own activeSpaceID decides - the same
// convention the read calls use.
//
// A SOURCE index that does not resolve is refused; a DESTINATION is clamped
// into range. An index from a surface can be stale by the time it arrives, and
// landing at the end beats dropping the move, while moving an arbitrary row is
// worse than doing nothing.
//
// Row positions count what the SURFACE RENDERS - the ungrouped rows of the
// space - not the persisted array. The two diverge the moment a space has a tab
// group. The pinned list counts ITEMS: its entries are pins and folders in one
// array, and a folder is one position however many members it holds.

// Moves a row within the active space's ungrouped list.
bool atrium_core_reorder_tab(int32_t active_space_index,
                             int32_t from_visible,
                             int32_t to_visible);

// Moves a pinned ITEM: a pin, or a folder with every member it holds.
//
// Both positions count the entries the pinned section draws, where a folder is
// one entry drawn at its first member's place.
bool atrium_core_reorder_pinned_item(int32_t active_space_index,
                                     int32_t from_item,
                                     int32_t to_item);

// Takes the member at member_index out of group_id and lands it in to_section
// at to_slot: a pinned ITEM slot for 1, a rendered row slot for 0.
//
// member_index counts the group's own members in order, so the caller needs no
// index into either list to move one out.
bool atrium_core_ungroup_member(int32_t active_space_index,
                                const char* group_id,
                                int32_t member_index,
                                int32_t to_section,
                                int32_t to_slot);

// Moves the member at from of group_id to to among that folder's own rows, in
// whichever list holds them. Both positions count the rows as drawn, and to
// is where the row lands once it has left its old place.
bool atrium_core_reorder_group_member(int32_t active_space_index,
                                      const char* group_id,
                                      int32_t from,
                                      int32_t to);

// Pins every member of a folder whose rows are regular ones, in order, at
// pinned item slot to_item_slot.
//
// A folder that already has a pinned member is refused: moving that one is
// atrium_core_reorder_pinned_item's job.
bool atrium_core_pin_folder(int32_t active_space_index,
                            const char* group_id,
                            int32_t to_item_slot);

// Moves a folder within the active space's own list. NOT translated: a
// folder maps to the snapshot's folders one for one.
bool atrium_core_reorder_folder(int32_t active_space_index,
                                int32_t from,
                                int32_t to);

// The "#RRGGBB" the chrome paints for the active space showing a page coloured
// page_hex, at opacity glass_alpha (0.0 to 1.0). Null with no active space, and
// a page_hex naming no colour returns the space tint unchanged. The bound and
// the ink_hex contrast floor grade the COMPOSITED colour, so a thinner glass is
// answered with a stronger value. The chromePageTint setting is the CALLER's
// gate. Free a non-null result with atrium_core_string_free.
char* atrium_core_page_tint(int32_t active_space_index, const char* page_hex,
                            const char* ink_hex, double glass_alpha);

// Pins the row at from_visible at pinned item slot to_pinned, where a folder
// counts as one slot.
bool atrium_core_pin_tab(int32_t active_space_index,
                         int32_t from_visible,
                         int32_t to_pinned);

// Pins the row a live tab is showing, named by that tab's id.
//
// A favourite dragged into the pinned strip has no visible row position to
// give: its page is drawn as a tile and nothing else until the favourite is
// removed, so the caller knows the tab and not the row.
bool atrium_core_pin_tab_by_tab_id(uint64_t tab_id, int32_t to_pinned);

// Moves the regular row a live tab is showing to to_visible_slot. The slot
// past the last row appends.
bool atrium_core_move_tab_by_tab_id(uint64_t tab_id, int32_t to_visible_slot);

// Unpins the row at from_pinned into the regular list at to_visible_slot.
// from_pinned is an ITEM index and must name a pin, not a folder.
//
// The destination is a SLOT, not a row: dropping past the last tab is a real
// destination, and resolving it as a row refuses it because nothing lives
// there.
bool atrium_core_unpin_tab(int32_t active_space_index,
                           int32_t from_pinned,
                           int32_t to_visible_slot);

// Adds the row at `index` to the profile's favourites. `section` is 1 for the
// pinned ITEM list (a folder is refused) and 0 for the regular one.
//
// An index rather than a URL, because the snapshot's row id is a live tab id.
// The URL and title are read from the model, where the (profile, url) key a
// favourite needs is already known. A profile is capped at 12 favourites and
// the 13th is refused, never evicting one.
// A negative `slot` appends. Any other value is a position among the profile's
// own favourites, which is what the grid's drop resolves the pointer to.
bool atrium_core_favorite_row(int32_t active_space_index,
                              int32_t index,
                              int32_t section,
                              int32_t slot);

// Creates a Doorway for the pinned ITEM at pinned_index (a folder is
// refused). Only reads the pinned tab, never mutates it.
bool atrium_core_add_doorway(int32_t active_space_index, int32_t pinned_index);

// JSON {"id","spaceID","title","body","updatedAt","marginalia":[...]}, or
// NULL. Free a non-NULL result with atrium_core_string_free.
char* atrium_core_get_space_note(int32_t active_space_index);

// The space position to pass to atrium_core_open_tab for a tab of
// `profile_id`, or `active_space_index` when the window's own space is that
// profile's. Creates a space for a profile that has none.
int32_t atrium_core_space_for_profile(int32_t active_space_index,
                                      const char* profile_id);

// The active space's profile id, or NULL. Free a non-NULL result with
// atrium_core_string_free.
char* atrium_core_active_space_profile_id(int32_t active_space_index);

// `profile_id`'s stored search engine choice, or NULL. Free a non-NULL
// result with atrium_core_string_free.
char* atrium_core_profile_search_engine(const char* profile_id);

// Sets the active space's note body, creating the record on its first write.
// `now` is seconds since Swift's reference date - atrium_core_open's
// migration_instant convention, since Rust reads no clock.
bool atrium_core_set_space_note(int32_t active_space_index,
                                const char* body,
                                double now);

// Stores a Marginalia highlight on url and returns its id, or NULL when the
// quote is under three characters. Free a non-NULL result with
// atrium_core_string_free. `now` follows atrium_core_open's migration_instant
// convention.
char* atrium_core_add_marginal(int32_t active_space_index,
                               const char* url,
                               const char* quote,
                               double now);

// Writes the reader's own note onto a stored highlight. False when the id
// resolves to nothing or the note already read that way.
bool atrium_core_set_marginal_note(const char* id, const char* note);

// The active space's highlights on url, as a JSON array, or NULL. Free a
// non-NULL result with atrium_core_string_free.
char* atrium_core_marginalia_for_url(int32_t active_space_index,
                                     const char* url);

// The Notes overview list as a JSON array, or NULL. Free a
// non-NULL result with atrium_core_string_free.
char* atrium_core_notes_overview(void);

// A note by id, space pad or freestanding, as JSON, or NULL. Free a non-NULL
// result with atrium_core_string_free.
char* atrium_core_note_get(const char* id);

// Creates an empty freestanding note and returns it as JSON, or NULL. Free a
// non-NULL result with atrium_core_string_free. `now` follows
// atrium_core_open's migration_instant convention.
char* atrium_core_note_create(double now);

// Overwrites a note's body by id, whichever kind it is.
bool atrium_core_note_set_body(const char* id, const char* body, double now);

// Hides a note from the overview list without deleting it.
bool atrium_core_note_dismiss(const char* id);

// Blanks a space pad, or removes a freestanding note entirely.
bool atrium_core_note_delete(const char* id, double now);

// The Shelf overview list as a JSON array, or NULL. Free a
// non-NULL result with atrium_core_string_free.
char* atrium_core_shelf_overview(void);

// Adds a ShelfItem and returns it as JSON, or NULL. `space_id` may be NULL.
// `now` follows atrium_core_open's migration_instant convention. Free a
// non-NULL result with atrium_core_string_free.
char* atrium_core_shelf_add(const char* kind,
                            const char* title,
                            const char* url,
                            const char* space_id,
                            double now);

// Removes a ShelfItem by id. The tab it points at is never touched.
bool atrium_core_shelf_remove(const char* id);

// The Archived overview list as a JSON array, or NULL. Free a
// non-NULL result with atrium_core_string_free.
char* atrium_core_archived_overview(void);

// Removes an ArchivedTab by id. The caller reopens the tab itself, before or
// after - restore and delete-forever both end here.
bool atrium_core_archived_remove(const char* id);

// The Snoozed overview list as a JSON array, or NULL. Free a
// non-NULL result with atrium_core_string_free.
char* atrium_core_snoozed_overview(void);

// Removes a SnoozedTab by id. Wake and delete-forever both end here.
bool atrium_core_snoozed_remove(const char* id);

// Sweeps every row past threshold's TTL into the Archived list. threshold is
// unrecognised -> no-op. Meant for a periodic timer.
bool atrium_core_sweep_archive(const char* threshold, double now);

// Snoozes the row at index in active_space_index's section (1 pinned, 0
// regular) until wake_at.
bool atrium_core_snooze_tab(int32_t active_space_index,
                            int32_t section,
                            int32_t index,
                            double wake_at,
                            bool alert,
                            double now);

// Removes a favourite from the active space's profile. A URL rather than a
// grid position: a favourite has no id of its own, and this keys the removal
// the way atrium_core_favorite_row keys the addition.
bool atrium_core_remove_favorite(int32_t active_space_index, const char* url);

// Moves a space from one SIDEBAR position to another. Positions, not backing
// indices - the backing array is in insertion order.
bool atrium_core_reorder_space(int32_t from, int32_t to);

// Archives every ungrouped row in the active space - the Clear control. Pinned
// rows and folder members stay. Returns whether anything moved.
bool atrium_core_archive_space_tabs(int32_t active_space_index, double now);

// Archives the regular row at index in the active space - the row menu's
// Archive Tab.
bool atrium_core_archive_tab(int32_t active_space_index, int32_t index, double now);

// Archives the regular rows BELOW index in the active space. Folder members
// stay, the same rule Clear follows.
bool atrium_core_archive_tabs_below(int32_t active_space_index, int32_t index,
                                    double now);

// Deletes the pinned ITEM at index, refusing a folder. atrium_core_unpin_tab
// demotes a pin into the regular list, this takes it away.
bool atrium_core_remove_pinned_tab(int32_t active_space_index, int32_t index);

// Removes the regular row at RENDERED position index when no live tab is bound
// to it - the x on a cold row. The row goes to the graveyard like a closed tab.
// A row with a live tab is refused: close that through the strip.
bool atrium_core_remove_cold_tab(int32_t active_space_index, int32_t index);

// Reorders a favourite within the grid. Both are GRID positions, and to_visible
// is already adjusted for the tile leaving its old slot - the surface applies
// that adjustment because it is the same one its live preview used.
bool atrium_core_reorder_favorite(int32_t active_space_index,
                                  int32_t from_visible,
                                  int32_t to_visible);

// Moves a row from one space to another. from_section is 1 for pinned, 0 for
// regular; a NEGATIVE to_visible_slot appends.
//
// Both spaces are positions in sidebar order and NEITHER is the window's active
// space: paging to the destination during the drag has already changed which
// space is active by the time the drop lands, so resolving the source against
// "active" would address the wrong space's tabs.
//
// The row always lands in the target's REGULAR list, whatever section it left -
// the drag target is a space, not a section of one.
bool atrium_core_move_tab_to_space(int32_t from_space,
                                   int32_t from_index,
                                   int32_t from_section,
                                   int32_t to_space,
                                   int32_t to_visible_slot);

// A split row's tabs as one run. slots_json is a JSON array of rendered slots.
bool atrium_core_move_tab_run(int32_t from_space,
                              const char* slots_json,
                              int32_t to_space,
                              int32_t to_visible_slot);

// Moves the row a live tab is showing into the space named by space_id,
// appended to that space's regular list.
//
// By ID rather than by slot, because the routing seam knows the space a rule
// names and the tab that asked for it, and nothing about either position.
// False when the tab has no row, when no space carries that id, or when the row
// is already there - all three mean the document must not change.
bool atrium_core_move_tab_id_to_space(uint64_t tab_id, const char* space_id);

// Places a room on the Courtyard plane. space is a position in sidebar order,
// never the window's active space: the Courtyard shows every room at once, so
// "active" names the wrong one for any room the user did not open from.
//
// Returns false and stores nothing for a NaN or out-of-plane value, so the
// view keeps its last good rect instead of losing the room off the plane.
bool atrium_core_set_space_canvas_rect(int32_t space,
                                       double x,
                                       double y,
                                       double w,
                                       double h);

// Sets a card's x offset inside its lane. section is 1 for pinned and 0 for
// regular, the same code atrium_core_move_tab_to_space takes. There is no
// favourites code: a favourite is keyed by (profile, url) and is not a tab, so
// it has nowhere to keep an offset. For pinned, index is an ITEM index and a
// folder is refused, otherwise it addresses the persisted array.
bool atrium_core_set_tab_canvas_x(int32_t space,
                                  int32_t section,
                                  int32_t index,
                                  double x);

// Puts the row at index into group_id. An empty or NULL id ungroups, and an id
// that names no group in this space does nothing, so a typo cannot silently
// strip membership. section is 1 for the pinned list, where index counts ITEMS
// and must name a pin, and 0 for the rendered rows. member_slot places a
// JOINING row among the members - negative for the old last-member default.
bool atrium_core_assign_tab_to_group(int32_t active_space_index,
                                     int32_t section,
                                     int32_t index,
                                     const char* group_id,
                                     int32_t member_slot);

// Puts the tab with tab_id into group_id, pinning it first when it is not
// pinned already - a folder's members live in the pinned run. Negative
// member_slot is last. By tab id because the caller just opened this tab for a
// dragged favourite, so it has no settled row yet.
bool atrium_core_group_tab_by_tab_id(uint64_t tab_id,
                                     const char* group_id,
                                     int32_t member_slot);

// Sets whether the pinned SECTION is folded, in the window's active space.
//
// EXPLICIT state, not a toggle. The surface has already moved its own DOM
// optimistically and is reporting where it ended up; a toggle would invert
// whatever the model happened to hold, and a re-entrant double-click would land
// the two on opposite values.
bool atrium_core_set_pinned_collapsed(int32_t active_space_index,
                                      bool collapsed);

// Sets whether a tab group is folded. Explicit state, for the same reason.
bool atrium_core_set_group_collapsed(int32_t active_space_index,
                                     const char* group_id,
                                     bool collapsed);

// Creates a new, empty space at the end of sidebar order and returns its id,
// or NULL if name or profile_id is empty. Free a non-NULL result with
// atrium_core_string_free.
char* atrium_core_create_space(const char* name, const char* profile_id);

// Creates a new, empty tab group in the window's active space and returns its
// id, or NULL if there is no active space. Free a non-NULL result with
// atrium_core_string_free.
char* atrium_core_create_group(int32_t active_space_index);

// Renames a row by setting its custom_title. section is 1 for pinned, 0 for
// regular. An EMPTY name clears the rename, so the row falls back to the page's
// own title.
bool atrium_core_rename_tab(int32_t active_space_index,
                            int32_t index,
                            int32_t section,
                            const char* name);

// Renames a tab group. An unknown id, or an empty name, does nothing.
bool atrium_core_rename_group(int32_t active_space_index,
                              const char* group_id,
                              const char* name);

// Renames a space by sidebar position. An empty name does nothing.
bool atrium_core_rename_space(int32_t space_index, const char* name);

// Sets a space's icon glyph by sidebar position. An empty string clears it
// back to the name's first letter.
bool atrium_core_set_space_icon(int32_t space_index, const char* icon);

// Creates a profile and returns its id, or NULL if no document is loaded. Free
// a non-NULL result with atrium_core_string_free. The Chromium profile
// directory is created on first use by the bootstrap, not here.
char* atrium_core_add_profile(void);

// Counts what `source`'s files would import, for the Settings pane. NULL
// `files_json` means the source has no files on this Mac. Free a non-NULL
// result with atrium_core_string_free.
char* atrium_core_import_preview(const char* source, const char* files_json);

// Adds `source`'s files to the open document and themes. `routing_json` is
// current routing.json, or "" for none. `engines_json` is the settings
// `searchEngines` array, or "" to leave search engines out. With nothing
// open, an empty document opens at `spaces_path` once the files parse, and
// never over an existing file. "" means the document is already open. An
// error leaves a closed core closed. `routing` is null when no rule landed,
// and `searchEngines` is null when no engine landed. BLOCKING. Free a
// non-NULL result with atrium_core_string_free.
char* atrium_core_import_run(const char* source,
                             const char* files_json,
                             const char* routing_json,
                             const char* engines_json,
                             const char* spaces_path);

// The text inside a mozLz4 file (Zen and Firefox write these), or NULL when
// the bytes do not decode to UTF-8 text without a NUL. `data` is `len` bytes
// and need not be NUL-terminated. Free a non-NULL result with
// atrium_core_string_free.
char* atrium_core_mozlz4_decode(const char* data, size_t len);

// Renames a profile. An unknown id, or an empty name, does nothing.
bool atrium_core_rename_profile(const char* profile_id, const char* name);

// Deletes a profile record. Refused while any space uses it. The Chromium
// profile directory on disk is left alone.
bool atrium_core_delete_profile(const char* profile_id);

// Points a space at another profile, by sidebar position. An id with no
// record is refused rather than stored.
bool atrium_core_set_space_profile(int32_t space_index, const char* profile_id);

// The space's theme as ThemeSpec JSON, or NULL when it has none. Free with
// atrium_core_string_free.
char* atrium_core_space_theme(const char* space_id);

// Stores a sanitized ThemeSpec for the space. NULL theme_json removes it and
// leaves the space's tint as it is. Otherwise the tint is derived from the
// theme's dominant colour. False for an unknown space or unreadable JSON.
// atrium_core_save then writes themes.json and spaces.json.
bool atrium_core_set_space_theme(const char* space_id, const char* theme_json);

// Rasterizes a ThemeSpec (saved or not) to straight-alpha RGBA8 at
// width x height, writing the byte length to out_len. warmth_offset is what
// atrium_core_night_shift_offset answered, 0 for none. ambient_time_seconds
// < 0 keeps the authored stop positions. NULL on bad input. Free with
// atrium_core_bytes_free.
uint8_t* atrium_core_theme_raster(const char* theme_json,
                                  int32_t width,
                                  int32_t height,
                                  bool dark,
                                  double warmth_offset,
                                  double ambient_time_seconds,
                                  size_t* out_len);

// Frees a buffer atrium_core_theme_raster returned. NULL is accepted and
// ignored.
void atrium_core_bytes_free(uint8_t* bytes, size_t len);

// {"accent","dominant","text","hoverFill","activeFill"} for the appearance.
// Free with atrium_core_string_free.
char* atrium_core_theme_tokens(const char* theme_json, bool dark);

// {"accent","ink","paper","soft","accentInk"} for one #RRGGBB accent, or for
// accent_index - the live AppleAccentColor - when that names one.
// Free with atrium_core_string_free.
char* atrium_core_overlay_accent_tokens(const char* accent_hex,
                                        int32_t accent_index);

// Night shift's warmth offset for local_hour (0-24).
double atrium_core_night_shift_offset(double local_hour);

// A one-stop theme from a tint - a name from tint.rs or a #RRGGBB hex - what
// the studio opens with for an unthemed space. Free with
// atrium_core_string_free.
char* atrium_core_single_stop_theme(const char* tint);

// Sets a profile's search engine, one of the ids the settings list holds. Null
// or empty clears it.
bool atrium_core_set_profile_search_engine(const char* profile_id,
                                           const char* engine);

// Sets a profile's hibernate mode: null or empty clears it back to Follow
// General, else "never" or "always".
bool atrium_core_set_profile_hibernate(const char* profile_id,
                                       const char* mode);

// Sets a profile's own hibernation timeout rung, overriding the general
// memorySaverThreshold ladder. Null or empty clears it back to Follow General.
bool atrium_core_set_profile_hib_threshold(const char* profile_id,
                                            const char* threshold);

// Deletes a tab group. Its tabs rejoin the flat list rather than closing.
bool atrium_core_delete_group(int32_t active_space_index,
                              const char* group_id);

// Sets a group's icon glyph. An empty string clears it back to the default.
bool atrium_core_set_group_icon(int32_t active_space_index,
                                const char* group_id,
                                const char* icon);

// Sets a space's own auto-archive TTL rung by sidebar position. Null or
// empty clears it back to Follow General.
bool atrium_core_set_space_archive_ttl(int32_t space_index, const char* ttl);

// Records which space is active, by its position in sidebar order. Out of range
// is IGNORED, not clamped: a stale index describes a model that no longer
// exists, and switching to a neighbouring space is worse than doing nothing.
bool atrium_core_set_active_space_index(int32_t index);

// --- The strip --------------------------------------------------------------
//
// Opening and closing a tab. Without these the document keeps rows for tabs
// that were gone, misses the ones that were opened, and drifts further with
// every session.
//
// A live tab is joined to its persisted row by a BINDING the core keeps for as
// long as the document is open - not by URL. A URL is not unique: two blank
// tabs, or two tabs on one site, would share a row and then delete each other's.
// Nothing about the binding is written to the file.

// Persists a tab the browser just opened in the window's active space.
//
// `tab_id` is the live Chromium tab id; the core remembers which row this tab
// is showing until atrium_core_close_tab says otherwise. `to_visible_slot`
// counts the space's RENDERED rows, and a NEGATIVE value - or a slot past the
// last row - appends.
//
// A row already in this space with the same URL and no live tab on it is TAKEN
// OVER rather than duplicated: that is a cold placeholder the user clicked, and
// appending would leave them looking at their tab twice. A cold PINNED tile wins
// that lookup over a regular row.
//
// Returns whether the document changed - false for a take-over, and for a tab
// that is already bound (the defensive replay in the adopt constructor, or a tab
// landing back in a strip it never really left). The binding is recorded either
// way, so the next close still knows which row to delete.
bool atrium_core_open_tab(int32_t active_space_index,
                          uint64_t tab_id,
                          const char* url,
                          const char* title,
                          int32_t to_visible_slot);

// Binds a live tab to the cold row of `url` in the active space, adding no row.
// For a favourite's tab restored at startup: it is claimed by its tile and opens
// no row of its own, and without a binding the row it came from stays drawn
// beside the tile. False when nothing matched or the tab is bound already.
bool atrium_core_bind_tab_to_row(int32_t active_space_index,
                                 uint64_t tab_id,
                                 const char* url);

// Stamps `now` as the last visit of the row tab_id shows, the time the archive
// sweep ages it from. Call it when a tab opens and when it is activated. False
// for an unbound tab, and for a stamp less than a minute after the last one.
bool atrium_core_touch_tab(uint64_t tab_id, double now);

// Removes the row a closing tab was showing.
//
// A PINNED row is KEPT and reverts to its cold tile - the pin is the user's, not
// the tab's, and deleting the entry would throw it away. A regular row is
// deleted: a closed tab is not resurrected as a cold placeholder on the next
// space switch or restart.
//
// ONLY FOR A TAB THE USER CLOSED. Two other paths reach the same Chromium
// notification and must not come through here:
//
//   - a WINDOW TEARING DOWN removes every tab in its strip. Those are not tabs
//     the user closed, and routing them here empties the space's persisted tab
//     list on every quit. Call atrium_core_forget_tab instead.
//   - a tab MOVING to another window or a side panel is still live and still
//     owns its row. Leave its binding alone: the destination's own add
//     announces the same tab id and is a no-op because it is already bound.
//
// Returns whether the document changed.
bool atrium_core_close_tab(uint64_t tab_id);

// Forgets a live tab without touching its row.
//
// The window-teardown counterpart of atrium_core_close_tab: the tabs of a
// closing window keep their rows and render cold next launch. This is what stops
// the binding map growing for the rest of the session.
//
// Returns whether there was a binding to drop. The document never changes.
bool atrium_core_forget_tab(uint64_t tab_id);

// Carries a tab's persisted row from `old_tab_id` to `new_tab_id`.
//
// A discard or a prerender activation replaces a tab's WebContents, so the same
// tab arrives with a new id, and Chromium then sends the ordinary close and add
// pair. Without this call that pair reads as "that tab is gone" followed by
// "here is a new one": the row is deleted and re-appended, losing where it sat
// and everything the document knew about it.
//
// Call it BEFORE the close and the add. Afterwards the old id holds nothing, so
// both halves are no-ops on their own and neither needs special-casing.
//
// Returns whether a row was carried. The document does not change, so a true
// return still does not need a save.
bool atrium_core_note_tab_replaced(uint64_t old_tab_id, uint64_t new_tab_id);

// Catches a tab's persisted row up with the title its page now has.
//
// A tab is added before its page has a title, so the row atrium_core_open_tab
// wrote lands blank and stays blank: the sidebar model reads titles off the ROW,
// and the live strip it merges carries only urls and ids, so re-pushing the
// surface redraws the same empty string. Call this from TitleWasSet, which is
// where a title actually arrives.
//
// This is NOT a rename. It writes the page's title, and a row the user renamed
// keeps showing that name - atrium_core_rename_tab owns the user's own.
//
// Returns whether the document changed, so the same title arriving twice does
// not rewrite the file.
bool atrium_core_note_tab_title(uint64_t tab_id, const char* title);

// Catches a tab's persisted row up with the url its page navigated to.
//
// The sibling of atrium_core_note_tab_title, and the one that matters more. A
// row's live tab id is assigned by matching its persisted url against the live
// strip's url EXACTLY, so a tab that navigated stopped resolving to its own row -
// and a row with no live id cannot be shown as selected, cannot be closed by id,
// and its click falls through to the cold-row path, which opens the page a SECOND
// time. Call it from PrimaryPageChanged, which fires per committed navigation;
// TitleWasSet is the wrong hook, because a url and a title do not always change
// together.
//
// Leaves both titles alone, and refuses an empty url.
//
// Returns whether the document changed.
bool atrium_core_note_tab_url(uint64_t tab_id, const char* url);

// Records the sidebar's settled width.
//
// Call it once, when a width drag is RELEASED - not per pointer move, which
// would rewrite the file on every frame of the gesture. The live half of the drag
// goes to SetSidebarWidthLive and touches no document.
//
// A width below the floor is REFUSED rather than clamped up: 0 means collapsed,
// which is session state, and storing it as a width would make the next
// re-expand return to nothing.
//
// Returns whether the document changed.
bool atrium_core_set_sidebar_width(int32_t width);

// The sidebar's stored width, or 0 when there is nothing to apply.
//
// The other half of set_sidebar_width. The write existed nowhere and the READ
// existed nowhere either, so a comment claiming "a launch seeds the stored width"
// described something no code did - a width that persists and is never applied is
// the same bug from the other end.
//
// 0 rather than the default, so the CALLER decides what "nothing stored" means
// rather than the core guessing at a layout constant it does not own.
int32_t atrium_core_sidebar_width(void);

// Resolves tab_id to its Space and answers its hibernation policy.
// memory_saver_threshold is settings.json's memorySaverThreshold rung.
//
// Records a visit on tab_id's timeline. `at_ms` is milliseconds
// since the epoch, passed in because the core reads no clock.
//
// Returns whether this added a STOP. A later title for the url already at the
// end of the timeline lands but is not a new stop, so a caller can use the
// answer to decide whether to re-push the scrubber.
//
// This is NOT the back stack. It does not prune on a branch, which is the
// point: Chromium drops the forward entries when the user navigates away from
// a mid-history point, and those are the pages the user still remembers.
//
// `url` is required and a NULL refuses. `title` may be NULL, because a page
// commits before its title arrives.
bool atrium_core_record_visit(uint64_t tab_id,
                              const char* url,
                              const char* title,
                              double at_ms);

// One tab's timeline as a JSON array, oldest first:
// [{"url","title","at"}, ...]. An empty array for a tab that has not
// navigated. Free a non-NULL result with atrium_core_string_free.
char* atrium_core_visits_for_tab(uint64_t tab_id);

// Drops a tab's timeline, and returns whether there was one. Call it when the
// tab closes - this is what stops the map growing for the rest of the session.
bool atrium_core_forget_visits(uint64_t tab_id);

// Sentinels, since a plain optional cannot cross the ABI: -2.0 unknown
// tab/no document, -1.0 hibernation off, >=0.0 threshold in seconds.
double atrium_core_hibernate_policy_for_tab(uint64_t tab_id,
                                             bool memory_saver_enabled,
                                             const char* memory_saver_threshold);

// Seconds a closed favourite's page stays loaded, or -1.0 when the keep-alive
// is off. settings_json is a whole settings object (the shape
// atrium_core_query("settings", ...) returns). One this build cannot read
// answers with the defaults. NULL answers -1.0.
double atrium_core_favorite_keep_alive_seconds(const char* settings_json);

// --- The launch -------------------------------------------------------------

// What this launch has to load, as JSON:
//
//   {"requiredProfiles":["Default","Work"],
//    "startupProfiles":["Work"],
//    "startupTab":{"profileID":"Work","url":"https://..."}}
//
// `requiredProfiles` is every profile the user's spaces name, sorted, filtered
// to safe single path components, and never empty. `startupProfiles` is the
// subset that must be resident before first paint - the active space's profile
// plus the profiles of the spaces the persisted window slots restore into; EMPTY
// means no deferral, load everything. `startupTab` names the profile whose
// window is shown and the one tab it materialises; "about:blank" means there is
// nothing to restore, and the palette opens instead.
//
// The three answers are derived together on purpose: the profile the startup tab
// names is guaranteed to be one of `requiredProfiles`, or the window it belongs
// to would never be created.
//
// `startup_behavior` is settings.json's Startup row verbatim -
// "restoreLastActive" or "blankTab". It lives in a different file than the
// spaces document, so the caller reads it and passes it in rather than the core
// remembering it. "blankTab" makes `startupTab.url` "about:blank" whatever the
// document would have restored, which is the whole of what that row does; the
// profiles are unaffected, because a blank tab still opens in the active
// space's window. NULL, "", or any value this build does not know restores -
// the same answer the settings loader normalises an unreadable row to.
//
// Returns NULL when no document is open. That is not an empty plan - the caller
// falls back to a single Default profile rather than launching with no window.
// Free a non-NULL result with atrium_core_string_free.
char* atrium_core_launch_plan(const char* startup_behavior);

// Puts a whole window - or one space slice - back to a state Rewind saved.
//
// Every other sidebar verb is per-gesture: one row moves, one tab closes, one
// cold tile reopens. Order, selection and folder membership have no verb at
// all, so a restore assembled out of them would be a long, racy, half-lossy
// replay. This takes the slice WHOLE instead.
//
// `payload` is the JSON Rewind's restoreSnapshot computes:
//   {"scope":"all"|"<space index>","activeSpace":n,
//    "spaces":[{"index":n,"name":"...","pinnedCollapsed":bool,
//               "pinned":[row],"tabs":[row],
//               "folders":[{"id","name","closed","tabs":[row]}],
//               "favorites":[row]}]}
// where a row is {"id":"<live tab id, or empty for cold>","url","title",
// "selected"}. "scope":"all" carries every space; a numeric scope carries
// exactly one slice, and every other space is left untouched.
//
// VALIDATED BEFORE ANYTHING IS WRITTEN. A payload naming a space that does not
// exist, contradicting its own scope, or carrying a row with no URL is refused
// outright - the document is left exactly as it was. A space the payload does
// not name is never created and never emptied.
//
// Returns the JSON of the half only the browser can do:
//   {"reopen":[{"space","section","slot","url","title","groupID","selected"}],
//    "closeTabIds":[id],"selectTabId":id|null,"activeSpace":n}
// A reopened row has ALREADY been written cold by this call, so opening a tab
// on its URL adopts that row rather than adding a second one.
//
// SELECTION COMES BACK ONE OF TWO WAYS, never both. "selectTabId" is a tab
// that is open RIGHT NOW - the moment's selected row survived and is only
// being moved. It is null when that row has to be reopened: Chromium never
// reuses a tab id, so the moment's id names a tab that can never exist again.
// In that case exactly one reopen entry has "selected":true, and the browser
// activates the tab it opens for that entry. At most one entry is flagged.
//
// Returns NULL when the payload does not parse or is refused, which is the
// signal that nothing changed. Free a non-NULL result with
// atrium_core_string_free.
char* atrium_core_restore_slice(const char* payload);

// Writes the document to the file it was opened from.
//
// BLOCKING - it fsyncs. Post it to a thread that allows blocking, and call it
// synchronously on shutdown so the last gesture is not lost.
//
// Every write goes through the whole rule set: the one-shot <path>.pre-rust
// copy before the FIRST Rust write ever, the <path>.bak refreshed from a file
// that still reads back as JSON, credential redaction on a copy, sorted keys,
// and an atomic 0600 write.
//
// Returns 1 when a file was written, 0 when there was nothing to write (nothing
// open, or no mutation since the last save), -1 when the spaces.json write
// failed and -2 when only the themes.json write failed. Either failure is worth
// a log line, because the last gesture is only in memory.
int32_t atrium_core_save(void);

// --- Settings ---------------------------------------------------------------
//
// settings.json has no in-memory document behind it. atrium_core_query takes
// the file's contents on every read and the caller owns the path, so the write
// is the mirror of that: the whole settings object back, plus where to put it.

// Writes `settings_json` to the settings store at `settings_path`.
//
// BLOCKING - it fsyncs. Post it to a thread that allows blocking.
//
// `settings_json` is a WHOLE settings object, in the shape
// atrium_core_query("settings", ...) returns, with the edit applied. It is
// re-read through the core's own loader, so a value the surface should not have
// sent is normalised rather than written.
//
// A PARTIAL object is refused rather than filled in. Every key past the first
// four is optional, so a missing key and a default are the same bytes, and
// completing one would reset every field the caller did not send.
//
// The file already on disk is read back before anything is written. One this
// build cannot read is left exactly as it is (docs/model-rules.md R1.2), and a
// key a later build added is written back as the bytes that file carried.
//
// Returns:
//    1  the file was written
//    0  nothing was written because the arguments were not usable - a NULL or
//       empty path, or a payload this build cannot read. A caller bug, not the
//       user's file.
//   -1  the write failed. The edit is in the surface only, so this is worth a
//       log line.
//   -2  the write was DECLINED: settings.json on disk is at a schema this build
//       cannot read, or is damaged. The file is untouched and stays unwritable
//       until it is moved aside, so there is nothing to retry.
int32_t atrium_core_save_settings(const char* settings_path,
                                  const char* settings_json);

// What atrium_core_save_settings would answer for the file at
// `settings_path`, asked without writing anything: 1, 0, -1 or -2 as above.
// BLOCKING - it reads the file.
int32_t atrium_core_settings_writable(const char* settings_path);

// Same shape as atrium_core_save_settings, for routing.json: R1.2 refusal,
// whole-object-in whole-object-out, the same four return codes.
int32_t atrium_core_save_routing(const char* routing_path,
                                 const char* routing_json);

// Same shape as atrium_core_save_settings, for hotkeys.json.
int32_t atrium_core_save_hotkeys(const char* hotkeys_path,
                                 const char* hotkeys_json);

// which keyboard command Atrium claims, and which window it acts on.
// Both lists moved into the core because Linux input handling needs the same
// answers, and a second copy of a decision is how the sidebar tint drifted out
// of existence.
//
// The command ids are Chromium's own IDC_* values. The core carries a copy of
// the numbers it decides on - it cannot include a Chromium header - so the
// tests in atrium_command_decision_abi_unittest.cc call across here with the
// real macros. That is what catches a renumbering upstream.

// What the intercept decision needs to know. Field order and types match
// `commands::CommandInterceptInputs` in the crate; the ABI version covers it.
typedef struct AtriumCommandInterceptInputs {
  int32_t command;

  // The key press has not reached the first responder yet. The menu item for
  // the same command arrives with this false, which is what lets Cmd+S toggle
  // the sidebar while "Save Page As..." still saves the page.
  bool is_before_first_responder;

  // A global accelerator listener has suspended shortcut handling.
  bool global_accelerators_suspended;

  // Chrome's own answer for this command - the reserved commands are the ones
  // that bypass the first responder.
  bool is_reserved_command;

  // Cmd+S is still bound to the sidebar toggle; Cmd+T to the command palette.
  // Rebound away, each key goes back to what it would otherwise do.
  bool sidebar_toggle_combo_bound;
  bool command_bar_combo_bound;

  // Whether anything can actually run the toggle. A little window has no
  // sidebar, and claiming Cmd+S there would take Save Page away from it and
  // give nothing back.
  bool sidebar_toggle_available;
} AtriumCommandInterceptInputs;

// What Atrium does with a command it is executing. The values are the ABI -
// atrium_core_command_intercept returns one of them as an int32_t.
typedef enum AtriumCommandIntercept {
  // Nothing of Atrium's. The command dispatches on the visible tab's browser.
  ATRIUM_COMMAND_INTERCEPT_NONE = 0,
  // Cmd+S with the sidebar toggle still bound.
  ATRIUM_COMMAND_INTERCEPT_TOGGLE_SIDEBAR = 1,
  // Quitting must not depend on there being a tab: the stock path returns
  // early on a tabless window AND reports success, so the quit is swallowed.
  ATRIUM_COMMAND_INTERCEPT_EXIT = 2,
  // Cmd+Shift+T on a tabless strip, which is the normal case here.
  ATRIUM_COMMAND_INTERCEPT_RESTORE_TAB = 3,
  // Cmd+T opens the command palette and creates no tab.
  ATRIUM_COMMAND_INTERCEPT_COMMAND_PALETTE = 4,
  // The same key with the palette unavailable: a blank tab, never the stock
  // new-tab handler, which is fatal for a chromeless window.
  ATRIUM_COMMAND_INTERCEPT_BLANK_TAB = 5,
  // Cmd+N opens a wired window slot rather than an unwired stock window.
  ATRIUM_COMMAND_INTERCEPT_NEW_WINDOW_SLOT = 6,
  // Swallowed. The stock handlers crash on a chromeless window: split view
  // NOTREACHEDs without a BrowserView-backed TabCollection, and the find bar
  // dereferences a null owner.
  ATRIUM_COMMAND_INTERCEPT_SWALLOW = 7,
  // Cmd+Shift+W on a tabless strip - same wall as EXIT/RESTORE_TAB.
  ATRIUM_COMMAND_INTERCEPT_CLOSE_WINDOW = 8,
  // Cmd+F opens Atrium's own find bar rather than the stock one.
  ATRIUM_COMMAND_INTERCEPT_FIND_OPEN = 9,
  // Cmd+G, or Enter with the bar already up.
  ATRIUM_COMMAND_INTERCEPT_FIND_NEXT = 10,
  // Cmd+Shift+G, or Shift+Enter with the bar already up.
  ATRIUM_COMMAND_INTERCEPT_FIND_PREVIOUS = 11,
} AtriumCommandIntercept;

// Whether the key press is claimed before the first responder sees it.
//
// False means the press carries on to the renderer and the main menu - which
// is also the answer for a NULL argument, because letting a key through is
// recoverable and claiming one Atrium then cannot run is not.
bool atrium_core_will_claim_command(const AtriumCommandInterceptInputs* input);

// Which intercept runs, as an AtriumCommandIntercept value. Assumes the
// command was claimed: callers run atrium_core_will_claim_command first, and a
// command it refused never reaches here. NULL answers NONE.
int32_t atrium_core_command_intercept(
    const AtriumCommandInterceptInputs* input);

// Whether a command acts on the tab the user can SEE rather than on the
// invoking window's own tab.
//
// In the single-window presenter model a slot shows a FOREIGN profile's
// WebContents in place, so the invoking window's own active tab is off screen.
// A command in this set is dispatched on the visible browser instead.
bool atrium_core_command_acts_on_visible_tab(int32_t command);

// --- Request-time SSRF gate --------------------------------------------------
//
// DNS can answer differently by request time than at save time.

// Whether a resolved address - a DNS answer or an IP literal, as text - is
// loopback, link-local, or a private range. NULL or unparsable answers true.
bool atrium_core_resolved_ip_is_local(const char* ip);

// --- A link another macOS app handed over --------------------------------
//
// -[AppController application:openURLs:] asks this before
// it hands the URLs to the stock path.

// What the decision needs. Field order and types match
// `navigation::ExternalLinkInputs` in the crate; the ABI version covers it.
//
// No profile here on purpose: which profile a little window opens on is a
// separate resolution, and folding it in would hide that fork inside this
// answer.
typedef struct AtriumExternalLinkInputs {
  // The externalLinksOpenInLittle setting. A settings file without the key
  // reads true, which is what the core's own loader answers.
  bool external_links_open_in_little;

  // False when the app was handed no URL at all - an openURLs with an empty
  // list, or a plain launch.
  bool has_url;

  bool is_http_or_https;
} AtriumExternalLinkInputs;

// Where a handed-over link opens. The values are the ABI -
// atrium_core_external_link_target returns one of them as an int32_t.
typedef enum AtriumExternalLinkTarget {
  ATRIUM_EXTERNAL_LINK_TARGET_LITTLE_WINDOW = 0,
  // What the browser does today, and what the setting turns back on.
  ATRIUM_EXTERNAL_LINK_TARGET_SPACE_WINDOW = 1,
} AtriumExternalLinkTarget;

// Which window a handed-over link opens in, as an AtriumExternalLinkTarget
// value. NULL answers SPACE_WINDOW - today's behaviour, because a caller that
// filled in nothing must not be handed a new window type on that basis.
int32_t atrium_core_external_link_target(
    const AtriumExternalLinkInputs* input);

// Frees a string from atrium_core_query, atrium_core_sidebar_model,
// atrium_core_sidebar_model_cached, atrium_core_launch_plan or
// atrium_core_restore_slice. NULL is accepted
// and ignored.
void atrium_core_string_free(char* s);

// Ctrl-Tab / Ctrl-Shift-Tab recency cycling. One process-wide list -
// see rust/atrium-core/src/tab_recency.rs for the design, in particular why a
// cycle holds a snapshot rather than re-sorting on every step, and why it
// commits on the modifier key's release rather than a timeout or a second key.

// Records tab_id as newly active for recency purposes. A no-op while a cycle
// is held (atrium_core_tab_recency_step opened one and has not yet reached
// atrium_core_tab_recency_end_cycle) - the walk must not reorder itself as
// BrowserController shows each candidate in turn.
void atrium_core_tab_recency_record_activation(uint64_t tab_id);

// Drops tab_id from the recency list and from any cycle in progress, so a
// closed tab is never a target.
void atrium_core_tab_recency_record_closed(uint64_t tab_id);

// Advances the held cycle one step, opening one first if none is in progress.
// direction is +1 for Ctrl-Tab, -1 for Ctrl-Shift-Tab. Returns the tab id to
// show, or -1 when there is nowhere to go.
int64_t atrium_core_tab_recency_step(int32_t direction);

// Ends the held cycle, committing the tab it currently sits on to the front
// of the recency list. Call on the modifier key's release.
void atrium_core_tab_recency_end_cycle();

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // THIRD_PARTY_ATRIUM_CORE_INCLUDE_ATRIUM_CORE_H_
