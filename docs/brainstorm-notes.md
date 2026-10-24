# Atrium — brainstorm decisions (working notes, 2026-07-03)

Session notes captured mid-brainstorm. Feeds the design doc at `docs/superpowers/specs/`.

## Product

- Personal daily-driver browser, macOS only (Apple Silicon).
- Hard requirement: full Chrome Web Store extension compatibility.
- Arc-style model: workspaces ("spaces") are tab sets; each space is assigned to a profile; multiple spaces can share one profile.

## Architecture (Section 1 approved)

- Approach B: native Swift/SwiftUI/AppKit UI bridged into an ungoogled-chromium base (Arc/Mori model).
- Repo shape: `patches/` (quilt series), `swift/` (UI), `bridge/` (ObjC++ "BrowsahKit", only layer seeing both worlds), `build/`.
- Rebase every second Chromium stable. Patch philosophy: add files, don't modify upstream.
- M0 feasibility gate: embed a live WebContents in a SwiftUI-hosted window (input/focus/IME working). If miserable, fall back to Approach A (fork Helium) with product design intact.
- Research findings: CEF can't do full extensions + custom UI; Electron shims are partial (MV3 service workers broken as of Electron 40); only content-layer Chromium fork delivers both.
- Chromium release cadence goes 4 weeks -> 2 weeks in Sept 2026 (doubles rebase load).

## Brand

- Name: **Atrium** (won 3 naming rounds; kept despite known collisions — getatrium.dev, atrium.app taken; acceptable for a personal project).
- Palette: deep atrium greens (#122E27, #1D4A3F, #2E6E5E) + skylight amber/cream (#F0C987, #FBF3DC, #D98E32, #E2A94F) + rose gold variant (#F4C08A, #FDEBD2, near-black #050D0B).
- Icon: switchable **icon family** (in-app picker, Settings -> Appearance; swap Dock icon via NSApp.applicationIconImage at launch/runtime).
  - Family (8): E1 Original Eclipse, E3 Daylight (inverted), E7 Rose Gold (user favorite, proposed default), M1 Sunset Gate, M3 Night Gate, G1 Sunset Gate Eclipsed, G2 Night Gate Eclipsed, G4 Dusk Rose.
- Motif language: eclipse (dark disc + corona), moon gate (circular portal), small suns/eclipses, warm light in dark fields.

## UI/UX (decided)

- Window layout: **A + C hybrid** — Arc-classic single sidebar (URL pill, pinned grid, tab list) with an auto-hide/floating overlay mode (Zen-style) for full-bleed browsing.
- Space switcher at the **bottom of the sidebar**: style C "peek drawer" (mini color-cards, drag-to-reorder, alt-tab hint) as default; styles A (dots) and B (icon row) selectable in Settings.
- Space switch animation: sidebar slides horizontally, backdrop crossfades tint (Arc-style). Shortcuts: alt-tab cycle, cmd-1..9 jump, two-finger horizontal swipe.
- Command bar (cmd-T): centered floating palette (Raycast-style) as default; sidebar-inline mode selectable in Settings. Results mix open tabs (tagged with space), history, search, commands.
- Search engine: ask on first run.
- Extension UI: floating translucent pill at page top-right (badges visible, auto-fade) as DEFAULT; sidebar-row and hidden/palette-only modes also supported (accepted cost: popup anchoring in multiple modes is bridge-level work; later milestone, not MVP).
- Page framing: rounded, floating on space-tinted backdrop.
- Personalization is a brand pillar: switchable app icon, switcher style, command bar mode, extension UI mode.

## Still to decide / next steps

1. Resume technical design Sections 2+ (components, data model, milestones, error handling, testing) -> write spec -> writing-plans.
2. Default icon confirmation (assumed E7 Rose Gold).
