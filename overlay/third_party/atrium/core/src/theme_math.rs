// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! Pure-function port of `ThemeMath.swift`.

use std::f64::consts::PI;

use crate::small_stores::{ThemeEffects, ThemeSpec, ThemeStop};
use crate::tint;

/// Ceiling `dominant_tint_hex` keeps its output at or below - the crossover
/// of WCAG's white-text and black-text contrast curves, so white sidebar
/// text stays legible on anything at or under it.
const TINT_LUMINANCE_CEILING: f64 = 0.1791;

const FALLBACK_HEX: &str = "#12282E";

/// Longest theme name the store keeps. The studio's own field is unbounded and
/// the name rides every preview frame, so the cap is enforced here.
const MAX_NAME_CHARS: usize = 64;

/// Per-appearance colours derived from a theme, ready to drop into CSS.
#[derive(Debug, Clone, PartialEq)]
pub(crate) struct Tokens {
    pub accent: String,
    pub dominant: String,
    pub text: String,
    pub hover_fill: String,
    pub active_fill: String,
}

/// Clamps every numeric field to its documented range, trims and caps the
/// name, drops stops beyond 5, and repairs an empty stop list or a malformed
/// hex. Idempotent.
pub fn sanitized(spec: &ThemeSpec) -> ThemeSpec {
    let fallback = fallback_stop();
    let source: &[ThemeStop] = if spec.stops.is_empty() {
        std::slice::from_ref(&fallback)
    } else {
        &spec.stops
    };
    let stops: Vec<ThemeStop> = source.iter().take(5).map(sanitize_stop).collect();

    ThemeSpec {
        name: sanitized_name(&spec.name),
        stops,
        opacity: spec.opacity.clamp(0.0, 1.0),
        effects: ThemeEffects {
            grain: spec.effects.grain.clamp(0.0, 1.0),
            vignette: spec.effects.vignette.clamp(0.0, 1.0),
            frost: spec.effects.frost.clamp(0.0, 30.0),
            warmth: spec.effects.warmth.clamp(-1.0, 1.0),
        },
        dark_adaptation: spec.dark_adaptation.clamp(0.0, 1.0),
        night_shift_enabled: spec.night_shift_enabled,
        ambient_enabled: spec.ambient_enabled,
        version: spec.version,
    }
}

/// Characters, not bytes - a byte cut would split a multi-byte character and
/// the JS half (theme-studio.js) counts code points too.
fn sanitized_name(raw: &str) -> String {
    raw.trim().chars().take(MAX_NAME_CHARS).collect()
}

fn fallback_stop() -> ThemeStop {
    ThemeStop {
        x: 0.5,
        y: 0.45,
        color: FALLBACK_HEX.to_string(),
        alpha: 0.9,
    }
}

fn sanitize_stop(stop: &ThemeStop) -> ThemeStop {
    ThemeStop {
        x: stop.x.clamp(0.0, 1.0),
        y: stop.y.clamp(0.0, 1.0),
        color: normalized_hex(&stop.color),
        alpha: stop.alpha.clamp(0.0, 1.0),
    }
}

/// `"#RRGGBB"` uppercased, or the fallback hex for anything else. Checks
/// bytes rather than slicing the trimmed string, so a multi-byte input can
/// never land the length check on a non-ASCII character and panic.
fn normalized_hex(raw: &str) -> String {
    let trimmed = raw.trim();
    let bytes = trimmed.as_bytes();
    let valid =
        bytes.len() == 7 && bytes[0] == b'#' && bytes[1..].iter().all(u8::is_ascii_hexdigit);
    if valid {
        trimmed.to_ascii_uppercase()
    } else {
        FALLBACK_HEX.to_string()
    }
}

/// A stop's rasterize-time (colour, effective alpha, position).
type MeshStop = ((f64, f64, f64), f64, (f64, f64));

/// Inverse-distance-weighted gradient mesh as a straight-alpha RGBA8 buffer.
/// `ambient_time: None` keeps the authored stop positions, `Some(t)` orbits them.
pub fn rasterize(
    spec: &ThemeSpec,
    width: usize,
    height: usize,
    dark: bool,
    warmth_offset: f64,
    ambient_time: Option<f64>,
) -> Vec<u8> {
    let stops = &spec.stops;
    // Warmth (night shift) nudges stop colors toward orange before the mesh
    // is blended, so night shift and ambient motion compose freely.
    let wf = (spec.effects.warmth + warmth_offset).clamp(-1.0, 1.0) * 0.12;
    let colors: Vec<(f64, f64, f64)> = stops
        .iter()
        .map(|stop| {
            let mut rgb = hex_to_rgb(&stop.color);
            if dark {
                rgb = adapt_dark(rgb, spec.dark_adaptation);
            }
            (
                (rgb.0 * (1.0 + wf)).clamp(0.0, 1.0),
                rgb.1,
                (rgb.2 * (1.0 - wf)).clamp(0.0, 1.0),
            )
        })
        .collect();
    let alphas: Vec<f64> = stops.iter().map(|stop| stop.alpha * spec.opacity).collect();
    let positions: Vec<(f64, f64)> = match ambient_time {
        Some(t) => ambient_positions(stops, t),
        None => stops.iter().map(|stop| (stop.x, stop.y)).collect(),
    };
    let mesh: Vec<MeshStop> = colors
        .into_iter()
        .zip(alphas)
        .zip(positions)
        .map(|((c, a), p)| (c, a, p))
        .collect();

    let aspect = width as f64 / height as f64;
    let mut buffer = vec![0u8; width * height * 4];

    let stop_count = mesh.len();
    // Every term that only depends on the column is lifted out of the pixel
    // loop: the horizontal offset to each stop is the same all the way down a
    // column, and squaring it is the expensive half.
    let mut column_dx2 = vec![0.0f64; width * stop_count];
    for x in 0..width {
        let nx = if width > 1 {
            x as f64 / (width - 1) as f64
        } else {
            0.0
        };
        for (slot, (_, _, pos)) in mesh.iter().enumerate() {
            let dx = (nx - pos.0) * aspect;
            column_dx2[x * stop_count + slot] = dx * dx;
        }
    }

    let falloff = Falloff::new();
    let mut row_dy2 = vec![0.0f64; stop_count];
    for y in 0..height {
        let ny = if height > 1 {
            y as f64 / (height - 1) as f64
        } else {
            0.0
        };
        for (slot, (_, _, pos)) in mesh.iter().enumerate() {
            let dy = ny - pos.1;
            row_dy2[slot] = dy * dy;
        }
        for x in 0..width {
            let (mut sum_r, mut sum_g, mut sum_b, mut sa, mut sw) = (0.0, 0.0, 0.0, 0.0, 0.0);
            let span = &column_dx2[x * stop_count..(x + 1) * stop_count];
            for (((color, a, _), dx2), dy2) in mesh.iter().zip(span).zip(&row_dy2) {
                let weight = 1.0 / (falloff.at(dx2 + dy2) + 1e-4);
                let wa = weight * a;
                sum_r += wa * color.0;
                sum_g += wa * color.1;
                sum_b += wa * color.2;
                sa += wa;
                sw += weight;
            }
            let idx = (y * width + x) * 4;
            buffer[idx] = byte_value(if sa > 0.0 { sum_r / sa } else { 0.0 });
            buffer[idx + 1] = byte_value(if sa > 0.0 { sum_g / sa } else { 0.0 });
            buffer[idx + 2] = byte_value(if sa > 0.0 { sum_b / sa } else { 0.0 });
            buffer[idx + 3] = byte_value(if sw > 0.0 { sa / sw } else { 0.0 });
        }
    }

    let radius = (spec.effects.frost / 4.0) as usize;
    if radius > 0 {
        buffer = box_blur(&buffer, width, height, radius);
    }
    if spec.effects.vignette > 0.0 {
        apply_vignette(&mut buffer, width, height, spec.effects.vignette);
    }
    buffer
}

