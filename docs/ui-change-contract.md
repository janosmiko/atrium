# UI change contract

Every new or changed Atrium surface goes through this list.
Read it before the first edit under `overlay/chrome/browser/resources/atrium/`
or `overlay/chrome/browser/ui/atrium/`. Nothing in the repository enforces it.

A row that does not apply is still a decision. Say in the pull request why you skipped it.

## Before you build

| Check | Where |
|---|---|
| Build to the existing tokens, do not pick new values | `docs/brand/brand-book.html`, `tokens.css` |
| Check the lab for the interaction first | `docs/lab/swipe-lab.html`, `docs/lab/settings-lab.html` |
| Copy the nearest sibling, do not invent a second shape | a hint copies `hint.js` + `hint-triggers.js`, a menu copies `quickmenu.js` |

## Wire it to the rest of the browser

| Check | Where |
|---|---|
| Hotkey, if Arc, Chrome or Safari bind one for this action | `atrium_native_hotkey_defaults.cc` |
| Collision check against the Arc defaults | `docs/hotkeys/arc-defaults.md` |
| Command bar verb | `VERBS` in `command-verbs.js`, `rust/atrium-core/src/commands.rs` |
| Right-click entry, if the surface owns rows or a canvas | `sidebar-menu.js`, `quickmenu.js`, `theme-studio.js` |
| Menu bar item, if other browsers carry one | `atrium_settings_menu_hook.mm` |
| Hint, if the control is a nav or ext button | `hint-triggers.js` |

## Make it correct

| Check | Note |
|---|---|
| Light, dark, and a non-default accent | push theme on the surface's ready message, not right after `BuildContents` |
| Esc, click-outside, focus return | register through `registerMenu()` in `menu-dismiss.js`, never a fourth close path |
| Every entry point gated, not just the one you tested | a close gate must not read state the close itself changes |
| Empty, cold row, private window | a cold row carries no tab id, so id-keyed routes are inert on it |
| `prefers-reduced-motion` | 7 of 25 stylesheets still lack it, and a hidden element with an infinite animation keeps the renderer busy |
| New user-facing strings recorded in the task | `atrium_resources.grd` holds 110 file includes and zero `<message>`, so every label is hardcoded English |

## Prove it

| Check | Note |
|---|---|
| Scenario in `build/scenarios/` | gate on a line only a CREATED surface emits, not on the document loading |
| Verified in the tart VM | never on the host, `build/scripts/vm-scenario.sh` |
| Docs touched only where the change made them wrong | see the documentation rules, no new section for an internal change |
