# Prototype Ideas (2026-07-28)

Feature ideas from market research (Arc/Dia, AI browsers, power-user browsers) plus
original concepts, feeding the atrium-lab prototype. Companion to `brainstorm-notes.md`.

## Market read (mid-2026)

- Arc is frozen; The Browser Company pivoted to Dia (Atlassian-owned). Dia still has
  no Spaces - the top reason ex-Arc users refuse to switch. Atrium's core (profile-bound
  spaces, per-space theming, gesture switching) sits in the gap the market abandoned.
- Agentic autonomy is a liability: repeated prompt-injection exploits against Comet,
  privacy backlash against Atlas's global memory, user revolt over Firefox default-on AI.
- Sticky AI patterns: page summarization, reusable prompt "Skills", passive/ambient
  organization. Gimmicks: open-ended agents, cross-tab synthesis at scale, proactive
  default-on suggestions.

## Already have / in flight

Spaces + profiles, per-space theming, link peek (naming: **Keyhole**), swipe machine,
command bar, split view,
favorites grid, extension pill, Boosts (`boosts-design.md`), traffic routing,
private mode (`private-mode-design.md`), icon family, multi-window.

## A. Tab lifecycle (biggest gap; all prototype-able)

| # | Lab | Idea | Source | Notes | Effort |
|---|------|--------|-------|--------|-----|
| 1 | ✓ | Auto-archive idle tabs | Arc | Unpinned tabs archive after configurable TTL; revive from archive list. Arc's most universally liked invisible feature. In the lab: per-space TTL setting + Archived rail with Restore. | Easy |
| 2 | ✓ | Sidebar folders / tab groups | Arc, Dia | The one organizational primitive that never got the "too complex" backlash. Foldered tabs exempt from auto-archive. | Easy |
| 3 | ✓ | Snooze tabs | SigmaOS | Tab leaves sidebar, returns at chosen time. In the lab: Option-click the close button, duration menu + alert toggle, Snoozed rail in Overview with Wake. | Easy |
| 4 | ✓ | Tab hibernation | Vivaldi | Suspend background tabs to free memory. Dimmed favicon border = hibernated. In the lab: General toggle + timeout slider (15 s-8 h), per-space mode (Follow/Never/Always) and timeout, click to wake. | Medium |

## B. Lightweight browsing surfaces