/// Anchors spread over the mantissa range `[1, 2)`, picked by the top 6
/// mantissa bits. The leftover gap is then under 1/64, which holds the
/// series tail below 1e-12.
const FALLOFF_ANCHORS: usize = 64;

/// `d.powf(2.4)` from a squared distance, as `x * x^0.2` with the fifth root
/// read off `anchor` (mantissa) and `binade` (the five `e/5` remainders).
struct Falloff {
    anchor: [f64; FALLOFF_ANCHORS],
    inv_anchor: [f64; FALLOFF_ANCHORS],
    binade: [f64; 5],
}

impl Falloff {
    fn new() -> Self {
        let mut anchor = [0.0; FALLOFF_ANCHORS];
        let mut inv_anchor = [0.0; FALLOFF_ANCHORS];
        for step in 0..FALLOFF_ANCHORS {
            let m = 1.0 + step as f64 / FALLOFF_ANCHORS as f64;
            anchor[step] = m.powf(0.2);
            inv_anchor[step] = 1.0 / m;
        }
        let mut binade = [0.0; 5];
        for (r, value) in binade.iter_mut().enumerate() {
            *value = 2.0f64.powf(r as f64 / 5.0);
        }
        Self {
            anchor,
            inv_anchor,
            binade,
        }
    }

    /// The anchors come from `powf` itself, so this tracks it to about 1e-15
    /// and no byte of the raster moves.
    fn at(&self, distance_squared: f64) -> f64 {
        // Zero, subnormal, infinite and NaN have no mantissa to split.
        if !distance_squared.is_normal() || distance_squared < 0.0 {
            return distance_squared.powf(1.2);
        }
        let bits = distance_squared.to_bits();
        let exponent = ((bits >> 52) & 0x7ff) as i64 - 1023;
        let mantissa = f64::from_bits((bits & 0x000f_ffff_ffff_ffff) | 0x3ff0_0000_0000_0000);
        let step = ((bits >> 46) & 0x3f) as usize;
        let delta = mantissa * self.inv_anchor[step] - 1.0;
        // (1 + delta)^0.2, binomial series to the fifth term.
        let series = 1.0
            + delta
                * (0.2 + delta * (-0.08 + delta * (0.048 + delta * (-0.0336 + delta * 0.025_536))));
        let octave = f64::from_bits(((exponent.div_euclid(5) + 1023) as u64) << 52);
        let remainder = exponent.rem_euclid(5) as usize;
        distance_squared * self.anchor[step] * series * self.binade[remainder] * octave
    }
}

/// Three sequential horizontal+vertical box-blur passes approximating a
/// Gaussian frost.
fn box_blur(buffer: &[u8], width: usize, height: usize, radius: usize) -> Vec<u8> {
    // Clamping keeps every window at 2*radius+1 samples, so the mean of an
    // integer sum is one of 2*radius+1 fixed byte outputs.
    let window = 2 * radius + 1;
    let mean: Vec<u8> = (0..=255 * window)
        .map(|sum| (sum as f64 / window as f64).round() as u8)
        .collect();

    let mut src = buffer.to_vec();
    let mut dst = vec![0u8; buffer.len()];
    for _ in 0..3 {
        box_blur_pass(&src, &mut dst, width, height, radius, &mean, true);
        std::mem::swap(&mut src, &mut dst);
        box_blur_pass(&src, &mut dst, width, height, radius, &mean, false);
        std::mem::swap(&mut src, &mut dst);
    }
    src
}

/// Running window sum: each step drops the sample leaving the window and adds
/// the one entering it, so a wide frost costs no more than a narrow one.
fn box_blur_pass(
    src: &[u8],
    dst: &mut [u8],
    width: usize,
    height: usize,
    radius: usize,
    mean: &[u8],
    horizontal: bool,
) {
    let (length, lines) = if horizontal {
        (width, height)
    } else {
        (height, width)
    };
    if length == 0 || lines == 0 {
        return;
    }
    let (step, line_step) = if horizontal {
        (4, width * 4)
    } else {
        (width * 4, 4)
    };
    let last = length - 1;

    for line in 0..lines {
        let base = line * line_step;
        let mut sum = [0u32; 4];
        for offset in 0..=2 * radius {
            let idx = base + offset.saturating_sub(radius).min(last) * step;
            for (channel, total) in sum.iter_mut().enumerate() {
                *total += u32::from(src[idx + channel]);
            }
        }
        for pos in 0..length {
            let idx = base + pos * step;
            for (channel, total) in sum.iter().enumerate() {
                dst[idx + channel] = mean[*total as usize];
            }
            if pos == last {
                break;
            }
            let leaving = base + pos.saturating_sub(radius) * step;
            let entering = base + (pos + radius + 1).min(last) * step;
            for (channel, total) in sum.iter_mut().enumerate() {
                *total =
                    *total - u32::from(src[leaving + channel]) + u32::from(src[entering + channel]);
            }
        }
    }
}

/// Radial falloff centered above-and-left of the raster's true center.
/// Darkens rgb toward black only, alpha is untouched.
fn apply_vignette(buffer: &mut [u8], width: usize, height: usize, strength: f64) {
    for y in 0..height {
        let ny = if height > 1 {
            y as f64 / (height - 1) as f64
        } else {
            0.0
        };
        let dy2 = (ny - 0.42).powi(2);
        for x in 0..width {
            let nx = if width > 1 {
                x as f64 / (width - 1) as f64
            } else {
                0.0
            };
            let d2 = (nx - 0.5).powi(2) + dy2;
            // The falloff starts at 0.52, so the middle of the raster never
            // needs the root. 0.27 sits clear of 0.52 squared, so the skip
            // does not turn on how that product rounds.
            if d2 <= 0.27 {
                continue;
            }
            let dist = d2.sqrt();
            let v = ((dist - 0.52) / 0.6).clamp(0.0, 1.0);
            let factor = (strength * v).clamp(0.0, 1.0);
            if factor <= 0.0 {
                continue;
            }
            let idx = (y * width + x) * 4;
            buffer[idx] = (f64::from(buffer[idx]) * (1.0 - factor)).round() as u8;
            buffer[idx + 1] = (f64::from(buffer[idx + 1]) * (1.0 - factor)).round() as u8;
            buffer[idx + 2] = (f64::from(buffer[idx + 2]) * (1.0 - factor)).round() as u8;
        }
    }
}

