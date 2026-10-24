# Boosts (per-site CSS/JS) - design

**Date:** 2026-07-26
**Status:** DRAFT - scoping only. No implementation. Go/no-go and priority are the owner's call - see section 8.
**Scope:** injection mechanism, security posture for user JS, per-site persistence model, editor surface, phasing. A hotkey stays blocked: the Arc catalogue has no Boosts row and no default binding, so there is no combo to match.

## 1. What exists today

Verified in `~/chromium-atrium/src` and this worktree, not from general Chromium knowledge.

| Fact | Evidence |
|---|---|
| Atrium does **no** script or CSS injection today | zero hits for `ExecuteJavaScript*`, `InsertCSS`, `InsertStyleSheet`, `IsolatedWorld` across `chrome/browser/ui/atrium/` |
| A per-tab `content::WebContentsObserver` already exists for every tab | `BrowserController::TabEventForwarder`, `atrium_browser_controller.mm:283-407`; owned per tab in `forwarders_` (`:1460`) |
| It observes `PrimaryPageChanged`/`TitleWasSet`/`LoadProgressChanged`, but no navigation-commit hook | same class; no `ReadyToCommitNavigation`/`DidFinishNavigation` override |
| `AtriumHostOps` is at **51** fields, both copies | `atrium_bridge.h:1496-1651` (51 fn ptrs); `swift/AtriumUI/Sources/AtriumBridgeC/include/atrium_bridge.h` (51); `kHostOps` at `atrium_browser_controller.mm:466` |
| The patch stack has **never** touched a renderer or Blink directory | every `+++ b/` path across `patches/*.patch` is under `chrome/app`, `chrome/browser`, `chrome/test`, `components/*`, `content/shell`, `build/apple` |
| The only browser-process JS API usable on web pages is isolated-world | `render_frame_host.h:584` `ExecuteJavaScript` is "chrome:// or devtools:// URLs" only; `:598`/`:607` are `ForTests`; `:590` `ExecuteJavaScriptInIsolatedWorld` is production |
| There is **no** browser-process CSS-insertion API | no `InsertCSS`/`InsertStyleSheet` on `content::RenderFrameHost`; Blink's `WebDocument::InsertStyleSheet` (`web_document.h:158`) is renderer-side |
| Only 10 isolated-world IDs exist for all content embedders; Chrome already uses 4 on mac | `content/public/common/isolated_world_ids.h:14-23`; `chrome/common/chrome_isolated_world_ids.h` (TRANSLATE, INDIGO, CHROME_INTERNAL, APPLESCRIPT) |
| Extensions are enabled and exercised | `enable_extensions` defaults true on mac (`extensions/buildflags/buildflags.gni:11`); live CWS install, pill, and enable/disable were verified |
| `chrome.userScripts` is gated per-extension, settable only from the chrome://extensions detail toggle | `UserScriptManager::AreUserScriptsAllowed` (`user_script_manager.cc:106`); sole non-test caller of `SetUserScriptPrefEnabled` is `developer_private_functions.cc:657`. Dev mode additionally flips `kDynamicUserScript` (`extension_util.cc:327-347`) |
| An in-process CDP client is already a shipped Atrium pattern | `FullPageScreenshotClient` (`atrium_capture.cc:90-160`), `Page.captureScreenshot` |
| ...but any CDP attach permanently disables main-frame process reuse **browser-wide** | `RenderFrameDevToolsAgentHost::WasEverAttachedToAnyFrame()` (`render_frame_devtools_agent_host.cc:218`) is a global static latch, read by `GetProcessReusePolicyForMainFrame` (`render_frame_host_manager.cc:477-481`) |
| No PSL / eTLD+1 logic exists anywhere in Swift, twice deliberately | `CommandBarMerge.baseDomain` "Deliberately not a public-suffix lookup" (`CommandBarModel.swift:985-993`); `FaviconStore.isSameSiteURL` "no eTLD+1 / public-suffix logic" (`FaviconStore.swift:351`) |
| No `TextEditor`, no monospace code font, no syntax highlighting exists in the Swift app | zero `TextEditor` hits under `swift/AtriumUI/Sources` |
| No per-site UI surface exists | no site-info popover, no per-site permissions or zoom pane |
| No store file is per-profile; profile identity is a field inside records | `Favorite.profileID`, `Space.profileID` (`SpacesModel.swift:141`); one global dir `~/Library/Application Support/Atrium/` |
| `RoutingSettings` has no cap on rule count | no `maxRules` constant, unlike `SpacesStore.maxFavoritesPerProfile = 12` |

