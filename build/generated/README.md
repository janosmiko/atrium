# Generated files

## tokens.css

The chrome layer's design tokens as CSS custom properties. `make tokens`
writes it, `make tokens-verify` guards it, and the WebUI serves a copy at
`$SRC/chrome/browser/resources/atrium/tokens.css`.

It is committed as the **reference**. The guard compares the WebUI copy against
it first and needs no Swift to do so, which is what keeps the check real after
the Swift generator is deleted. While Swift is still here, the
guard also re-runs the generator and compares.

### Who generates it

`make tokens` runs the RUST generator (`rust/atrium-core/src/tokens.rs`), so
regeneration needs no Swift and no Chromium checkout. `tokens_reference.rs`
holds it to this committed file byte for byte, and it was control-tested by
changing one constant.

This generator replaces `TokenExport.swift`, which the Swift cut deleted.
Verification survives the cut on its own.

### Where each number comes from

The values are Rust literals now. This table records where each one CAME from,
which is what a reviewer needs when a number looks wrong.

| Token prefix | Swift symbol | File |
|---|---|---|
| `--atrium-width` … `--atrium-resize-handle-width` (33 lengths) | `SidebarMetrics.<camelCase>` | `SidebarMetrics.swift` |
| `--atrium-pill-fade-after`, `--atrium-pill-fade-duration` | `PillTiming.fadeAfter`, `PillTiming.fadeDuration` | `PillTiming.swift` |
| `--atrium-default-glass-opacity` | `TintPalette.defaultGlassOpacity` | `TintPalette.swift` |
| `--atrium-min-glass-opacity`, `--atrium-max-glass-opacity` | `TintPalette.glassOpacityRange` bounds | `TintPalette.swift` |
| `--atrium-glass-backdrop-alpha` | `ThemeMath.glassBackdropAlpha` | `ThemeMath.swift` |
| `--atrium-grain-overlay-multiplier` | `ThemeMath.grainOverlayMultiplier` | `ThemeMath.swift` |
| `--atrium-tint-luminance-ceiling` | `ThemeMath.tintLuminanceCeiling` | `ThemeMath.swift` |

The property name is the Swift symbol in kebab-case with an `--atrium-` prefix:
`favoriteTileMinWidth` becomes `--atrium-favorite-tile-min-width`.

### How each value is formatted

Getting this wrong produces a file that looks right and differs byte for byte.

| Kind | Rounding | Unit | Whole numbers |
|---|---|---|---|
| length | 2 decimal places | `px` | `220px`, never `220.0px` |
| duration | 4 decimal places | `s` | `3s`, never `3.0s` |
| scalar | 4 decimal places | none | `1`, never `1.0` |

Section order is fixed: lengths, then durations, then scalars, each under its
own comment, with a blank line before the second and third.
