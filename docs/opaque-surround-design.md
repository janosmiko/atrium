# Opaque window surround (native-app chrome mode) - design

Design pass. Adds a second window presentation: an opaque colour
framing the web content card. Today's all-glass surround stays the default.

On 2026-08-26 one decision recorded below was reversed: the sidebar no
longer keeps its glass in this mode. The reporter saw a neutral surround beside
a tinted sidebar column and read the two as one window's chrome disagreeing with
itself. The C++ half is unchanged - the fill still lands on `pane_container_`
for the reasons section 2 gives - and the sidebar answers the mode from the
page, which is the only side that can paint across that edge. Section 2's
consequence list is annotated where it went stale.

Scope: what paints what, which ABI op is needed, how the mode relates to
per-space themes, and how the setting persists and applies live. No
implementation here.

## 1. What paints the surround today

There is no surround view. The surround is negative space - the part of
`AtriumRootView`'s bounds that the pane container's inset border leaves
uncovered.

| Layer | Owner | Detail |
|---|---|---|
| Behind-window glass | C++ `InstallLiquidGlassBackdrop`, `atrium_browser_window.mm:3418` | One window-wide `NSVisualEffectView`, material `UnderWindowBackground`, blending `BehindWindow`, state `Active`. Frosts the **desktop**. |
| Tint wash | C++ `AtriumRootView`'s `views::Background`, set by `SetBackdropTint` (`:3538`) or `AtriumThemeBackground` via `PaintThemeBackdrop` (`:4356`) | Solid tint or 192x108 theme raster, composited at `backdrop_glass_alpha_` (default `0xB8`). |
| Corner clip | `UpdateWindowCornerMask` (`:3448`) | `contentView` and the glass view get `cornerRadius = AtriumWindowCornerRadius()` + `masksToBounds = YES`. |
| Content card | `AtriumPaneView` tiles | Fully opaque, own per-tile radii. |

Root view layout (`AtriumRootView::AtriumRootView`, `:1265`), horizontal
`BoxLayout`:

```
[ sidebar_web_container_ (fixed width) ][ pane_container_ (flex 1) ]
```

`pane_container_` owns `ContentCardInset()` (the `borderWidth` setting, 10 by
default) as an empty border
(`ApplyContentInsets`, `:1069`). The sidebar-adjacent side is inset 0 - that
gutter is the sidebar's own trailing padding. So **every gutter except the
sidebar-adjacent one lies inside `pane_container_`'s bounds**.

The docked sidebar paints no background at all. `SidebarView.swift:723-729`
states it directly: the sidebar region, the gutter and the card margin "all
read as ONE tinted glass", and "an opaque fill would block the glass in
exactly the sidebar region".

## 2. Decision: C++ paints it, on `pane_container_`

Swift cannot paint the surround - it has no view there, and giving it one
means a new hosted NSView across the whole frame. Rejected.

Within C++ there are three candidate insertion points:

| Where | Effect | Verdict |
|---|---|---|
| `backdrop_glass_alpha_` -> `0xFF` | Sidebar goes opaque too (same surface), and the status bubble goes opaque (`StatusBubbleBackgroundColor`, `:3863`, returns `backdrop_tint_` alpha included). Carries no colour. | Reject |
| `root_view_`'s background | Same surface as the sidebar. Opaque there = opaque sidebar. | Reject |
| **`pane_container_`'s background** | Covers exactly the three card gutters plus the area behind the card. Sidebar column untouched. | **Adopt** |

An opaque `views::Background` on `pane_container_` is the whole visual change.
It needs no layer promotion: a non-layer-backed background paints into the
pane's paint layer, beneath native NSViews (the mechanism seen on
the peek scrim) - which is where we want it, under the card and over the glass.

Consequences that follow for free:

- ~~**Sidebar is byte-for-byte unchanged in both modes.**~~ True of the C++
  half and no longer true of the window. the page paints the surround
  colour over its own glass while the mode is on, told by
  `PushSidebarAppearance`. The paths this bullet lists are still untouched -
  the sidebar covers them rather than losing them, so turning the mode off
  returns the column to glass with nothing to restore.