/// One source gradient, never two: dims and desaturates in HSL space so the
/// dark variant reads as the same theme, not a different palette.
fn adapt_dark(rgb: (f64, f64, f64), k: f64) -> (f64, f64, f64) {
    let (h, s, l) = rgb_to_hsl(rgb);
    hsl_to_rgb(h, s * (1.0 - 0.25 * k), l * (1.0 - 0.7 * k))
}

/// Warmth offset from the clock: peaks at +0.44 around 02:00, zero
/// mid-afternoon. Fallback for when a real sunset/sunrise isn't available.
pub fn night_shift_offset(hour: f64) -> f64 {
    0.44 * (1.0 + (2.0 * PI * (hour - 2.0) / 24.0).cos()) / 2.0
}

/// Render-time-only stop positions: orbits the composition centroid so stops
/// slowly trade places, with per-stop angle/radius wobble. Never mutates the
/// authored stops.
fn ambient_positions(stops: &[ThemeStop], t: f64) -> Vec<(f64, f64)> {
    if stops.is_empty() {
        return Vec::new();
    }
    let n = stops.len() as f64;
    let cx = stops.iter().map(|s| s.x).sum::<f64>() / n;
    let cy = stops.iter().map(|s| s.y).sum::<f64>() / n;
    let th = t * 0.042;
    let wob = if stops.len() == 1 { 0.12 } else { 0.03 };

    stops
        .iter()
        .enumerate()
        .map(|(i, stop)| {
            let fi = i as f64;
            let a = th + (0.05 * t + 1.9 * fi).sin() * 0.15;
            let rr = 1.0 + 0.25 * (0.037 * t + 2.4 * fi).sin();
            let dx = stop.x - cx;
            let dy = stop.y - cy;
            let rotated_x = (dx * a.cos() - dy * a.sin()) * rr;
            let rotated_y = (dx * a.sin() + dy * a.cos()) * rr;
            let x = cx + rotated_x + (0.06 * t + 2.7 * fi).sin() * wob;
            let y = cy + rotated_y + (0.05 * t + 3.3 * fi).cos() * wob;
            (x.clamp(0.04, 0.96), y.clamp(0.04, 0.96))
        })
        .collect()
}

/// `accent` is the most saturated stop, lightened until legible on chrome.
/// `dominant` composites the mean stop colour over the glass base. `text` is
/// white or black, whichever clears `tint::MIN_CHROME_CONTRAST` against
/// `dominant` (the C++ `InkForBackdrop` rule). `hover_fill`/`active_fill`
/// are that ink at 8%/14% alpha, the sidebar's own `--fillhover` convention.
pub(crate) fn derived_tokens(spec: &ThemeSpec, dark: bool) -> Tokens {
    let adapted: Vec<(f64, f64, f64)> = spec
        .stops
        .iter()
        .map(|stop| {
            let rgb = hex_to_rgb(&stop.color);
            if dark {
                adapt_dark(rgb, spec.dark_adaptation)
            } else {
                rgb
            }
        })
        .collect();
    let eff_alphas: Vec<f64> = spec.stops.iter().map(|s| s.alpha * spec.opacity).collect();

    // Effective alpha and HSV saturation: HSL saturation would overrate
    // near-white stops and hand the accent to a pale highlight.
    let mut accent_rgb = (0.0, 0.0, 0.0);
    let mut best_score = -1.0;
    for (rgb, alpha) in adapted.iter().zip(&eff_alphas) {
        let score = hsv_saturation(*rgb) * (0.5 + 0.5 * alpha);
        if score > best_score {
            best_score = score;
            accent_rgb = *rgb;
        }
    }
    for _ in 0..8 {
        if relative_luminance(accent_rgb) >= 0.28 {
            break;
        }
        accent_rgb = (
            accent_rgb.0 + (1.0 - accent_rgb.0) * 0.14,
            accent_rgb.1 + (1.0 - accent_rgb.1) * 0.14,
            accent_rgb.2 + (1.0 - accent_rgb.2) * 0.14,
        );
    }

    let alpha_sum: f64 = eff_alphas.iter().sum();
    let mut mean_rgb = (0.0, 0.0, 0.0);
    if alpha_sum > 0.0 {
        for (rgb, a) in adapted.iter().zip(&eff_alphas) {
            mean_rgb.0 += rgb.0 * a;
            mean_rgb.1 += rgb.1 * a;
            mean_rgb.2 += rgb.2 * a;
        }
        mean_rgb = (
            mean_rgb.0 / alpha_sum,
            mean_rgb.1 / alpha_sum,
            mean_rgb.2 / alpha_sum,
        );
    }
    let mean_alpha = if eff_alphas.is_empty() {
        0.0
    } else {
        alpha_sum / eff_alphas.len() as f64
    };

    let base = glass_neutral_base(dark);
    let dominant_rgb = (
        (mean_rgb.0 * mean_alpha + base.0 * (1.0 - mean_alpha)).clamp(0.0, 1.0),
        (mean_rgb.1 * mean_alpha + base.1 * (1.0 - mean_alpha)).clamp(0.0, 1.0),
        (mean_rgb.2 * mean_alpha + base.2 * (1.0 - mean_alpha)).clamp(0.0, 1.0),
    );

    let dominant_hex = rgb_to_hex(dominant_rgb);
    let white_reads =
        tint::contrast_ratio(&dominant_hex, "#FFFFFF").unwrap_or(0.0) >= tint::MIN_CHROME_CONTRAST;
    let (text, ink) = if white_reads {
        ("#FFFFFF", (255, 255, 255))
    } else {
        ("#000000", (0, 0, 0))
    };

    Tokens {
        accent: rgb_to_hex(accent_rgb),
        dominant: dominant_hex,
        text: text.to_string(),
        hover_fill: format!("rgba({}, {}, {}, 0.08)", ink.0, ink.1, ink.2),
        active_fill: format!("rgba({}, {}, {}, 0.14)", ink.0, ink.1, ink.2),
    }
}

/// The neutral tone `derived_tokens`' `dominant` composites the mean stop
/// colour over.
fn glass_neutral_base(dark: bool) -> (f64, f64, f64) {
    if dark {
        (22.0 / 255.0, 24.0 / 255.0, 28.0 / 255.0)
    } else {
        (226.0 / 255.0, 228.0 / 255.0, 233.0 / 255.0)
    }
}

/// The overlay palette one accent colour derives, in the shape overlay.css's
/// `html.atrium-theme-*` classes carry by hand.
#[derive(Debug, Clone, PartialEq)]
pub(crate) struct AccentTokens {
    pub accent: String,
    /// `R G B`, not a colour: consumed as `rgb(var(--ov-ink) / alpha)`.
    pub ink: String,
    pub paper: String,
    pub soft: String,
    pub accent_ink: String,
}

/// Read off the brand book's hand-derived entries: its papers sit at
/// .943-.961 on the HSL lightness axis and its inks at .069-.098.
const ACCENT_PAPER_LIGHTNESS: f64 = 0.953;
const ACCENT_INK_LIGHTNESS: f64 = 0.085;
/// Lands within a couple of points of the book's own soft shade for Crimson,
/// Vermilion, Cyan and Amber alike.
const ACCENT_SOFT_LIFT: f64 = 0.38;
/// The book's inks are less saturated than their accents by about this much -
/// Crimson .36 against .50, Vermilion .66 against .82.
const ACCENT_INK_SATURATION: f64 = 0.75;