| # | Lab | Idea | Source | Notes | Effort |
|---|------|--------|-------|--------|-----|
| 5 | ✓ | Foyer: ephemeral mini-window for external links | Arc (Little Arc) | Links from Slack/Mail open in transient window with "promote to tab in space X". Complements routing rules. Named for the entry room where visitors wait before being admitted. In the lab: demo Slack link opens the Foyer, Move-to-space menu, Esc/close leaves no trace. | Medium |
| 6 | ✓ | Pinned tabs never navigate away | Arc (Peek), Zen (Glance) | Off-site links from a favorite open in the Keyhole overlay instead of replacing the tab. In the lab: plain link clicks on pinned tabs and favorites open the Keyhole (pop-out/split from there); regular tabs navigate. | Medium |
| 7 | ✓ | Drag-to-tile split view | Vivaldi | Drag tab into viewport edge for 2-4 pane layouts instead of menu-driven split. In the lab: drop on pane edges to add (up to 4, mixed shapes canonicalize to a 2x2 grid), center swaps, panes rearrange by drag; fused sidebar row tracks all panes. | Medium |
| 8 | ✓ | Notes as a split-view surface | Arc | Native scratchpad in a split slot next to a page. In the lab: Notes Beside This Page (ctx) or /note (cmd bar) opens a per-space editable pad in the split; content persists across tabs/spaces; ⧉ closes; markdown shortcuts (#, -, 1., >, **, `, _); Overview > Notes lists every space's pad plus freestanding custom notes (+ New Note, preview, open, delete). | Easy |
| 25 | ✓ | Full URL toolbar toggle | Arc (Developer Mode, ⌘⇧D) | Per-site toggle that shows a traditional address bar above the web view; Arc auto-enables it for localhost. For devs who need to watch URLs change. In the lab: ⌘⇧D per site (sidebar address bar hides while up), Dev-space tabs auto-show localhost URLs, copy button. | Easy |

## C. AI layer (maps onto the Pro tier)

| # | Lab | Idea | Source | Notes | Effort |
|---|------|------|--------|-------|--------|
| 9 | ✓ | Local on-device summarization free, hosted chat paid | Brave (Ocelot) | Free local summarization reinforces trust brand at zero inference cost; hosted tier becomes an upgrade, not a gate. Use the OS model (Apple Foundation Models) - no bundled weights (a usable 1-3B model is 0.7-2 GB; Chrome/Edge only ship theirs as on-demand downloads). Non-Apple-Silicon fallback: Ollama. In the lab: ` toggles the AI sidebar on the opposite edge (summary + page chat); Summarize Page (ctx + /summarize) opens it; chat upsells unless the Hosted model is selected. | Hard |
| 10 | ✓ | Skills in the command bar | Dia | Reusable prompt shortcuts as command-bar entries. Most durable AI mechanic per reviewers. In the lab: Skills section (/summarize, /translate, /clip); AI ones honor the kill switch. | Medium |
| 11 | ✓ | Per-space AI memory, inspectable/deletable | Atlas (fixed) | Atlas proved memory useful (73% of sessions) and that global scope causes backlash. Space-scoped memory falls out of Atrium's architecture; nobody else has it. In the lab: summaries/chats write space-scoped entries; Settings > AI lists them per space with forget/clear; space ctx has AI Memory…. | Hard |
| 12 | ✓ | BYOM (Ollama) + visible AI kill switch | Brave, Firefox lesson | No polished native macOS browser offers bring-your-own-model. Ship the kill switch before anyone asks. In the lab: Settings > AI kill switch hides every AI surface; model select On-device / Hosted (Pro) / Ollama (host+port, any machine, free) / Custom endpoint (Pro, BYOK: URL + keychain-held API key). Tier rule: local = free, remote = Pro. | Medium |

## D. Ambient polish

| # | Lab | Idea | Source | Notes | Effort |
|---|-|-----|--------|-------|--------|
| 13 | ✓ | Sidebar media controls + auto-PiP on tab switch | Arc, Dia | Media Session API does the heavy lifting; native SwiftUI transport bar. In the lab: hover-play on Media rows, sidebar transport bar, PiP corner card when you leave the playing tab (click returns, pause/stop). | Medium |
| 14 | ✓ | Downloads: media tiles vs document list | Arc | Presentation-layer change only. In the lab: Downloads rail auto-splits into a media tile grid and a grouped document list. | Easy |
| 15 | ✓ | Tidy tab titles on pin | Arc Max | Clean "(3) Inbox - Gmail" noise when pinning. Heuristic first, LLM later. In the lab: prefix/suffix heuristic applied on pin-drop, logged. | Easy |
| 26 | ✓ | Sidebar on the right | Vivaldi, Edge | Layout option mirroring the whole chrome - sidebar, edge gestures, Shelf drop zone - to the right side. Settings toggle, global or per-space. In the lab (✓): Settings > General segment, mirrors sidebar/hidden-overlay/Shelf zone/resizer; traffic lights stay top-left in their own strip. | Medium |
| 27 | ✓ | Extension surfacing | Arc, Chrome | Per-workspace extension tray: one place listing every extension active on this space's profile; pin favorites so their icons always show in the chrome. Builds on the profile-bound extension pill. In the lab: puzzle button opens the per-space tray (profile-scoped), star pins extensions; placement setting puts pins in the chrome row, under the address bar, or as a folded corner overlay (hover expands; rides the dev bar when open). | Medium |
| 28 | | Screen capture | Arc, CleanShot | Three modes: custom region, viewport, full page. Export as picture or PDF. ⌘⇧+drag anywhere starts a region capture without entering a capture mode first. | Medium |

## E. Original concepts (larger features, no market equivalent)

| # | Lab | Idea | Concept | Effort |
|---|------|------|---------|--------|
| 16 | ✓ | Courtyard | Pinch out to fly backward out of the current space into a zoomed-out view: all spaces as themed rooms around a central court, each with its gradient and live tab thumbnails. Drag tabs between rooms; pinch in or click to enter. Mission Control for browsing - makes the Atrium name the product's mental model. In the lab: pinch-in (ctrl+wheel) or empty-space menu opens themed rooms with mini tab lists; drag tabs between rooms; click enters. | Hard |
| 17 | ✓ | Rewind | Session time machine: scrub the whole window state backward - tabs, layout, an hour ago, yesterday, before that accidental cmd-W. Local-only, so it doubles as a privacy story. Auto-archive (#1) becomes its passive face. In the lab: every structural change snapshots (60 kept); Rewind… scrubber restores tabs+selection+space, scoped to one space or the whole window. | Hard |
| 18 | ✓ | Mirror view | Open the same URL side-by-side as two different profiles: "view as Client A admin / Client B / logged out". Falls out of profile-bound spaces + split view; no other browser can do it. The demo that sells the Teams tier. In the lab: right-click → Mirror View… → any profile or Logged out; profile badges per pane; Esc, navigation or a space switch ends it. | Medium |
| 19 | ✓ | Skylight | Themes that live: space gradients follow the time of day - cool morning light, warm dusk, near-dark at night; optionally weather-aware. Animates the existing gradient engine; makes screenshots identifiable at a glance. In the lab: live day-cycle curve behind the Night Shift toggle, Ambient breathing gradient, Clear/Overcast/Rain, demo time scrubber. | Medium |
| 20 | ✓ | Doorways | Promote any pinned tab to its own Dock icon: a site-specific app window keeping its space's profile, theme, and extensions. Safari's "Add to Dock" without losing the identity model. In the lab: Create Doorway on pinned tabs, mini dock strip, themed standalone window with profile line. | Medium |
| 21 | ✓ | Shelf | Staging tray + save-for-later, merged with #24: drag tabs or links to the left edge (or right-click a tab); the tab stays open, a reference waits in the Overview rail with source space; Open jumps to the live tab or restores a closed one; export as Markdown with source links and highlights. Connective tissue between browsing and Notes (#8). In the lab: left-edge drop zone, ghost fly animation, annotated pages auto-shelve, Shelf rail with quotes, Markdown export. | Medium |
| 22 | ✓ | Marginalia | Highlight and annotate any page; notes persist locally and glow in the sidebar on revisit. Paired with per-space memory (#11), highlights become the index that answers "where did I read that?". In the lab: select text → Highlight opens the Notes pad beside the page with a docs-style comment card (quote + editable note, synced both ways); clicking a mark jumps to its comment; amber dot = highlights, blue dot = notes; quotes surface on the Shelf. | Medium |
| 23 | ✓ | Ambient edge | The rounded window frame as a peripheral signal surface: soft pulse on download complete, glow when a snoozed tab returns, slow warm sweep before a meeting in a pinned calendar tab. Information without notification. In the lab: ring on snooze return, pulse when the mock download lands, warm sweep 25 s after load. | Easy |
| 24 | | ~~The Gallery~~ | Merged into Shelf (#21): pages and fragments share one save-for-later surface. | - |

Naming family (decided): strangers arrive at the **Foyer** (#5), quick looks go through
the **Keyhole** (link peek), trusted sites get **Doorways** (#20), and anything saved
for later rests on the **Shelf** (#21).

## Skip

- Shared spaces via public URLs, Live Folders/Groups, Instant Links, Browse-for-Me:
  need hosted backends or per-service OAuth; wrong cost profile for a local-first app.
- Open-ended agentic automation: 2026 security record (CometJacking, invisible-text
  injection, password-manager hijack) makes it a trust liability.

## Prototype priority

1. Cluster A reshapes the sidebar - the heart of the mock - for the least effort.
2. AI surfaces (9-11) next: they anchor the paid tier and affect command bar +
   sidebar layout.
3. Flagship bets among the originals: Courtyard (16), Rewind (17), Mirror view (18) -
   each unmatched in the market and demo-able.