- **Status bubble stays glass**, because `backdrop_glass_alpha_` is not touched.
- **Immersive/fullscreen tracks automatically.** `ApplyContentInsets` zeroes all
  insets while `immersive_`, so the card covers `pane_container_` entirely and
  the opaque fill has no visible area.
- **Little windows work unchanged.** Vertical `BoxLayout` of
  `[little_chrome_host_, pane_container_]` - the chrome strip keeps glass.

## 3. Decision: one new append-only ABI op

Needed, because C++ paints the surround and nothing existing carries the
information.

```c
// Appended LAST in AtriumHostOps, and LAST in the kHostOps initializer.
void (*set_opaque_surround)(AtriumHost* host, int32_t enabled, uint32_t argb);
```

- **The op is field 54, not 52.** This section originally said 52, appended
  after `toggle_extensions_developer_mode`. That was correct when written and
  went stale: the struct grew with `set_atrium_text_focus` and
  `set_backdrop_tint_live`. The op is appended after
  `set_backdrop_tint_live`, the actual last field, so `kHostOps` needs
  positional entry 54 (`atrium_browser_controller.mm`). A wrong number here is
  a silent ABI desync, not a compile error. Nothing mid-struct moves.
- `argb`'s top byte is ignored; C++ forces `SK_AlphaOPAQUE`, the same guard
  `ProgressAccentColor` (`:3629`) already applies.
- One op, not two: enabled and colour always change together at the paint site,
  and a second op is a second chance to desync.
- Degradation: Swift optional-chains the pointer (`Exports.swift` pattern, e.g.
  `set_glass_backdrop_alpha?`), so a stale chrome build no-ops and the window
  stays all-glass. That is the correct fallback - it is today's default. As with
  `set_glass_backdrop_alpha` there is no older op to fall back to, because this
  lever never existed.

Why not reuse `set_glass_backdrop_alpha`: it is an alpha byte on a surface
shared with the sidebar and the status bubble, and it carries no colour. Using
it would make the sidebar opaque, which is the one thing this mode must not do.

Why not overload `set_backdrop_tint`'s alpha byte: C++ discards that byte today
and Swift always sends `0xFF`. Redefining a shipped op's semantics is worse than
appending.

## 4. Decision: the surround is theme-independent

**Recommended policy (escalated - see section 8):** the surround colour is
independent of per-space themes and Theme Studio gradients. Default is
appearance-adaptive neutral (white in light, near-black in dark) with an
optional explicit colour override that applies in both appearances.

The reasoning that makes this cheap rather than a compromise: because the
opaque fill lands on `pane_container_`, the theme keeps painting `root_view_`
exactly as today, and the sidebar keeps showing exactly the same slice of the
192x108 raster it shows now (`ThemeMath.swift:245-256`: the docked sidebar
crops nothing - one landscape raster is stretched across the whole window and
the sidebar shows whichever slice falls in its bounds). The off-sidebar part of
the gradient is simply hidden behind the opaque fill.

| Option | Behaviour | Cost |
|---|---|---|
| **A. Independent (recommended)** | Neutral frame; theme lives in the sidebar. Sidebar identical across modes. | Zero theme code. |
| B. Theme-derived tone | Frame takes the space tint flattened toward white. Keeps per-space identity in the frame. | Needs a gradient -> single-tone reduction for Theme Studio themes, and repaints the frame on every space switch. |
| C. Independent + explicit "match space theme" opt-in | A plus B as a third choice. | B's cost, deferred. |

Rule: **in opaque mode, per-space themes and Theme Studio
gradients colour the sidebar only. The surround does not participate.** Its
colour comes from the appearance-adaptive default or the user's explicit
override.

