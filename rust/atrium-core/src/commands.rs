// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! Which keyboard command Atrium claims, and which window it acts on.
//!
//! Moved here from `chrome/browser/ui/atrium/atrium_command_intercept_decision.cc`
//! and `atrium_command_target_decision.cc`. Linux input handling
//! needs the same answers, and a second copy of a decision is how the sidebar
//! tint drifted out of existence.
//!
//! Both lists decay quietly. A command dropped from `intercept_for` does not
//! fail - it reaches a stock handler that CRASHES on an Atrium window, because
//! split view NOTREACHEDs with no BrowserView-backed TabCollection. The find
//! commands used to share that crash too, until they got their own intercept
//! values that open Atrium's own bar instead. A command dropped from
//! `command_acts_on_visible_tab` acts on an off-screen tab, which looks like
//! the feature doing nothing. Neither shows up anywhere but here.

/// Chromium's command ids, copied from `chrome/app/chrome_command_ids.h`.
///
/// These numbers are the one part of this module that is not self-contained,
/// and they are a COPY - Rust cannot include a Chromium header. What keeps the
/// copy honest is on the C++ side: `atrium_command_decision_abi_unittest.cc`
/// calls across the ABI with the real `IDC_*` macros, so a renumbering
/// upstream fails that test naming the command rather than silently rerouting
/// a key.
///
/// The ids are cross-platform - `chrome/app` is shared, not Mac-only - which is
/// what makes the decision portable to Linux as it stands.
pub mod idc {
    pub(super) const BACK: i32 = 33000;
    pub(super) const FORWARD: i32 = 33001;
    pub(super) const RELOAD: i32 = 33002;
    pub(super) const HOME: i32 = 33003;
    pub(super) const STOP: i32 = 33006;
    pub(super) const RELOAD_BYPASSING_CACHE: i32 = 33007;
    pub(super) const NEW_WINDOW: i32 = 34000;
    pub(super) const CLOSE_WINDOW: i32 = 34012;
    pub(super) const NEW_TAB: i32 = 34014;
    pub(super) const CLOSE_TAB: i32 = 34015;
    pub(super) const SELECT_NEXT_TAB: i32 = 34016;
    pub(super) const SELECT_PREVIOUS_TAB: i32 = 34017;
    pub(super) const DUPLICATE_TAB: i32 = 34027;
    pub(super) const RESTORE_TAB: i32 = 34028;
    pub(super) const EXIT: i32 = 34031;
    pub(super) const NEW_SPLIT_TAB: i32 = 34057;
    pub(super) const BOOKMARK_THIS_TAB: i32 = 35000;
    pub(super) const VIEW_SOURCE: i32 = 35002;
    pub(super) const PRINT: i32 = 35003;
    pub(super) const SAVE_PAGE: i32 = 35004;
    pub(super) const BASIC_PRINT: i32 = 35007;
    pub(super) const SHOW_TRANSLATE: i32 = 35009;
    pub(super) const MANAGE_PASSWORDS_FOR_PAGE: i32 = 35010;
    pub(super) const FIND: i32 = 37000;
    pub(super) const FIND_NEXT: i32 = 37001;
    pub(super) const FIND_PREVIOUS: i32 = 37002;
    pub(super) const ZOOM_PLUS: i32 = 38001;
    pub(super) const ZOOM_NORMAL: i32 = 38002;
    pub(super) const ZOOM_MINUS: i32 = 38003;
    pub(super) const DEV_TOOLS: i32 = 40004;
    pub(super) const DEV_TOOLS_CONSOLE: i32 = 40005;
    pub(super) const DEV_TOOLS_DEVICES: i32 = 40007;
    pub(super) const SHOW_HISTORY: i32 = 40010;
    pub(super) const SHOW_DOWNLOADS: i32 = 40012;
    #[cfg(test)]
    pub(super) const OPTIONS: i32 = 40015;
    pub(super) const DEV_TOOLS_INSPECT: i32 = 40023;
    pub(super) const DEV_TOOLS_TOGGLE: i32 = 40237;
}