## 2. Decision: browser-process isolated-world injection off the existing per-tab observer

`TabEventForwarder` gains `ReadyToCommitNavigation` (arm) and `DidFinishNavigation` (inject). Injection is one `ExecuteJavaScriptInIsolatedWorld` call into a single Atrium-owned isolated world, claiming one of the 10 content-embedder IDs as `ISOLATED_WORLD_ID_ATRIUM_BOOSTS`.

CSS is applied by that shim via a constructable stylesheet (`new CSSStyleSheet()` + `replaceSync` + `document.adoptedStyleSheets`), not a `<style>` element - see 3.4.

### 2.1 Rejected alternatives

| Option | Why rejected |
|---|---|
| In-process CDP (`Page.addScriptToEvaluateOnNewDocument`) | Attaching to every tab trips `WasEverAttachedToAnyFrame()`, a process-lifetime global latch that disables main-frame process reuse for the whole browser session. Paying a permanent, browser-wide memory regression for a Low-priority cosmetic feature is not a trade worth making. `Page.setBypassCSP` existing on this path is a reason against it, not for it |
| Extensions `chrome.userScripts` / `kDynamicUserScript` | Gated on a per-extension pref reachable only from the chrome://extensions detail toggle (`developer_private_functions.cc:657`), historically dev mode. A first-class feature cannot require the user to flip a developer switch |
| Bundled internal extension + `chrome.scripting.registerContentScripts` | Correct CSS semantics (user-origin, CSP-exempt, keyed removal) with no renderer patch - the strongest runner-up. Rejected for v1 on artifact cost: a second shipped bundle, host permissions, a row in chrome://extensions and the pill that the user can disable out from under the feature. Reconsider if 2.3's CSP limits bite in practice (Q2) |
| Component extension | `PermissionsData::CanExecuteScriptEverywhere` returns true for `kComponent` (`permissions_data.cc:119-120`), so component status would let user content run on `chrome://` pages. Actively unsafe here |
| New renderer mojo calling `WebDocument::InsertStyleSheet(kUser)` | The technically correct primitive: user-origin cascade, CSP-exempt, removable by key. Rejected for v1 because it would be the **first renderer/Blink patch in the stack** (section 1), which is the most rebase-expensive class of fork patch. Revisit only if the feature earns it |
| `EmbedderUserScriptLoader` | `HostType` is a closed set - `kExtensions`, `kWebUi`, `kControlledFrameEmbedder` (`host_id.mojom:7-12`), `NOTREACHED()` otherwise. Reaching arbitrary sites means patching shared extensions mojom |

### 2.2 Run location

| Point | Verdict |
|---|---|
| document_start | Not reachable on this path. The browser cannot guarantee a message lands before the document's own scripts without a renderer-side pre-commit hook |
| document_end / `DidFinishNavigation` | **Adopted.** CSS applied here can still flash unstyled content briefly; acceptable for v1 and honest about it |
| document_idle | No |

Consequence: a Boost cannot suppress something before it paints. Say so in the UI rather than pretending otherwise. This is the main functional cost of avoiding both the extension and renderer-patch paths, and the main reason Q2 exists.

### 2.3 ABI: one new append-only op

Swift owns `boosts.json` (section 4); C++ needs the rules to inject them. One field appended at position **52**, at the end of both the struct and the positional `kHostOps` initializer, optional and nil-checked Swift-side:

```c
void (*set_boosts_json)(AtriumHost* host, const char* json);
```

`host` is ABI symmetry only - Boosts are one global user preference, so Swift broadcasts to every host on change, the same posture as `broadcastSidebarPosition` and `set_glass_backdrop_alpha`. C++ parses once into a matcher, re-injects into live tabs on change. No other op is needed.

## 3. Security posture

This section, not the mechanism, is what makes the feature expensive. In a browser, trust is the product.

### 3.1 v1 ships CSS only. User JS is excluded.

Not a phasing convenience - a considered exclusion.