/// What `NSColor.controlAccentColor` answers for each `AppleAccentColor` the
/// macOS Appearance pane writes. AppKit caches that colour for the life of the
/// process, so a running window resolves the live default through this table.
const MACOS_ACCENTS: [(i32, &str); 8] = [
    (-1, "#8C8C8C"), // Graphite
    (0, "#FF5257"),  // Red
    (1, "#F7821B"),  // Orange
    (2, "#FFC600"),  // Yellow
    (3, "#62BA46"),  // Green
    (4, "#007AFF"),  // Blue
    (5, "#953D96"),  // Purple
    (6, "#F74F9E"),  // Pink
];

/// None for Multicolor, which writes no default at all and which AppKit paints
/// blue - the caller's own reading of `controlAccentColor` already says so.
pub fn macos_accent_hex(index: i32) -> Option<&'static str> {
    MACOS_ACCENTS
        .iter()
        .find(|(known, _)| *known == index)
        .map(|(_, hex)| *hex)
}

/// The four `--ov-*` values plus the ink that reads ON the accent. Hue is
/// held and lightness moved, so two accents differing only in hue still yield
/// a different paper and ink. A malformed hex reads as black.
pub(crate) fn accent_tokens(accent_hex: &str) -> AccentTokens {
    let rgb = hex_to_rgb(accent_hex);
    let (h, s, l) = rgb_to_hsl(rgb);

    let soft = hsl_to_rgb(h, s, l + (1.0 - l) * ACCENT_SOFT_LIFT);
    let paper = hsl_to_rgb(h, s, ACCENT_PAPER_LIGHTNESS);
    let ink = hsl_to_rgb(h, s * ACCENT_INK_SATURATION, ACCENT_INK_LIGHTNESS);

    // The grade `derived_tokens` gives its own text, so a yellow accent takes
    // black rather than white on near-white.
    let accent_ink = if tint::contrast_ratio(&rgb_to_hex(rgb), "#FFFFFF").unwrap_or(0.0)
        >= tint::MIN_CHROME_CONTRAST
    {
        "#FFFFFF"
    } else {
        "#000000"
    };

    AccentTokens {
        accent: rgb_to_hex(rgb),
        ink: format!(
            "{} {} {}",
            byte_value(ink.0),
            byte_value(ink.1),
            byte_value(ink.2)
        ),
        paper: rgb_to_hex(paper),
        soft: rgb_to_hex(soft),
        accent_ink: accent_ink.to_string(),
    }
}

/// The `Space.tint` cache: the effective-alpha-weighted mean stop colour,
/// darkened toward black until it clears `TINT_LUMINANCE_CEILING`.
pub fn dominant_tint_hex(spec: &ThemeSpec) -> String {
    let eff_alphas: Vec<f64> = spec.stops.iter().map(|s| s.alpha * spec.opacity).collect();
    let alpha_sum: f64 = eff_alphas.iter().sum();
    let mut rgb = (0.0, 0.0, 0.0);
    if alpha_sum > 0.0 {
        for (stop, a) in spec.stops.iter().zip(&eff_alphas) {
            let c = hex_to_rgb(&stop.color);
            rgb.0 += c.0 * a;
            rgb.1 += c.1 * a;
            rgb.2 += c.2 * a;
        }
        rgb = (rgb.0 / alpha_sum, rgb.1 / alpha_sum, rgb.2 / alpha_sum);
    }
    // Bounded loop mirrors derived_tokens' accent-lighten loop.
    for _ in 0..24 {
        if relative_luminance(rgb) <= TINT_LUMINANCE_CEILING {
            break;
        }
        rgb = (rgb.0 * 0.9, rgb.1 * 0.9, rgb.2 * 0.9);
    }
    rgb_to_hex(rgb)
}

/// Synthesizes a solid single-stop theme from a `Space.tint` ref (a named
/// tint or a `"#RRGGBB"` hex) - the default theme for a space that has a
/// tint but no stored `ThemeSpec` yet. Sanitized before return.
pub fn single_stop_theme(tint_hex: &str) -> ThemeSpec {
    sanitized(&ThemeSpec {
        name: String::new(),
        stops: vec![ThemeStop {
            x: 0.5,
            y: 0.5,
            color: tint::resolve_hex(tint_hex),
            alpha: 1.0,
        }],
        opacity: 1.0,
        effects: ThemeEffects {
            grain: 0.0,
            vignette: 0.0,
            frost: 0.0,
            warmth: 0.0,
        },
        dark_adaptation: 0.45,
        night_shift_enabled: false,
        ambient_enabled: false,
        version: crate::schema::current::THEMES,
    })
}

// tint.rs's hex/luminance helpers are private to that file and take 0..255
// channels. This file works in 0..1 throughout, so these stay self-contained.

fn hex_to_rgb(hex: &str) -> (f64, f64, f64) {
    let trimmed = hex.strip_prefix('#').unwrap_or(hex);
    if trimmed.len() != 6 || !trimmed.bytes().all(|b| b.is_ascii_hexdigit()) {
        return (0.0, 0.0, 0.0);
    }
    let Ok(value) = u32::from_str_radix(trimmed, 16) else {
        return (0.0, 0.0, 0.0);
    };
    (
        f64::from((value >> 16) & 0xFF) / 255.0,
        f64::from((value >> 8) & 0xFF) / 255.0,
        f64::from(value & 0xFF) / 255.0,
    )
}

fn rgb_to_hex(rgb: (f64, f64, f64)) -> String {
    format!(
        "#{:02X}{:02X}{:02X}",
        byte_value(rgb.0),
        byte_value(rgb.1),
        byte_value(rgb.2)
    )
}

fn byte_value(v: f64) -> u8 {
    (v.clamp(0.0, 1.0) * 255.0).round() as u8
}

/// HSV-style saturation ((max-min)/max, 0 for black). Distinct from HSL
/// saturation, which reads near-white colors as highly saturated.
fn hsv_saturation(rgb: (f64, f64, f64)) -> f64 {
    let maxc = rgb.0.max(rgb.1).max(rgb.2);
    if maxc <= 0.0 {
        return 0.0;
    }
    (maxc - rgb.0.min(rgb.1).min(rgb.2)) / maxc
}

fn rgb_to_hsl(rgb: (f64, f64, f64)) -> (f64, f64, f64) {
    let (r, g, b) = rgb;
    let maxc = r.max(g).max(b);
    let minc = r.min(g).min(b);
    let l = (maxc + minc) / 2.0;
    if maxc == minc {
        return (0.0, 0.0, l);
    }
    let d = maxc - minc;
    let s = if l > 0.5 {
        d / (2.0 - maxc - minc)
    } else {
        d / (maxc + minc)
    };
    let h = if maxc == r {
        (g - b) / d + if g < b { 6.0 } else { 0.0 }
    } else if maxc == g {
        (b - r) / d + 2.0
    } else {
        (r - g) / d + 4.0
    };
    (h / 6.0, s, l)
}