Appearance handling: resolve light/dark in Swift with the existing
`SpacesViewModel.isSystemDark()` (`SpacesViewModel.swift:3644`, test seam
`systemDarkOverride`) and push a resolved RGB, so the ABI stays a plain colour.
Note a pre-existing limitation inherited rather than introduced: there is no
system-appearance-change observer anywhere in the app, so a mid-session dark
mode toggle does not repaint the theme raster today and will not repaint an
adaptive surround either. Out of scope.

## 5. Seam verdict: the docked sidebar keeps its glass, no seam mechanism exists

Plain answer: **yes, it works, and the failure mode the task feared cannot
occur for the docked sidebar.**

The docked sidebar's glass is `NSVisualEffectMaterialUnderWindowBackground` with
`NSVisualEffectBlendingModeBehindWindow`. `BehindWindow` samples what is on
screen *behind the window* - the desktop - not sibling views inside the window.
The surround is a sibling view inside the same window. Making it opaque cannot
change what the sidebar's material reads. Nothing needs to change for the
docked sidebar to still look like glass.

This is the same rule documented at `SidebarView.swift:2144-2165`:
blending mode follows where the sampled content lives, not construction. Here it
cuts in our favour.

What is genuinely at risk is **aesthetic adjacency, not material behaviour**: a
glass column tinted by the space theme sits directly against a flat neutral
frame. The two surfaces will not match, by design. Whether that reads as
intentional (coloured sidebar, neutral page frame - Safari and Arc both do this)
or as two unrelated materials is a judgment only the eye settles. Nothing in
code can decide it.

Two secondary items, both real and both minor:

- **Hover-reveal overlay sidebar.** A separate child NSWindow using
  `.behindWindow` (`SidebarView.swift:2171`), landing over the main window. Its
  outer ~5pt (`kOverlayHorizontalInset`) currently overlaps a translucent
  gutter; in opaque mode that strip samples a flat colour instead of the
  desktop. The remainder still overlaps page content, so the panel still reads
  as glass. Worth a manual look, not a blocker.
- **Collapsed sidebar.** `ApplyContentInsets` restores all four insets when
  `collapsed_`, so in opaque mode with the sidebar collapsed there is no glass
  anywhere in the window except the hover overlay. Expected, not a defect.

## 6. Geometry: radii, split view, docked DevTools

| Surface | Behaviour in opaque mode |
|---|---|
| Window corner | Unchanged. `contentView` has `masksToBounds = YES` and `cornerRadius = AtriumWindowCornerRadius()` (`:3474-3480`), so the opaque fill is clipped to the window silhouette without its own mask. |
| Content card radius | Unchanged. `ContentCardCornerRadius() = ConcentricRadius(ContentCardInset())` = 6 at the default gutter of 10 and a window corner of 16. Opaque mode does not touch the gutter width, so the concentric gap is unchanged whatever the user sets it to. |
| Split view | The divider gap between the two cards lies inside `pane_container_` and goes opaque with the rest of the frame. Each pane keeps its own radii. No change needed. |
| Docked DevTools | `CardCornerRadii` rounds only the corners a tile shares with the card; the contents/DevTools seam stays square. Unaffected - the surround is outside the card. |
| Immersive | No visible surround (insets zeroed, shadow hidden, radii 0). |
| Right-docked sidebar | `ApplyContentInsets` gives `pane_container_` a `kTitlebarHeight` top inset so the traffic lights stay off live content. In opaque mode that band becomes an opaque strip at the top-left with the glass sidebar on the right. Needs an eye. |

Two contrast items, both tuning rather than correctness:

- **The card has no border or hairline.** Its edge reads from the tint contrast
  plus the drop shadow (`kContentCardShadowColor` = black `0x47`, elevation 4,
  `:299`). On a white surround the shadow becomes the only separation cue and
  will read heavier than it does on glass. Expect to tune elevation per mode.
- **`empty_card_`'s placeholder wash** is translucent white `0x1A` (`:585`),
  tuned against a frosted backdrop. On a white opaque surround it nearly
  vanishes.

Verify at implementation, because it compiles clean either way: that the opaque
fill is genuinely clipped to the window corner. The diagnostic fingerprint is
the peek scrim's - four `r x r` notches at the corners where a square fill overruns a
rounded silhouette.