- **The isolated world is not a sandbox.** It shares the page's DOM, its cookies via `document.cookie`, and its credentialed `fetch`. A Boost pasted onto a banking page or a profile page can read and exfiltrate everything the page can. There is no capability Atrium can withhold on this path.
- **It is strictly *more* privileged than the page's own scripts.** An isolated world with no registered CSP is exempt from eval checks: `evaluate_csp_for_eval` is false unless `IsolatedWorldCSP::Get().HasContentSecurityPolicy(world_id)` (`local_window_proxy.cc:262-270`), and nothing outside `extensions/` ever calls `IsolatedWorldCSP::SetContentSecurityPolicy`. Registering a CSP for the Atrium world is possible but renderer-side, which 2.1 rules out for v1.
- **The threat model is paste, not authorship.** The realistic user does not write a Boost; they paste one from a forum. Atrium would be shipping a one-field arbitrary-code-execution surface with no review, no signing, no provenance, and no revocation.
- **A better path already exists in-product.** Extensions work. A user who wants userscripts can install a userscript manager today and get a permission model, a review process, and an uninstall path. A second, weaker injection channel is strictly worse for the user and adds a support and CVE surface Atrium then owns.
- **CSS is a genuinely smaller surface.** It cannot read the DOM, cannot make requests it is not given URLs for, and cannot exfiltrate on its own. Residual risks are listed in 3.5.

If JS ever ships it needs its own design pass covering: a per-Boost consent record that invalidates on edit, a visible always-on indicator while a JS Boost is active on the page, a hard block list, and an answer to "what does the user see when a Boost is silently updated". None of that exists.

### 3.2 Where a Boost may never run - explicit allowlist, fail-closed

`ExecuteJavaScriptInIsolatedWorld` has **no** built-in restricted-URL guard; extensions get theirs from `PermissionsData::IsRestrictedUrl` (`permissions_data.cc:128`), which this path does not go through. Atrium must implement the guard itself.

Inject only when all hold, checked C++-side on the committed URL:

| Condition | Reason |
|---|---|
| Scheme is exactly `http` or `https` | Excludes `chrome://`, `devtools://`, `chrome-extension://`, `file://`, `view-source:`, `data:`, `blob:`, `about:` |
| Frame is the primary main frame | No subframes, no fenced frames, no prerender, no portals in v1 |
| The tab is not a peek, command bar, or any Atrium-owned surface | Atrium's own chrome must never be a Boost target |
| The window is not private (3.3) | |
| The URL is not the Chrome Web Store or the extension management surface | Prevents a Boost from dressing up an install or permission prompt |

Fail closed: unknown scheme, null frame, or unresolved profile means no injection.

### 3.3 Profile and private-window boundary

`docs/private-mode-design.md` 2.3 is binding: privacy identity is an explicit bit, never inferred from `profile_id_`, because an OTR profile's basename equals its original's.

- Every Boost record carries `profileID: String?`. **A new Boost defaults to the profile it was authored in.** "All profiles" is an explicit opt-in checkbox, never a default and never inferred.
- Matching compares the record's `profileID` to the controller's `profile_id_` for equality only. Nothing pattern-matches the string.
- **Boosts do not run in private windows in v1.** Hard off, gated on the `private_` bit threaded by private-mode Phase 0 (`docs/private-mode-design.md`) - not on any string. Rationale: a Boost is user-authored content bound to an identity, and injected CSS is observable by the page through `getComputedStyle`, so running the same Boost in both contexts is a cross-context linkability signal. Revisit as an explicit per-Boost opt-in later, mirroring Chromium's per-extension `incognito_enabled` posture.
- Private mode is not implemented, so there is nothing to read today. Boosts must not invent a parallel privacy bit. They consume the one that private-mode Phase 0 threads through `RegisterHostInSlot`.

### 3.4 CSP interaction

| Path | CSP behaviour | Evidence |
|---|---|---|
| `<style>` element created from the isolated world | **Blocked** by the page's `style-src`. `StyleElement` checks `GetContentSecurityPolicyForCurrentWorld()`, which falls back to the main-world CSP when the isolated world has none | `style_element.cc:197-208`; `local_dom_window.cc:413-433` |
| Constructable `CSSStyleSheet` + `adoptedStyleSheets` | No CSP check on this path | no `ContentSecurityPolicy`/`AllowInline` reference in `css_style_sheet.cc` |
| Extension CSS via `WebDocument::InsertStyleSheet` | Not CSP-checked; `StyleEngine::InjectSheet` has no CSP call and keeps user-origin sheets in a separate list | `script_injection.cc:409`; `style_engine.cc:328-342` |

v1 uses the constructable-stylesheet route. It is author-origin, so a page's own `!important` rules can outrank a Boost - a functional limitation, not a security one. **This asymmetry is the single most load-bearing unverified claim in this doc; confirm it on a strict-`style-src` site before Phase 1 merges (R1).**

Atrium must not add a CSP bypass. `Page.setBypassCSP` exists on the CDP path and is a reason that path was rejected.

### 3.5 Residual risks of CSS-only, and the warning text