fn hsl_to_rgb(h: f64, s: f64, l: f64) -> (f64, f64, f64) {
    if s == 0.0 {
        return (l, l, l);
    }
    let q = if l < 0.5 {
        l * (1.0 + s)
    } else {
        l + s - l * s
    };
    let p = 2.0 * l - q;
    (
        hue_to_rgb(p, q, h + 1.0 / 3.0),
        hue_to_rgb(p, q, h),
        hue_to_rgb(p, q, h - 1.0 / 3.0),
    )
}

fn hue_to_rgb(p: f64, q: f64, t: f64) -> f64 {
    let mut t = t;
    if t < 0.0 {
        t += 1.0;
    }
    if t > 1.0 {
        t -= 1.0;
    }
    if t < 1.0 / 6.0 {
        return p + (q - p) * 6.0 * t;
    }
    if t < 1.0 / 2.0 {
        return q;
    }
    if t < 2.0 / 3.0 {
        return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
    }
    p
}

/// WCAG relative luminance, `rgb` channels in 0..1. Shared with `tint.rs`,
/// which wraps this for its own 0..255 channels.
pub(crate) fn relative_luminance(rgb: (f64, f64, f64)) -> f64 {
    let lin = |v: f64| {
        if v <= 0.04045 {
            v / 12.92
        } else {
            ((v + 0.055) / 1.055).powf(2.4)
        }
    };
    0.2126 * lin(rgb.0) + 0.7152 * lin(rgb.1) + 0.0722 * lin(rgb.2)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::small_stores::{ThemeEffects, ThemeSpec, ThemeStop};

    fn solid(hex: &str, alpha: f64) -> ThemeSpec {
        ThemeSpec {
            name: "t".to_string(),
            stops: vec![ThemeStop {
                x: 0.5,
                y: 0.5,
                color: hex.to_string(),
                alpha,
            }],
            opacity: 1.0,
            effects: ThemeEffects {
                grain: 0.0,
                vignette: 0.0,
                frost: 0.0,
                warmth: 0.0,
            },
            dark_adaptation: 0.45,
            night_shift_enabled: false,
            ambient_enabled: false,
            version: 1,
        }
    }

    fn no_effects() -> ThemeEffects {
        ThemeEffects {
            grain: 0.0,
            vignette: 0.0,
            frost: 0.0,
            warmth: 0.0,
        }
    }

    // ---------------------------------------------------------- rasterize

    #[test]
    fn single_stop_raster_is_uniform_color() {
        let buf = rasterize(&solid("#336699", 1.0), 8, 4, false, 0.0, None);
        assert_eq!(buf.len(), 8 * 4 * 4);
        for px in (0..buf.len()).step_by(4) {
            assert_eq!(buf[px], 0x33);
            assert_eq!(buf[px + 1], 0x66);
            assert_eq!(buf[px + 2], 0x99);
            assert_eq!(buf[px + 3], 255);
        }
    }

    #[test]
    fn global_opacity_scales_alpha_channel() {
        let mut spec = solid("#336699", 1.0);
        spec.opacity = 0.5;
        let buf = rasterize(&spec, 2, 2, false, 0.0, None);
        assert!((f64::from(buf[3]) / 255.0 - 0.5).abs() < 0.01);
    }

    #[test]
    fn two_stop_raster_pixels_nearer_a_stop_are_closer_to_its_color() {
        let spec = ThemeSpec {
            name: "t".to_string(),
            stops: vec![
                ThemeStop {
                    x: 0.0,
                    y: 0.5,
                    color: "#FF0000".to_string(),
                    alpha: 1.0,
                },
                ThemeStop {
                    x: 1.0,
                    y: 0.5,
                    color: "#0000FF".to_string(),
                    alpha: 1.0,
                },
            ],
            opacity: 1.0,
            effects: no_effects(),
            dark_adaptation: 0.45,
            night_shift_enabled: false,
            ambient_enabled: false,
            version: 1,
        };
        let (w, h) = (16, 8);
        let buf = rasterize(&spec, w, h, false, 0.0, None);
        let left = buf[(h / 2 * w + 1) * 4];
        let right = buf[(h / 2 * w + w - 2) * 4];
        assert!(left > 200);
        assert!(right < 60);
    }

    #[test]
    fn frost_blur_reduces_the_difference_between_neighbouring_pixels() {
        let mut spec = ThemeSpec {
            name: "t".to_string(),
            stops: vec![
                ThemeStop {
                    x: 0.0,
                    y: 0.5,
                    color: "#FF0000".to_string(),
                    alpha: 1.0,
                },
                ThemeStop {
                    x: 1.0,
                    y: 0.5,
                    color: "#0000FF".to_string(),
                    alpha: 1.0,
                },
            ],
            opacity: 1.0,
            effects: no_effects(),
            dark_adaptation: 0.45,
            night_shift_enabled: false,
            ambient_enabled: false,
            version: 1,
        };
        let (w, h) = (32, 16);
        let sharp = rasterize(&spec, w, h, false, 0.0, None);
        spec.effects.frost = 40.0;
        let blurred = rasterize(&spec, w, h, false, 0.0, None);

        let row = h / 2;
        let x = w / 2;
        let sharp_diff =
            i32::from(sharp[(row * w + x) * 4]) - i32::from(sharp[(row * w + x + 1) * 4]);
        let blurred_diff =
            i32::from(blurred[(row * w + x) * 4]) - i32::from(blurred[(row * w + x + 1) * 4]);
        assert!(
            blurred_diff.abs() < sharp_diff.abs(),
            "frost did not smooth the transition: sharp={sharp_diff} blurred={blurred_diff}"
        );
    }

    #[test]
    fn vignette_darkens_the_corner_against_the_centre_only_when_enabled() {
        let (w, h) = (32, 32);
        let center_idx = (h / 2 * w + w / 2) * 4;
        let channel_sum = |buf: &[u8], idx: usize| -> u32 {
            u32::from(buf[idx]) + u32::from(buf[idx + 1]) + u32::from(buf[idx + 2])
        };

        let mut spec = solid("#AAAAAA", 1.0);
        spec.effects.vignette = 0.8;
        let vignetted = rasterize(&spec, w, h, false, 0.0, None);
        assert!(
            channel_sum(&vignetted, 0) < channel_sum(&vignetted, center_idx),
            "the corner must be darker than the centre with vignette on"
        );

        spec.effects.vignette = 0.0;
        let flat = rasterize(&spec, w, h, false, 0.0, None);
        assert_eq!(
            channel_sum(&flat, 0),
            channel_sum(&flat, center_idx),
            "a uniform raster with no vignette must be flat"
        );
    }

    #[test]
    fn warmth_offset_shifts_red_up_and_blue_down_by_the_expected_amount() {
        // grey (0x80,0x80,0x80) at warmth_offset 1.0 -> wf = clamp(1.0,-1,1) * 0.12 = 0.12.
        // red: 128*1.12 = 143.36 -> 143. blue: 128*0.88 = 112.64 -> 113. green untouched.
        let buf = rasterize(&solid("#808080", 1.0), 2, 2, false, 1.0, None);
        assert_eq!(buf[0], 143);
        assert_eq!(buf[1], 128);
        assert_eq!(buf[2], 113);
    }

    #[test]
    fn rasterize_with_ambient_time_differs_from_authored_and_from_a_later_time() {
        let spec = ThemeSpec {
            name: "t".to_string(),
            stops: vec![
                ThemeStop {
                    x: 0.2,
                    y: 0.3,
                    color: "#268BD2".to_string(),
                    alpha: 0.9,
                },
                ThemeStop {
                    x: 0.8,
                    y: 0.7,
                    color: "#CB4B16".to_string(),
                    alpha: 0.6,
                },
            ],
            opacity: 1.0,
            effects: no_effects(),
            dark_adaptation: 0.45,
            night_shift_enabled: false,
            ambient_enabled: true,
            version: 1,
        };
        let (w, h) = (16, 16);
        let authored = rasterize(&spec, w, h, false, 0.0, None);
        let at_zero = rasterize(&spec, w, h, false, 0.0, Some(0.0));
        let at_ten = rasterize(&spec, w, h, false, 0.0, Some(10.0));
        assert_ne!(
            authored, at_zero,
            "Some(t) must orbit away from the authored positions"
        );
        assert_ne!(at_zero, at_ten, "a later ambient time must keep moving");
    }

    #[test]
    fn falloff_tracks_powf_over_the_whole_distance_range() {
        let falloff = Falloff::new();
        let mut worst = 0.0f64;
        for exponent in -300..300 {
            for step in 0..97 {
                let x = (1.0 + step as f64 / 97.0) * 2.0f64.powi(exponent);
                let want = x.powf(1.2);
                let error = ((falloff.at(x) - want) / want).abs();
                worst = worst.max(error);
            }
        }
        // A byte moves at a relative error near 1e-3. Anything under 1e-12
        // keeps the raster identical to the powf it replaced.
        assert!(worst < 1e-12, "falloff drifted from powf by {worst:e}");

        for odd in [0.0, f64::INFINITY, f64::MIN_POSITIVE / 4.0] {
            assert_eq!(falloff.at(odd), odd.powf(1.2), "odd input {odd}");
        }
        assert!(falloff.at(f64::NAN).is_nan());
    }

    #[test]
    fn box_blur_running_window_matches_a_direct_window_sum() {
        let (w, h) = (13, 7);
        let buffer: Vec<u8> = (0..w * h * 4).map(|i| ((i * 37) % 256) as u8).collect();
        for radius in [1usize, 3, 9] {
            let direct = naive_box_blur(&buffer, w, h, radius);
            assert_eq!(
                box_blur(&buffer, w, h, radius),
                direct,
                "radius {radius} drifted from a direct window sum"
            );
        }
    }

    /// The pre-optimisation blur, kept as the oracle for the running-window
    /// rewrite: one full window summed per pixel, edges clamped not dropped.
    fn naive_box_blur(buffer: &[u8], width: usize, height: usize, radius: usize) -> Vec<u8> {
        let mut current = buffer.to_vec();
        for _ in 0..3 {
            for horizontal in [true, false] {
                let mut out = current.clone();
                let length = if horizontal { width } else { height };
                let lines = if horizontal { height } else { width };
                let radius = radius as isize;
                for line in 0..lines {
                    for pos in 0..length {
                        let mut sums = [0.0f64; 4];
                        let mut count = 0.0;
                        for offset in -radius..=radius {
                            let p = (pos as isize + offset).clamp(0, length as isize - 1) as usize;
                            let (x, y) = if horizontal { (p, line) } else { (line, p) };
                            let idx = (y * width + x) * 4;
                            for (channel, sum) in sums.iter_mut().enumerate() {
                                *sum += f64::from(current[idx + channel]);
                            }
                            count += 1.0;
                        }
                        let (x, y) = if horizontal { (pos, line) } else { (line, pos) };
                        let idx = (y * width + x) * 4;
                        for (channel, sum) in sums.iter().enumerate() {
                            out[idx + channel] = (sum / count).round() as u8;
                        }
                    }
                }
                current = out;
            }
        }
        current
    }

    // ------------------------------------------------------ dark adaptation

    #[test]
    fn adapt_dark_dims_and_desaturates() {
        let (r, g, b) = adapt_dark((0.8, 0.6, 0.4), 0.45);
        assert!(r.max(g).max(b) < 0.8);
        assert!(r.max(g).max(b) - r.min(g).min(b) <= (0.8 - 0.4) + 0.0001);
    }

    #[test]
    fn adapt_dark_zero_k_is_identity() {
        let (r, g, b) = adapt_dark((0.8, 0.6, 0.4), 0.0);
        assert!((r - 0.8).abs() < 0.001);
        assert!((g - 0.6).abs() < 0.001);
        assert!((b - 0.4).abs() < 0.001);
    }

    // ---------------------------------------------------------- night shift

    #[test]
    fn night_shift_peaks_near_two_am_and_zero_mid_afternoon() {
        assert!((night_shift_offset(2.0) - 0.44).abs() < 0.01);
        assert!(night_shift_offset(14.0).abs() < 0.01);
        assert!(night_shift_offset(22.0) > night_shift_offset(18.0));
    }

    // -------------------------------------------------------- ambient motion

    #[test]
    fn ambient_positions_stay_in_bounds_and_never_mutate_input() {
        let spec = ThemeSpec {
            name: "t".to_string(),
            stops: vec![
                ThemeStop {
                    x: 0.24,
                    y: 0.30,
                    color: "#268BD2".to_string(),
                    alpha: 0.90,
                },
                ThemeStop {
                    x: 0.78,
                    y: 0.68,
                    color: "#CB4B16".to_string(),
                    alpha: 0.45,
                },
                ThemeStop {
                    x: 0.60,
                    y: 0.10,
                    color: "#123842".to_string(),
                    alpha: 1.00,
                },
            ],
            opacity: 1.0,
            effects: no_effects(),
            dark_adaptation: 0.45,
            night_shift_enabled: false,
            ambient_enabled: false,
            version: 1,
        };
        let stops = spec.stops.clone();
        let mut t = 0.0;
        while t <= 400.0 {
            for p in ambient_positions(&stops, t) {
                assert!((0.04..=0.96).contains(&p.0));
                assert!((0.04..=0.96).contains(&p.1));
            }
            t += 13.7;
        }
    }

    /// Brief-specified range: `t` in 0..600s, step 7.
    #[test]
    fn ambient_positions_stay_in_bounds_over_ten_minutes() {
        let stops = vec![
            ThemeStop {
                x: 0.24,
                y: 0.30,
                color: "#268BD2".to_string(),
                alpha: 0.90,
            },
            ThemeStop {
                x: 0.78,
                y: 0.68,
                color: "#CB4B16".to_string(),
                alpha: 0.45,
            },
        ];
        let mut t = 0.0;
        while t < 600.0 {
            for p in ambient_positions(&stops, t) {
                assert!((0.04..=0.96).contains(&p.0));
                assert!((0.04..=0.96).contains(&p.1));
            }
            t += 7.0;
        }
    }

    #[test]
    fn ambient_positions_travel_far() {
        let stops = vec![
            ThemeStop {
                x: 0.24,
                y: 0.30,
                color: "#268BD2".to_string(),
                alpha: 0.90,
            },
            ThemeStop {
                x: 0.78,
                y: 0.68,
                color: "#CB4B16".to_string(),
                alpha: 0.45,
            },
            ThemeStop {
                x: 0.60,
                y: 0.10,
                color: "#123842".to_string(),
                alpha: 1.00,
            },
        ];
        let a = ambient_positions(&stops, 0.0);
        let b = ambient_positions(&stops, 75.0);
        let d = ((a[0].0 - b[0].0).powi(2) + (a[0].1 - b[0].1).powi(2)).sqrt();
        assert!(d > 0.15);
    }

    // -------------------------------------------------------- derived tokens

    #[test]
    fn derived_tokens_text_flips_with_luminance() {
        let dark = derived_tokens(&solid("#0A0A14", 1.0), false);
        let light = derived_tokens(&solid("#F2EFE8", 1.0), false);
        assert_ne!(dark.dominant, light.dominant);
        // dominant of a full-alpha solid theme is exactly the stop colour.
        assert_eq!(light.dominant, "#F2EFE8");
        assert_eq!(dark.dominant, "#0A0A14");
        assert_eq!(light.text, "#000000");
        assert_eq!(dark.text, "#FFFFFF");
    }

    #[test]
    fn derived_accent_prefers_saturated_stop_over_pale_highlight() {
        let spec = ThemeSpec {
            name: "t".to_string(),
            stops: vec![
                ThemeStop {
                    x: 0.3,
                    y: 0.3,
                    color: "#F5F0EA".to_string(),
                    alpha: 1.0,
                },
                ThemeStop {
                    x: 0.7,
                    y: 0.7,
                    color: "#2A6B4F".to_string(),
                    alpha: 0.5,
                },
            ],
            opacity: 1.0,
            effects: no_effects(),
            dark_adaptation: 0.45,
            night_shift_enabled: false,
            ambient_enabled: false,
            version: 1,
        };
        let t = derived_tokens(&spec, false);
        let (r, g, _b) = hex_to_rgb(&t.accent);
        assert!(
            g > r,
            "accent must derive from the green stop, not the pale highlight"
        );
    }

    #[test]
    fn derived_accent_score_uses_effective_alpha() {
        let spec = ThemeSpec {
            name: "t".to_string(),
            stops: vec![
                ThemeStop {
                    x: 0.3,
                    y: 0.3,
                    color: "#66CC52".to_string(),
                    alpha: 1.0,
                },
                ThemeStop {
                    x: 0.7,
                    y: 0.7,
                    color: "#E61A8C".to_string(),
                    alpha: 0.2,
                },
            ],
            opacity: 0.5,
            effects: no_effects(),
            dark_adaptation: 0.45,
            night_shift_enabled: false,
            ambient_enabled: false,
            version: 1,
        };
        let t = derived_tokens(&spec, false);
        let (r, g, _b) = hex_to_rgb(&t.accent);
        assert!(
            r > g,
            "accent must derive from the magenta stop under effective-alpha scoring"
        );
    }

    #[test]
    fn derived_accent_is_legible_on_chrome() {
        let t = derived_tokens(&solid("#101418", 1.0), true);
        let lum = relative_luminance(hex_to_rgb(&t.accent));
        assert!(lum >= 0.27);
    }

    #[test]
    fn derived_hover_and_active_fill_are_the_ink_colour_at_alpha() {
        let dark = derived_tokens(&solid("#0A0A14", 1.0), false);
        assert_eq!(dark.text, "#FFFFFF");
        assert_eq!(dark.hover_fill, "rgba(255, 255, 255, 0.08)");
        assert_eq!(dark.active_fill, "rgba(255, 255, 255, 0.14)");

        let light = derived_tokens(&solid("#F2EFE8", 1.0), false);
        assert_eq!(light.text, "#000000");
        assert_eq!(light.hover_fill, "rgba(0, 0, 0, 0.08)");
        assert_eq!(light.active_fill, "rgba(0, 0, 0, 0.14)");
    }

    // ------------------------------------------------------------- tint hex

    #[test]
    fn dominant_tint_hex_never_exceeds_the_luminance_ceiling() {
        let specs = [
            solid("#FFFFFF", 1.0),
            solid("#F2EFE8", 1.0),
            solid("#12282E", 1.0),
            ThemeSpec {
                name: "t".to_string(),
                stops: vec![
                    ThemeStop {
                        x: 0.3,
                        y: 0.3,
                        color: "#EED6A8".to_string(),
                        alpha: 0.8,
                    },
                    ThemeStop {
                        x: 0.7,
                        y: 0.7,
                        color: "#8FA6C8".to_string(),
                        alpha: 0.8,
                    },
                ],
                opacity: 1.0,
                effects: no_effects(),
                dark_adaptation: 0.45,
                night_shift_enabled: false,
                ambient_enabled: false,
                version: 1,
            },
        ];
        for spec in &specs {
            let hex = dominant_tint_hex(spec);
            let lum = relative_luminance(hex_to_rgb(&hex));
            assert!(
                lum <= TINT_LUMINANCE_CEILING + 1e-6,
                "{hex} exceeds the ceiling ({lum})"
            );
        }
    }

    // ------------------------------------------------------------ sanitize

    #[test]
    fn sanitized_clamps_stop_count_to_five() {
        let stop = ThemeStop {
            x: 0.5,
            y: 0.5,
            color: "#123456".to_string(),
            alpha: 1.0,
        };
        let spec = ThemeSpec {
            name: "t".to_string(),
            stops: vec![stop; 7],
            opacity: 1.0,
            effects: no_effects(),
            dark_adaptation: 0.45,
            night_shift_enabled: false,
            ambient_enabled: false,
            version: 1,
        };
        assert_eq!(sanitized(&spec).stops.len(), 5);
    }

    #[test]
    fn sanitized_repairs_empty_stops_with_atrium_preset_first_stop() {
        let spec = ThemeSpec {
            name: "t".to_string(),
            stops: vec![],
            opacity: 1.0,
            effects: no_effects(),
            dark_adaptation: 0.45,
            night_shift_enabled: false,
            ambient_enabled: false,
            version: 1,
        };
        assert_eq!(sanitized(&spec).stops.len(), 1);
    }

    #[test]
    fn sanitized_clamps_ranges() {
        let stop = ThemeStop {
            x: 2.0,
            y: -1.0,
            color: "#123456".to_string(),
            alpha: 3.0,
        };
        let spec = ThemeSpec {
            name: "t".to_string(),
            stops: vec![stop],
            opacity: 9.0,
            effects: ThemeEffects {
                grain: 5.0,
                vignette: -2.0,
                frost: 99.0,
                warmth: 7.0,
            },
            dark_adaptation: -3.0,
            night_shift_enabled: false,
            ambient_enabled: false,
            version: 1,
        };
        let s = sanitized(&spec);
        assert_eq!(s.stops[0].x, 1.0);
        assert_eq!(s.stops[0].y, 0.0);
        assert_eq!(s.stops[0].alpha, 1.0);
        assert_eq!(s.opacity, 1.0);
        assert_eq!(s.effects.grain, 1.0);
        assert_eq!(s.effects.vignette, 0.0);
        assert_eq!(s.effects.frost, 30.0);
        assert_eq!(s.effects.warmth, 1.0);
        assert_eq!(s.dark_adaptation, 0.0);
    }

    #[test]
    fn sanitized_normalizes_invalid_hex_to_fallback() {
        let stop = ThemeStop {
            x: 0.5,
            y: 0.5,
            color: "notahex".to_string(),
            alpha: 1.0,
        };
        let spec = ThemeSpec {
            name: "t".to_string(),
            stops: vec![stop],
            opacity: 1.0,
            effects: no_effects(),
            dark_adaptation: 0.45,
            night_shift_enabled: false,
            ambient_enabled: false,
            version: 1,
        };
        assert_eq!(sanitized(&spec).stops[0].color, "#12282E");
    }

    #[test]
    fn sanitized_trims_and_caps_the_name() {
        let spec = ThemeSpec {
            name: format!("  {}  ", "n".repeat(100)),
            stops: vec![ThemeStop {
                x: 0.5,
                y: 0.5,
                color: "#123456".to_string(),
                alpha: 1.0,
            }],
            opacity: 1.0,
            effects: no_effects(),
            dark_adaptation: 0.45,
            night_shift_enabled: false,
            ambient_enabled: false,
            version: 1,
        };
        assert_eq!(sanitized(&spec).name, "n".repeat(64));
    }

    #[test]
    fn sanitized_preserves_night_shift_ambient() {
        let spec = ThemeSpec {
            name: "t".to_string(),
            stops: vec![ThemeStop {
                x: 0.5,
                y: 0.5,
                color: "#123456".to_string(),
                alpha: 1.0,
            }],
            opacity: 1.0,
            effects: no_effects(),
            dark_adaptation: 0.45,
            night_shift_enabled: true,
            ambient_enabled: true,
            version: 1,
        };
        let s = sanitized(&spec);
        assert!(s.night_shift_enabled);
        assert!(s.ambient_enabled);
    }

    #[test]
    fn sanitized_is_idempotent() {
        let stop = ThemeStop {
            x: 2.0,
            y: -1.0,
            color: "garbage".to_string(),
            alpha: 3.0,
        };
        let spec = ThemeSpec {
            name: "t".to_string(),
            stops: vec![stop; 9],
            opacity: 9.0,
            effects: ThemeEffects {
                grain: 5.0,
                vignette: -2.0,
                frost: 99.0,
                warmth: 7.0,
            },
            dark_adaptation: -3.0,
            night_shift_enabled: true,
            ambient_enabled: true,
            version: 1,
        };
        let once = sanitized(&spec);
        let twice = sanitized(&once);
        assert_eq!(once, twice);
    }

    // ------------------------------------------------------- single-stop

    #[test]
    fn single_stop_theme_wraps_the_given_hex_as_one_full_alpha_stop() {
        let spec = single_stop_theme("#12282E");
        assert_eq!(spec.stops.len(), 1);
        assert_eq!(spec.stops[0].color, "#12282E");
        assert_eq!(spec.stops[0].x, 0.5);
        assert_eq!(spec.stops[0].y, 0.5);
        assert_eq!(spec.stops[0].alpha, 1.0);
        assert_eq!(spec.opacity, 1.0);
        assert_eq!(spec.dark_adaptation, 0.45);
        assert!(!spec.night_shift_enabled);
        assert!(!spec.ambient_enabled);
    }

    #[test]
    fn single_stop_theme_round_trips_through_dominant_tint_hex_when_already_under_the_ceiling() {
        let spec = single_stop_theme("#12282E");
        assert_eq!(dominant_tint_hex(&spec), "#12282E");
    }

    #[test]
    fn single_stop_theme_sanitizes_an_invalid_hex() {
        let spec = single_stop_theme("not-a-color");
        assert_eq!(spec.stops[0].color, "#12282E");
    }

    #[test]
    fn single_stop_theme_resolves_a_named_tint() {
        let spec = single_stop_theme("oceanBlue");
        assert_eq!(spec.stops[0].color, "#0F2B46");
    }

    #[test]
    fn every_macos_accent_derives_its_own_paper_and_ink() {
        let mut papers = std::collections::HashSet::new();
        let mut inks = std::collections::HashSet::new();
        for (index, hex) in MACOS_ACCENTS {
            let tokens = accent_tokens(hex);
            assert!(
                papers.insert(tokens.paper.clone()),
                "{index} repeats a paper"
            );
            assert!(inks.insert(tokens.ink.clone()), "{index} repeats an ink");
        }
    }

    /// Multicolor writes no default, so nothing looks it up and the window's
    /// own reading of `controlAccentColor` - blue - is what gets derived.
    #[test]
    fn multicolor_has_no_index_and_blue_is_what_it_derives() {
        assert_eq!(macos_accent_hex(7), None);
        assert_eq!(macos_accent_hex(4), Some("#007AFF"));
        assert_ne!(accent_tokens("#007AFF"), accent_tokens("#953D96"));
    }

    /// -1 is Graphite, which is grey, so a table read off by one hands every
    /// named accent the wrong colour and only this catches it.
    #[test]
    fn the_index_table_matches_the_appearance_pane() {
        assert_eq!(macos_accent_hex(-1), Some("#8C8C8C"));
        assert_eq!(macos_accent_hex(0), Some("#FF5257"));
        assert_eq!(macos_accent_hex(6), Some("#F74F9E"));
        assert_eq!(MACOS_ACCENTS.len(), 8);
    }

    /// The paper has to stay a paper and the ink an ink whatever the accent,
    /// or a surface paints its text the colour of its own card.
    #[test]
    fn a_paper_stays_pale_and_an_ink_stays_dark() {
        for (index, hex) in MACOS_ACCENTS {
            let tokens = accent_tokens(hex);
            let paper_l = relative_luminance(hex_to_rgb(&tokens.paper));
            assert!(paper_l > 0.75, "{index}'s paper is too dark: {paper_l}");
            let ink: Vec<f64> = tokens
                .ink
                .split(' ')
                .map(|c| c.parse::<f64>().unwrap() / 255.0)
                .collect();
            let ink_l = relative_luminance((ink[0], ink[1], ink[2]));
            assert!(ink_l < 0.04, "{index}'s ink is too pale: {ink_l}");
        }
    }

    #[test]
    fn the_accent_travels_through_unchanged_and_the_soft_shade_lifts() {
        let tokens = accent_tokens("#B03A48");
        assert_eq!(tokens.accent, "#B03A48");
        let (_, _, accent_l) = rgb_to_hsl(hex_to_rgb("#B03A48"));
        let (_, _, soft_l) = rgb_to_hsl(hex_to_rgb(&tokens.soft));
        assert!(soft_l > accent_l, "the soft shade is not lighter");
    }

    /// White on macOS Yellow is unreadable, and every overlay's filled button
    /// inks itself with this.
    #[test]
    fn a_pale_accent_takes_black_ink_and_a_deep_one_white() {
        assert_eq!(accent_tokens("#FFC600").accent_ink, "#000000");
        assert_eq!(accent_tokens("#953D96").accent_ink, "#FFFFFF");
    }
}