## 7. Persistence and live apply

Follows the shape of the other window-appearance settings exactly. No new mechanism.

| Step | Where | Notes |
|---|---|---|
| Storage | `SettingsStore.opaqueSurround: Bool` (default `false`), `SettingsStore.surroundColor: String?` (nil = automatic) | Optional in `FileShape`, defaulted and validated on every `init` path, same as `glassOpacity` (`SettingsStore.swift:205,252,287`). |
| Disk | `settings.json` via `SecureFileWriter` | Atomic temp-then-rename, 0600. Unchanged. |
| Setter | `SettingsModel.setOpaqueSurround(_:)` / `setSurroundColor(_:)` | Clamp/validate, mutate store, `persist()`, broadcast. Mirrors `setGlassOpacity` (`SettingsModel.swift:249`). |
| Live mirror | Only if a SwiftUI surface needs it | Nothing in SwiftUI paints the surround, so no `AtriumSurroundSettings` mirror unless the Settings preview needs one. Do not add an unread mirror for symmetry. |
| Broadcast | `SpacesViewModel.broadcastOpaqueSurround(...)` over `registry.allHosts()` | Same shape as `broadcastGlassBackdropAlpha` (`SpacesViewModel.swift:1313`). |
| New windows | `pushOpaqueSurround(toHost:)` from `Exports.registerHost` | **Required.** Alongside the existing `pushGlassBackdropAlpha` call (`Exports.swift:1087-1088`), or windows opened later start all-glass. |
| Throttle | Colour picker drag only | `SidebarResizeThrottle` at ~30Hz (`SidebarResize.swift:19`), per the m4-dragarch rule against bridge ops at raw mouse-event rate. The mode toggle is discrete and fires once - no throttle. |
| Gesture end | Unconditional re-broadcast + disk write | Same as the glass slider (`SettingsAppearancePane.swift:245-249`), so a final tick dropped inside a throttle window still lands. |

UI lives in Settings > Appearance next to the glass-opacity slider. Colour
picker: `HueRingPickerView` / `HueRingPickerModel` already exist for Theme
Studio and are the reuse candidate.

## 8. Settled decisions

All five were escalated as recommendations and accepted as a block on
2026-07-27. They are decisions now, not options - implement against them.

| # | Question | Decision |
|---|---|---|
| 1 | Is the surround theme-independent (section 4, option A), or should it derive a tone from the space theme (option B)? | **A, independent.** Zero theme code, and the sidebar stays pixel-identical between modes so the theme keeps a full-strength home. |
| 2 | Does the sidebar's trailing gutter stay glass (it is the sidebar's own padding), or should the opaque fill run right up to the sidebar's true edge? | **Stays glass.** Zero code, and the glass reads as the sidebar's own edge padding. The flush alternative needs a new inset path and crowds the card. |
| 3 | Right-docked: an opaque titlebar band at the top-left under the traffic lights, glass sidebar on the right. Acceptable? | **Accepted for v1.** The band merges into the frame as one continuous surface. Revisit only if it reads badly in use. |
| 4 | Collapsed sidebar in opaque mode has no glass anywhere except the hover overlay. Acceptable? | **Accepted** - the honest consequence of "glass only in the sidebar". The mode is deliberately not coupled to collapse state; a setting that silently stops applying is worse. |
| 5 | Card shadow elevation on a white surround (section 6). | **Ship at current elevation, tune from a capture.** Not a code decision, and mockups cannot settle shadow weight. No second per-mode constant for now. |

## 9. Not determined

- The reference screenshot named in the task is not committed anywhere in the
  repo, so the exact target look is inferred from the task text, not observed.
- That an opaque `views::Background` on `pane_container_` paints where expected
  and is clipped by the `contentView` corner mask. Both follow from the code
  cited above; neither has been built or captured.
- Whether the appearance-adaptive dark tone should be a specific existing
  constant rather than a new one - no dark neutral for chrome surfaces was found
  in `ThemeMath`/`TintPalette`.