/// What the intercept decision needs to know. `#[repr(C)]` because C++ hands
/// this struct across the ABI by pointer.
///
/// Half the gates used to ask the Swift dylib and every one of them went false
/// at the cut. Written as inputs, the policy survived that
/// unchanged and only the callers moved.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct CommandInterceptInputs {
    pub command: i32,

    /// The key press has not reached the first responder yet. The menu item for
    /// the same command arrives with this false, which is what lets Cmd+S
    /// toggle the sidebar while "Save Page As..." still saves the page.
    pub is_before_first_responder: bool,

    /// A global accelerator listener has suspended shortcut handling.
    pub global_accelerators_suspended: bool,

    /// Chrome's own answer for this command - the reserved commands are the
    /// ones that bypass the first responder.
    pub is_reserved_command: bool,

    /// Cmd+S is still bound to toggleSidebar. Rebinding the action away gives
    /// the key back to the native Save Page.
    pub sidebar_toggle_combo_bound: bool,

    /// Cmd+T is still bound to openCommandBar. Rebound away, Cmd+T becomes a
    /// plain blank tab - never the stock new-tab handler, which is fatal here.
    pub command_bar_combo_bound: bool,

    /// Whether anything can actually run the toggle. Claiming the key and then
    /// declining would kill Save Page, so this is a gate rather than a
    /// deletion: a little window has no sidebar and must keep Save Page.
    pub sidebar_toggle_available: bool,
}

/// What Atrium does with a command it is executing.
///
/// The discriminants are the ABI: `atrium_core.h` pins the same numbers, and
/// C++ switches on them.
#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CommandIntercept {
    /// Nothing of Atrium's. The command dispatches on the visible tab's browser.
    None = 0,
    /// Cmd+S with the sidebar toggle still bound.
    ToggleSidebar = 1,
    /// Quitting must not depend on there being a tab: the stock path returns
    /// early on a tabless window AND reports success, so the quit is swallowed.
    Exit = 2,
    /// Cmd+Shift+T on a tabless strip, which is the normal case here - an
    /// Atrium window's strip holds one live tab, so closing it empties it.
    RestoreTab = 3,
    /// Cmd+T opens the command palette and creates no tab.
    CommandPalette = 4,
    /// The same key with the palette unavailable: a blank tab, never the stock
    /// new-tab handler, which is fatal for a chromeless window.
    BlankTab = 5,
    /// Cmd+N opens a wired window slot rather than an unwired stock window.
    NewWindowSlot = 6,
    /// Swallowed, because the stock handlers crash on a chromeless window.
    Swallow = 7,
    /// Cmd+Shift+W on a tabless strip - same wall as `Exit`/`RestoreTab`.
    CloseWindow = 8,
    /// Cmd+F opens Atrium's own find bar rather than the stock one.
    FindOpen = 9,
    /// Cmd+G, or Enter with the bar already up.
    FindNext = 10,
    /// Cmd+Shift+G, or Shift+Enter with the bar already up.
    FindPrevious = 11,
}

/// Cmd+S is claimed only while the sidebar toggle is bound AND something can
/// actually run it. Without both, the key belongs to Save Page.
fn claims_save_page_for_sidebar(input: &CommandInterceptInputs) -> bool {
    input.command == idc::SAVE_PAGE
        && input.sidebar_toggle_combo_bound
        && input.sidebar_toggle_available
}

/// Whether the key press is claimed before the first responder sees it.
///
/// False means the press carries on to the renderer and the main menu. True
/// means Atrium handles it, and [`intercept_for`] decides what that is.
pub fn will_claim_command(input: &CommandInterceptInputs) -> bool {
    if !input.is_before_first_responder {
        // The menu-item path. Nothing is claimed early because nothing is early.
        return true;
    }
    if input.global_accelerators_suspended {
        return false;
    }

    // SAVE_PAGE is not in Chrome's reserved list, so without an explicit claim
    // here the key press reaches the renderer and then the main menu's own
    // handler, and the intercept below never runs.
    if claims_save_page_for_sidebar(input) {
        return true;
    }

    input.is_reserved_command
}

/// Which intercept runs. Assumes the command was claimed - callers run
/// [`will_claim_command`] first, and a command it refused never reaches here.
pub fn intercept_for(input: &CommandInterceptInputs) -> CommandIntercept {
    // Gated on the early path: the "Save Page As..." menu item arrives with
    // is_before_first_responder false and still saves the page.
    if input.is_before_first_responder && claims_save_page_for_sidebar(input) {
        return CommandIntercept::ToggleSidebar;
    }

    match input.command {
        idc::EXIT => CommandIntercept::Exit,
        idc::RESTORE_TAB => CommandIntercept::RestoreTab,
        idc::CLOSE_WINDOW => CommandIntercept::CloseWindow,
        idc::NEW_TAB => {
            if input.command_bar_combo_bound {
                CommandIntercept::CommandPalette
            } else {
                CommandIntercept::BlankTab
            }
        }
        idc::NEW_SPLIT_TAB => CommandIntercept::Swallow,
        idc::FIND => CommandIntercept::FindOpen,
        idc::FIND_NEXT => CommandIntercept::FindNext,
        idc::FIND_PREVIOUS => CommandIntercept::FindPrevious,
        // this used to need the dylib's spaces group, so since the
        // cut Cmd+N fell to the stock path and opened a window with no
        // controller. Creating a window slot needs nothing from a dylib, so the
        // gate went and Cmd+N gives a wired window again.
        idc::NEW_WINDOW => CommandIntercept::NewWindowSlot,
        _ => CommandIntercept::None,
    }
}