CSS alone still allows: hiding or repositioning security-relevant UI (a bank's "you are signing for X"), overlaying a fake control, and coarse exfiltration through attribute-selector-triggered `background-image` requests. None of these are fixable by restricting CSS syntax without a parser Atrium would then own.

Mitigations for v1: an always-visible indicator in the sidebar when a Boost is active on the current tab, and a one-line off switch. The indicator is not optional - a page a user cannot tell is modified is the actual hazard.

The confirmation on first save of a Boost the user did not type themselves is out of scope for v1 because provenance cannot be detected. Instead, the pane carries a permanent, non-dismissible line:

> Boosts change how a site looks in this browser only. A Boost can hide or move anything on the page, including warnings and confirmation prompts. Only add CSS you understand.

No marketing framing, no "safe" claim.

## 4. Persistence

One global `boosts.json` in `~/Library/Application Support/Atrium/`, written through `SecureFileWriter` (atomic rename, 0600 from creation), `ATRIUM_BOOSTS_FILE` env override, `schemaVersion = 1`, per-entry salvage decode. Debounced save on a 1.0s timer with a synchronous `saveNow()` at `AtriumWillTerminate`, following `SpacesStore.scheduleSave` (`SpacesStore.swift:1079-1110`) rather than `HotkeyStore`'s synchronous write - a live CSS editor writes far more often than a hotkey rebind.

**Matching key: host, with an explicit `includeSubdomains` flag.**

| Field | Shape |
|---|---|
| `id` | `UUID` |
| `enabled` | `Bool` |
| `host` | lowercased host, leading `www.` stripped, matched against `URLComponents(url:resolvingAgainstBaseURL:).host` the same way `RoutingSettings.route(for:)` does |
| `includeSubdomains` | `Bool`, default false. True also matches any host ending in `.<host>` |
| `profileID` | `String?`; nil means all profiles, and is never the default (3.3) |
| `css` | `String` |
| `name` | `String` |

Deliberately **not** supported: regex, path or query matching, and any public-suffix awareness. Regex exists in `RoutingSettings` because a mis-match there merely opens the wrong space; here a mis-match runs user content on the wrong site. No PSL, consistent with the two documented refusals in section 1 - the accepted consequence is that `includeSubdomains` on a bare public suffix over-matches, which requires the user to type one deliberately.

Precedence: first enabled matching rule wins, array order = priority, matching `RoutingSettings`' documented rule. Multiple Boosts on one host are a Phase 2 question (Q3).

**Bounds** - `RoutingSettings` has none, which is a gap this must not copy:

| Limit | Value | Enforced |
|---|---|---|
| Boosts per file | 200 | decode drops the tail, save rejects |
| CSS bytes per Boost | 128 KB | editor caps, decode drops the entry |
| Distinct hosts | implied by the 200 cap | - |

## 5. Editor surface

**v1: a new Settings pane.** `SettingsModel.Pane` gains `boosts` (`SettingsModel.swift:15-19`); the sidebar picks it up from `allCases` automatically (`SettingsView.swift:40`). Two switches are exhaustive and the compiler forces them (`SettingsSidebarRow.symbolName`/`.title`, `SettingsView.swift:95-115`). `SettingsDetailPane`'s `if/else if` chain (`:132-145`) is **not** checked, and its `else` arm falls through to `SettingsAppearancePane` - a forgotten branch silently renders the *Appearance* pane under the Boosts label. Add the branch and a test that asserts the pane count matches `Pane.allCases`.

Shape follows `SettingsRoutingModel` (139 lines) + `SettingsRoutingPane` (280 lines), the closest existing precedent: a rule list with add/remove/enable, plus a `TextEditor` for the CSS body. This is the first `TextEditor` and the first monospace code surface in the app; there is no in-repo convention to copy and none should be invented. No syntax highlighting, no linting, no autocomplete in v1.

Rejected for v1: a per-site panel anchored to the current tab (no anchored-popover precedent exists anywhere in the Swift layer), an external watched file, and any import/share format. A command-bar action "Edit Boost for this site" that opens Settings pre-filled with the current host is cheap and is the honest bridge to a per-site feel - Phase 2.

## 6. Phasing and non-goals

| Phase | Content | Ships |
|---|---|---|
| 0 | `BoostsStore` + `boosts.json` + matcher + bounds + tests. No injection, no UI | Nothing user-visible; makes the matcher testable before any code runs on a page |
| 1 | `ISOLATED_WORLD_ID_ATRIUM_BOOSTS`, `TabEventForwarder` injection at `DidFinishNavigation`, the 3.2 allowlist, `set_boosts_json` op at ABI 52, the Settings pane, the sidebar active-Boost indicator | First shippable slice - CSS only |
| 2 | Command-bar "Edit Boost for this site"; live re-apply without reload; multiple Boosts per host if Q3 says yes | Convenience |
| 3 | Private-window opt-in once private-mode Phase 0 lands; reconsider the extension or renderer-patch path if CSP or document_start limits bite | Conditional |
| - | User JS | Not scheduled. Needs its own design pass (3.1) |

**Non-goals.** User JS in any form. `document_start` timing. Subframe injection. Regex or path matching. Public-suffix awareness. Sharing, importing, or a Boost gallery. Syntax highlighting or a CSS linter. Any CSP bypass. Running Boosts on `chrome://`, extension pages, or Atrium's own surfaces. A hotkey (there is no Arc default to match).

## 7. Risks and unknowns

| # | Risk | Posture |
|---|---|---|
| R1 | Unverified: that a constructable `CSSStyleSheet` adopted from an isolated world is not subject to the page's `style-src`. Read from Blink source, not observed | Verify on a strict-`style-src` site before Phase 1 merges. If it is blocked, v1's CSS story collapses and Q2 becomes the decision, not a fallback |
| R2 | Unverified: injection ordering at `DidFinishNavigation` relative to the page's own scripts, and how bad the unstyled flash is | Measure in Phase 1. A visible flash on every navigation may itself be disqualifying for a cosmetic feature |
| R3 | Claiming an ID from a 10-slot shared pool with no allocator. Chrome already uses 4 on mac | Low: one slot, declared next to Chrome's in `chrome_isolated_world_ids.h`. Note it in the ABI docs so a future feature does not silently reuse it |
| R4 | Back/forward cache interaction is unmeasured. Extension CSS injection passes `BackForwardCacheAware::kPossiblyDisallow` (`script_injection.cc:411`), suggesting stylesheet injection can evict | Measure in Phase 1; a per-navigation BFCache eviction would be a real regression |
| R5 | The feature normalizes "paste code from the internet into your browser". Even CSS-only, it builds the habit and the UI affordance that a later JS phase would inherit | Accepted only if the 3.5 indicator ships with Phase 1, non-optional |
| R6 | Private mode is unimplemented, so the private-window gate in 3.3 cannot be tested end to end | Phase 1 gates on a constant `false` until the bit exists; Phase 3 wires it. The gate must never be omitted "because nothing is private yet" |

## 8. Questions for the repo owner

| # | Question | Options | Recommendation |
|---|---|---|---|
| Q1 | **Build this at all?** It is Low priority, cosmetic, and the closest thing to a trust-negative feature on the list. Shipping the free browser comes first | (a) Defer until after public launch. (b) Build CSS-only now. (c) Drop and close as WONTFIX, pointing users at extensions | **(a).** The design is cheap to hold; the feature is not. Nothing about Arc parity is urgent for a browser with no users yet, and section 3 is a real ongoing cost. If it is built, (b) - never JS |
| Q2 | If R1 or R2 make browser-process injection unworkable, escalate to the bundled internal extension or stop? | (a) Escalate to the extension path. (b) Stop and close | **(a)**, but only with a fresh cost estimate: it is a second shipped artifact, a chrome://extensions row, and host permissions - materially more than 2.1's "runner-up" framing suggests |
| Q3 | One Boost per host, or many with ordering? | (a) One, first-match. (b) Many, concatenated in array order | **(a)** for v1. (b) is real once users have more than a handful, but it adds a reordering UI to a pane that has no other list affordances |
| Q4 | Does a Boost default to its authoring profile or to all profiles? | (a) Authoring profile, "all" is opt-in. (b) All profiles, scope is opt-in | **(a).** 3.3. (b) reads as friendlier and is exactly the silent cross-profile behaviour section 2.3 of the private-mode design was written to prevent |
| Q5 | Is the always-on active-Boost indicator negotiable? | (a) No - ships with Phase 1. (b) Later, as polish | **(a).** A modified page the user cannot identify is the actual hazard in 3.5. Without it, prefer not shipping |
| Q6 | Is `document_end` timing acceptable, given the flash? | (a) Yes, document it. (b) No - block on a path that reaches document_start | **(a)** for v1, revisited after R2 is measured. (b) means the renderer patch, which is disproportionate here |

## 9. Not determined

- Whether the constructable-stylesheet CSP result (R1) holds in practice. Read from source only; no build was run and no page was loaded, per this task's constraints.
- Actual injection latency and flash severity (R2), and BFCache impact (R4) - both require runtime observation.
- Whether `boosts.json` should ever be covered by a future sync feature. Syncing user-authored injected content across devices is a separate trust decision and was not analysed.