/// Whether a command acts on the tab the user can SEE rather than on the
/// invoking window's own tab.
///
/// In the single-window presenter model a slot shows a FOREIGN profile's
/// WebContents in place, so the invoking window's own active tab is off screen.
/// A command in this set is dispatched on the visible browser instead.
///
/// NOT in the set, deliberately: app-scoped commands like `EXIT` and `OPTIONS`,
/// and `NEW_WINDOW`, which resolves the visible PROFILE when the slot is
/// created - a different question from which Browser it runs on.
pub fn command_acts_on_visible_tab(command: i32) -> bool {
    matches!(
        command,
        // tab- and window-scoped commands. Cmd+W on the invoking
        // window would otherwise close its hidden tab rather than the one on
        // screen.
        idc::CLOSE_TAB
            | idc::CLOSE_WINDOW
            | idc::SELECT_NEXT_TAB
            | idc::SELECT_PREVIOUS_TAB
            | idc::RESTORE_TAB
            | idc::NEW_TAB
            // navigation acts on what the user sees.
            | idc::RELOAD
            | idc::RELOAD_BYPASSING_CACHE
            | idc::BACK
            | idc::FORWARD
            | idc::STOP
            | idc::HOME
            // The rest of the per-tab commands. Each reads or
            // mutates the page itself, so it belongs to the tab on screen for
            // the same reason navigation does.
            | idc::BOOKMARK_THIS_TAB
            | idc::ZOOM_PLUS
            | idc::ZOOM_MINUS
            | idc::ZOOM_NORMAL
            | idc::SHOW_HISTORY
            | idc::SHOW_DOWNLOADS
            | idc::VIEW_SOURCE
            | idc::DEV_TOOLS
            | idc::DEV_TOOLS_CONSOLE
            | idc::DEV_TOOLS_DEVICES
            | idc::DEV_TOOLS_INSPECT
            | idc::DEV_TOOLS_TOGGLE
            | idc::PRINT
            | idc::BASIC_PRINT
            | idc::DUPLICATE_TAB
            | idc::MANAGE_PASSWORDS_FOR_PAGE
            | idc::SHOW_TRANSLATE
            // Only the menu-item path reaches here - the Cmd+S key equivalent
            // is claimed earlier for the sidebar toggle.
            | idc::SAVE_PAGE
            // Enablement only, since intercept_for returns before dispatch:
            // a presenter window's own browser can have no active tab.
            | idc::FIND
            | idc::FIND_NEXT
            | idc::FIND_PREVIOUS
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Every gate on, so a test can turn ONE off and see what that alone
    /// changes.
    fn all_on(command: i32) -> CommandInterceptInputs {
        CommandInterceptInputs {
            command,
            is_before_first_responder: true,
            global_accelerators_suspended: false,
            is_reserved_command: false,
            sidebar_toggle_combo_bound: true,
            command_bar_combo_bound: true,
            sidebar_toggle_available: true,
        }
    }

    // MARK: - What is claimed before the first responder

    #[test]
    fn suspended_accelerators_claim_nothing() {
        let mut input = all_on(idc::SAVE_PAGE);
        input.global_accelerators_suspended = true;
        assert!(!will_claim_command(&input));
    }

    /// SAVE_PAGE is not reserved, so without this claim the key press reaches
    /// the renderer and then Save Page, and the toggle never runs.
    #[test]
    fn save_page_is_claimed_for_the_sidebar_toggle() {
        assert!(will_claim_command(&all_on(idc::SAVE_PAGE)));
    }

    /// Each gate is independently necessary. A decision table that keeps
    /// passing after one input stops mattering has stopped meaning anything.
    #[test]
    fn every_save_page_gate_is_necessary() {
        let mut rebound = all_on(idc::SAVE_PAGE);
        rebound.sidebar_toggle_combo_bound = false;
        assert!(
            !will_claim_command(&rebound),
            "rebound away, the key is Save Page"
        );

        let mut unavailable = all_on(idc::SAVE_PAGE);
        unavailable.sidebar_toggle_available = false;
        assert!(
            !will_claim_command(&unavailable),
            "nothing can run the toggle, so the key is Save Page"
        );
    }

    /// Atrium has no settings window of its own, so Cmd+, and the menu
    /// item belong to stock chrome://settings.
    #[test]
    fn options_is_no_longer_claimed() {
        assert!(!will_claim_command(&all_on(idc::OPTIONS)));
    }

    /// Everything else follows Chrome: only a reserved command bypasses the
    /// first responder.
    #[test]
    fn everything_else_follows_the_reserved_list() {
        let mut input = all_on(idc::CLOSE_TAB);
        input.is_reserved_command = false;
        assert!(!will_claim_command(&input));
        input.is_reserved_command = true;
        assert!(will_claim_command(&input));
    }

    /// The menu-item path is not early, so there is nothing to claim early.
    #[test]
    fn the_menu_path_is_always_let_through() {
        let mut input = all_on(idc::SAVE_PAGE);
        input.is_before_first_responder = false;
        input.global_accelerators_suspended = true;
        assert!(will_claim_command(&input));
    }

    // MARK: - What runs

    #[test]
    fn save_page_toggles_the_sidebar_on_the_key_path_only() {
        assert_eq!(
            intercept_for(&all_on(idc::SAVE_PAGE)),
            CommandIntercept::ToggleSidebar
        );

        // The "Save Page As..." menu item must still save the page.
        let mut input = all_on(idc::SAVE_PAGE);
        input.is_before_first_responder = false;
        assert_eq!(intercept_for(&input), CommandIntercept::None);
    }

    #[test]
    fn options_runs_no_intercept() {
        assert_eq!(intercept_for(&all_on(idc::OPTIONS)), CommandIntercept::None);

        let mut input = all_on(idc::OPTIONS);
        input.is_before_first_responder = false;
        assert_eq!(intercept_for(&input), CommandIntercept::None);
    }

    /// The stock path returns early on a tabless window AND reports success, so
    /// the quit is swallowed and even the Cmd+Q fallback never gets a turn.
    #[test]
    fn exit_is_always_intercepted() {
        assert_eq!(intercept_for(&all_on(idc::EXIT)), CommandIntercept::Exit);

        let mut input = all_on(idc::EXIT);
        input.sidebar_toggle_available = false;
        input.sidebar_toggle_combo_bound = false;
        input.command_bar_combo_bound = false;
        assert_eq!(
            intercept_for(&input),
            CommandIntercept::Exit,
            "quitting must not depend on any other gate"
        );
    }

    /// An Atrium window's strip holds one live tab, so closing it empties the
    /// strip - which is exactly when Cmd+Shift+T goes dead on the stock path.
    #[test]
    fn restore_tab_is_always_intercepted() {
        assert_eq!(
            intercept_for(&all_on(idc::RESTORE_TAB)),
            CommandIntercept::RestoreTab
        );
    }

    /// closing must not depend on there being a tab either, same
    /// wall as EXIT/RESTORE_TAB.
    #[test]
    fn close_window_is_always_intercepted() {
        assert_eq!(
            intercept_for(&all_on(idc::CLOSE_WINDOW)),
            CommandIntercept::CloseWindow
        );
    }

    #[test]
    fn new_tab_opens_the_palette_while_it_is_bound() {
        assert_eq!(
            intercept_for(&all_on(idc::NEW_TAB)),
            CommandIntercept::CommandPalette
        );
    }

    /// Rebound away, Cmd+T is a blank tab - never the stock new-tab handler,
    /// which is fatal for a chromeless window.
    #[test]
    fn new_tab_falls_back_to_a_blank_tab_not_the_stock_path() {
        let mut input = all_on(idc::NEW_TAB);
        input.command_bar_combo_bound = false;
        assert_eq!(intercept_for(&input), CommandIntercept::BlankTab);
    }

    /// this used to need the dylib's spaces group, so since the cut
    /// Cmd+N opened a stock window with no controller.
    #[test]
    fn new_window_always_opens_a_wired_slot() {
        assert_eq!(
            intercept_for(&all_on(idc::NEW_WINDOW)),
            CommandIntercept::NewWindowSlot
        );

        let mut input = all_on(idc::NEW_WINDOW);
        input.sidebar_toggle_available = false;
        input.sidebar_toggle_combo_bound = false;
        assert_eq!(intercept_for(&input), CommandIntercept::NewWindowSlot);
    }

    /// Split view still has no Atrium answer, so it stays swallowed. The find
    /// commands do not: they open Atrium's own bar.
    #[test]
    fn split_view_is_still_swallowed() {
        assert_eq!(
            intercept_for(&all_on(idc::NEW_SPLIT_TAB)),
            CommandIntercept::Swallow
        );
    }

    #[test]
    fn the_find_commands_open_atriums_own_bar() {
        assert_eq!(
            intercept_for(&all_on(idc::FIND)),
            CommandIntercept::FindOpen
        );
        assert_eq!(
            intercept_for(&all_on(idc::FIND_NEXT)),
            CommandIntercept::FindNext
        );
        assert_eq!(
            intercept_for(&all_on(idc::FIND_PREVIOUS)),
            CommandIntercept::FindPrevious
        );
    }

    /// Cmd+F is not reserved, so the menu path must intercept too.
    #[test]
    fn find_is_intercepted_on_the_menu_path_too() {
        let mut input = all_on(idc::FIND);
        input.is_before_first_responder = false;
        assert_eq!(intercept_for(&input), CommandIntercept::FindOpen);
    }

    /// Enablement only: a presenter window's browser can have no active tab
    /// while the tab on screen belongs to another one.
    #[test]
    fn find_enables_off_the_visible_tab() {
        for command in [idc::FIND, idc::FIND_NEXT, idc::FIND_PREVIOUS] {
            assert!(command_acts_on_visible_tab(command), "command {command}");
        }
    }

    /// An ordinary command has no intercept and dispatches normally. Without
    /// this a decision that returned something for everything would still pass.
    #[test]
    fn an_ordinary_command_is_not_intercepted() {
        for command in [idc::RELOAD, idc::BACK, idc::PRINT, idc::ZOOM_PLUS] {
            assert_eq!(
                intercept_for(&all_on(command)),
                CommandIntercept::None,
                "command {command}"
            );
        }
    }

    // MARK: - Which browser a command acts on

    /// The sets that shipped before stage 2. Kept as
    /// their own cases so a regression names the stage it broke.
    #[test]
    fn tab_and_window_scoped_commands_follow_the_visible_tab() {
        for command in [
            idc::CLOSE_TAB,
            idc::CLOSE_WINDOW,
            idc::SELECT_NEXT_TAB,
            idc::SELECT_PREVIOUS_TAB,
            idc::RESTORE_TAB,
            idc::NEW_TAB,
        ] {
            assert!(command_acts_on_visible_tab(command), "command {command}");
        }
    }

    #[test]
    fn navigation_follows_the_visible_tab() {
        for command in [
            idc::RELOAD,
            idc::RELOAD_BYPASSING_CACHE,
            idc::BACK,
            idc::FORWARD,
            idc::STOP,
            idc::HOME,
        ] {
            assert!(command_acts_on_visible_tab(command), "command {command}");
        }
    }

    /// Stage 2, enumerated by user impact. Every one of these reads or
    /// mutates the page, so it belongs to the tab on screen.
    #[test]
    fn stage_two_per_tab_commands_follow_the_visible_tab() {
        for command in [
            idc::BOOKMARK_THIS_TAB,
            idc::ZOOM_PLUS,
            idc::ZOOM_MINUS,
            idc::ZOOM_NORMAL,
            idc::SHOW_HISTORY,
            idc::SHOW_DOWNLOADS,
            idc::VIEW_SOURCE,
            idc::DEV_TOOLS,
            idc::DEV_TOOLS_CONSOLE,
            idc::DEV_TOOLS_DEVICES,
            idc::DEV_TOOLS_INSPECT,
            idc::DEV_TOOLS_TOGGLE,
            idc::PRINT,
            idc::BASIC_PRINT,
            idc::DUPLICATE_TAB,
            idc::MANAGE_PASSWORDS_FOR_PAGE,
            idc::SHOW_TRANSLATE,
            idc::SAVE_PAGE,
        ] {
            assert!(command_acts_on_visible_tab(command), "command {command}");
        }
    }

    /// App-scoped commands stay on the invoking window ON PURPOSE. NEW_WINDOW
    /// is the one that reads like a mistake and is not: it resolves the visible
    /// PROFILE when the slot is created, a different question from which
    /// Browser the command runs on. Asserted here so nobody "fixes" it by
    /// adding it to the set and quietly double-resolves it.
    #[test]
    fn app_scoped_commands_stay_on_the_invoking_window() {
        for command in [idc::EXIT, idc::NEW_WINDOW, idc::OPTIONS] {
            assert!(!command_acts_on_visible_tab(command), "command {command}");
        }
    }

    /// An unknown command must fall through rather than being swept into the
    /// set by a range check or a default that leans the wrong way.
    #[test]
    fn an_unknown_command_does_not_follow_the_visible_tab() {
        assert!(!command_acts_on_visible_tab(-1));
        assert!(!command_acts_on_visible_tab(0));
        assert!(!command_acts_on_visible_tab(999_999));
    }
}
