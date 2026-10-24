// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! The space tint palette: a stored tint ref becomes a real colour here.
//!
//! Recovered from `TintPalette.swift`, which was deleted with the Swift
//! package by c0a35ffe and never replaced. The names and their values are
//! product decisions, so they are lifted from the `last-swift` tag rather than
//! re-picked - a tint that shifted would repaint every user's sidebar.
//!
//! Resolution happens HERE, in the core, and not in CSS. `Space.tint` holds a
//! product name (`oceanBlue`) or a `#RRGGBB` string, and a name is not a
//! colour any consumer can paint with. The consumers are not all web: the
//! sidebar's `--selbase` and the space dots are CSS, but the host's backdrop
//! tint is a native ARGB value, so a palette living in a stylesheet would need
//! a second copy for the native side - and two copies of a palette drift, the
//! same failure `sidebar::color_for_host` is written to avoid. The core
//! already produces every other colour the sidebar draws.

/// The seven shipped tints and their sRGB values, in the order
/// `TintPalette.knownTints` listed them.
const ENTRIES: [(&str, &str); 7] = [
    ("atriumSolarized", "#12282E"),
    ("atriumCrimson", "#2E1219"),
    ("atriumGreen", "#122E27"),
    ("oceanBlue", "#0F2B46"),
    ("sunsetOrange", "#5A2E0D"),
    ("plum", "#3A1A42"),
    ("slate", "#25282C"),
];

/// What an unknown ref resolves to. A newer Atrium's tint read by an older
/// build still paints something rather than leaving the surface unstyled.
const FALLBACK: &str = "#12282E";

/// `tint` as `#RRGGBB` - a shipped name, a custom hex, or the fallback.
///
/// A hex ref comes back UPPERCASED, so one stored colour has exactly one
/// encoding on the wire whichever case the file carries.
pub fn resolve_hex(tint: &str) -> String {
    if let Some((_, hex)) = ENTRIES.iter().find(|(name, _)| *name == tint) {
        return (*hex).to_string();
    }
    match parse_hex(tint) {
        Some(hex) => hex,
        None => FALLBACK.to_string(),
    }
}

/// A `#RRGGBB` string uppercased, or `None` for anything else - a missing
/// `#`, the wrong length, or a non-hex digit. Deliberately stricter than
/// Swift's `UInt32(_:radix:)`, which accepts a sign character.
fn parse_hex(tint: &str) -> Option<String> {
    let digits = tint.strip_prefix('#')?;
    if digits.len() != 6 || !digits.bytes().all(|b| b.is_ascii_hexdigit()) {
        return None;
    }
    Some(format!("#{}", digits.to_ascii_uppercase()))
}

/// How far the chrome may drift from the space's own tint toward the active
/// page's colour, as a fraction.
///
/// A space carries one theme so every tab in it looks alike, and this shift is
/// the only signal in the chrome about which page is in front. It is BOUNDED
/// because a space has to keep reading as itself: at 1.0 the page would repaint
/// the window and the space's identity would be gone, which is the thing the
/// tint palette exists to hold.
const MAX_PAGE_TINT_SHIFT: f64 = 0.22;

fn channels(hex: &str) -> Option<(f64, f64, f64)> {
    let s = hex.strip_prefix('#')?;
    // bytes, not chars. The old guard compared s.len() - a BYTE count -
    // against 6 and then sliced &s[a..a + 2], so a six-byte multi-byte string
    // passed the guard and sliced mid-character: two euro signs are 6 bytes and 2
    // chars, and slicing 0..2 inside that panics. A panic in a function reached
    // through extern "C" aborts the process, while atrium_core_page_tint
    // documents the opposite - a page_hex naming no colour leaves the tint alone.
    //
    // as_bytes with an is_ascii_hexdigit check answers both questions at once:
    // six ASCII hex digits is exactly what from_str_radix can take, and every
    // byte being ASCII means no index can land inside a character.
    let bytes = s.as_bytes();
    if bytes.len() != 6 || !bytes.iter().all(u8::is_ascii_hexdigit) {
        return None;
    }
    let part = |a: usize| u8::from_str_radix(&s[a..a + 2], 16).ok().map(f64::from);
    Some((part(0)?, part(2)?, part(4)?))
}

/// The chrome colour for a space tinted `base` showing a page coloured `page`.
///
/// `amount` is clamped to `MAX_PAGE_TINT_SHIFT`, so no caller can exceed the
/// bound by passing a larger one - the bound is the rule, not a default.
///
/// An unresolvable `page` returns `base` unchanged: a page with no theme colour
/// is the common case, not an error, and it must leave the space looking
/// exactly as it does with no page at all.
fn page_shifted_hex(base: &str, page: &str, amount: f64) -> String {
    mix_hex(base, page, amount.clamp(0.0, MAX_PAGE_TINT_SHIFT))
}

/// `base` moved `t` of the way toward `page`, with no bound but the ends.
/// Private because the bound above is the rule every caller outside this file
/// gets, and `page_tint_over_glass` applies its own before it reaches here.
fn mix_hex(base: &str, page: &str, t: f64) -> String {
    let resolved = resolve_hex(base);
    let (Some(b), Some(p)) = (channels(&resolved), channels(page)) else {
        return resolved;
    };
    let t = t.clamp(0.0, 1.0);
    let mix = |from: f64, to: f64| (from + (to - from) * t).round() as u8;
    format!(
        "#{:02X}{:02X}{:02X}",
        mix(b.0, p.0),
        mix(b.1, p.1),
        mix(b.2, p.2)
    )
}

/// The contrast the chrome's own text must keep against the tinted backdrop.
///
/// WCAG AA for large text. The chrome's labels are large and the backdrop is a
/// solid fill behind them, so AA large is the right rung: AAA would refuse
/// almost every page colour and the feature would never be seen.
pub const MIN_CHROME_CONTRAST: f64 = 3.0;

fn relative_luminance(c: (f64, f64, f64)) -> f64 {
    crate::theme_math::relative_luminance((c.0 / 255.0, c.1 / 255.0, c.2 / 255.0))
}

/// The WCAG contrast ratio between two `#RRGGBB` colours, 1.0 to 21.0.
/// `None` for anything that is not one.
///
/// Strict, where `resolve_hex` is forgiving: this answers whether a colour is
/// readable, and resolving a typo to the fallback would answer about a colour
/// the caller never named.
pub fn contrast_ratio(a: &str, b: &str) -> Option<f64> {
    let (a, b) = (channels(a)?, channels(b)?);
    let (la, lb) = (relative_luminance(a), relative_luminance(b));
    let (hi, lo) = if la > lb { (la, lb) } else { (lb, la) };
    Some((hi + 0.05) / (lo + 0.05))
}

/// `page_shifted_hex` with the chrome's text kept readable.
///
/// The drift walks back toward the space's own tint until the ratio against
/// `ink` clears `MIN_CHROME_CONTRAST`. The base itself is the floor: a space
/// whose own theme is already unreadable is that theme's problem, and refusing
/// to paint it here would leave the window with no tint at all.
#[cfg(test)]
fn page_shifted_readable(base: &str, page: &str, amount: f64, ink: &str) -> String {
    match readable_amount(base, page, amount, ink) {
        Some(step) => page_shifted_hex(base, page, step),
        None => page_shifted_hex(base, page, amount),
    }
}

/// The largest amount at or below `amount` whose shift keeps `ink` readable.
/// `None` when `ink` names no colour, which is the caller's fallback signal.
fn readable_amount(base: &str, page: &str, amount: f64, ink: &str) -> Option<f64> {
    let mut step = amount.clamp(0.0, MAX_PAGE_TINT_SHIFT);
    loop {
        match contrast_ratio(&page_shifted_hex(base, page, step), ink) {
            Some(ratio) if ratio >= MIN_CHROME_CONTRAST => return Some(step),
            None => return None,
            _ => {}
        }
        if step <= 0.0 {
            return Some(step);
        }
        step = (step - 0.02).max(0.0);
    }
}

/// The chrome colour to PAINT behind glass of opacity `glass_alpha`. Everything
/// above bounds the VALUE. The bound and the contrast floor grade the COMPOSITED
/// colour here: the drift a user sees, and the surface the chrome's ink sits on.
pub fn page_tint_over_glass(base: &str, page: &str, ink: &str, glass_alpha: f64) -> String {
    let resolved = resolve_hex(base);
    let alpha = glass_alpha.clamp(0.0, 1.0);
    // Glass carrying none of the tint has nothing to drift, and it is the one
    // alpha the division below has no answer for.
    if channels(page).is_none() || alpha <= 0.0 {
        return resolved;
    }
    let seen = readable_amount(base, page, MAX_PAGE_TINT_SHIFT, ink).unwrap_or(MAX_PAGE_TINT_SHIFT);
    // The page's own colour is the ceiling. Glass too thin to carry the bound
    // would need a value past the page, and the user sees `alpha` instead.
    mix_hex(base, page, (seen / alpha).min(1.0))
}

#[cfg(test)]
mod tests {
    use super::*;

    /// WCAG's own worked pair: black on white is 21:1, the maximum.
    #[test]
    fn contrast_runs_from_one_to_twenty_one() {
        assert!((contrast_ratio("#000000", "#FFFFFF").unwrap() - 21.0).abs() < 0.01);
        assert!((contrast_ratio("#FFFFFF", "#FFFFFF").unwrap() - 1.0).abs() < 0.01);
    }

    #[test]
    fn contrast_of_an_unreadable_colour_is_none() {
        assert!(contrast_ratio("not-a-colour", "#FFFFFF").is_none());
    }

    /// The base has to be one the FULL shift actually breaks, or the guard is
    /// never reached and the test passes with the guard deleted. #7A7A7A reads
    /// at 4.29 against white ink and its full shift toward white drops to
    /// 2.92, just under the floor.
    #[test]
    fn a_shift_that_would_bury_the_text_is_pulled_back() {
        let base = "#7A7A7A";
        let full = page_shifted_hex(base, "#FFFFFF", MAX_PAGE_TINT_SHIFT);
        assert!(
            contrast_ratio(&full, "#FFFFFF").unwrap() < MIN_CHROME_CONTRAST,
            "the fixture no longer breaks contrast, so it proves nothing"
        );

        let shifted = page_shifted_readable(base, "#FFFFFF", MAX_PAGE_TINT_SHIFT, "#FFFFFF");
        assert_ne!(shifted, full, "the guard did not pull anything back");
        assert!(
            contrast_ratio(&shifted, "#FFFFFF").unwrap() >= MIN_CHROME_CONTRAST,
            "{shifted} is unreadable against white ink"
        );
    }

    #[test]
    fn a_shift_that_stays_readable_is_left_alone() {
        // A dark page against a dark base never approaches the threshold, so
        // the guard must not quietly cost the feature its drift.
        let base = "#101820";
        let plain = page_shifted_hex(base, "#203040", MAX_PAGE_TINT_SHIFT);
        let guarded = page_shifted_readable(base, "#203040", MAX_PAGE_TINT_SHIFT, "#FFFFFF");
        assert_eq!(guarded, plain);
    }

    #[test]
    fn a_page_with_no_colour_leaves_the_space_alone_even_under_the_guard() {
        let base = "#101820";
        assert_eq!(
            page_shifted_readable(base, "", MAX_PAGE_TINT_SHIFT, "#FFFFFF"),
            resolve_hex(base)
        );
    }

    #[test]
    fn unreadable_ink_falls_back_to_the_bounded_shift() {
        // The ink is the caller's, and a caller that cannot name it must not
        // silently lose the feature. The base is the one the guard DOES act on,
        // so a fallback that quietly ran the guard anyway would differ.
        let base = "#7A7A7A";
        assert_eq!(
            page_shifted_readable(base, "#FFFFFF", MAX_PAGE_TINT_SHIFT, "bogus"),
            page_shifted_hex(base, "#FFFFFF", MAX_PAGE_TINT_SHIFT)
        );
    }

    /// The regression guard on the alpha rework: glass this thick hides
    /// nothing, so the answer that shipped must not move.
    #[test]
    fn glass_that_hides_nothing_gives_the_plain_answer() {
        let base = "#12282E";
        for page in ["#DC143C", "#008080", "#FFFFFF", "#0D1117"] {
            assert_eq!(
                page_tint_over_glass(base, page, "#FFFFFF", 1.0),
                page_shifted_readable(base, page, MAX_PAGE_TINT_SHIFT, "#FFFFFF"),
                "{page} disagrees at full opacity"
            );
        }
    }

    /// A value shifted 22% behind a 55% glass moves the user's pixels by 12%,
    /// so the feature quietly loses half its reach.
    #[test]
    fn the_bound_measures_what_the_user_sees() {
        let base = "#12282E";
        let page = "#008080";
        let painted = page_tint_over_glass(base, page, "#FFFFFF", 0.55);
        assert_eq!(
            perceived(base, &painted, 0.55),
            page_shifted_hex(base, page, MAX_PAGE_TINT_SHIFT),
            "the drift on screen is not the bound"
        );
    }

    /// Reaching 22% on screen through a 20% glass needs a value 110% of the way
    /// to the page, which is past the page. The page's own colour is the ceiling.
    #[test]
    fn a_glass_too_thin_for_the_bound_paints_the_page_colour() {
        let base = "#12282E";
        let page = "#DC143C";
        assert_eq!(page_tint_over_glass(base, page, "#FFFFFF", 0.2), page);
    }

    /// #7A7A7A toward white breaks the floor at full opacity. Behind a thin
    /// glass it never reaches the screen, so grading it would refuse a fine page.
    #[test]
    fn the_floor_grades_the_composited_colour_not_the_painted_one() {
        let base = "#7A7A7A";
        let painted = page_tint_over_glass(base, "#FFFFFF", "#FFFFFF", 0.2);
        assert!(
            contrast_ratio(&painted, "#FFFFFF").unwrap() < MIN_CHROME_CONTRAST,
            "the fixture no longer paints a value the old guard would refuse, \
             so it cannot prove which colour is graded"
        );
        let seen = perceived(base, &painted, 0.2);
        assert!(
            contrast_ratio(&seen, "#FFFFFF").unwrap() >= MIN_CHROME_CONTRAST,
            "{seen} is what the user reads, and it is unreadable"
        );
    }

    #[test]
    fn a_page_with_no_colour_leaves_the_space_alone_behind_any_glass() {
        let base = "#101820";
        for alpha in [0.0, 0.2, 0.55, 1.0] {
            assert_eq!(
                page_tint_over_glass(base, "", "#FFFFFF", alpha),
                resolve_hex(base),
                "a colourless page moved the chrome at alpha {alpha}"
            );
        }
    }

    /// The glass the chrome ships with: 184/255.
    const SHIPPED_GLASS: f64 = 184.0 / 255.0;

    /// Off a light base every shift fails the floor against white, so the walk
    /// ends at zero. Black is the ink the sidebar draws there, and it moves.
    #[test]
    fn a_light_base_answers_the_page_colour_under_the_ink_it_really_uses() {
        let base = "#F0F4F8";
        for page in ["#FF0000", "#C81E3C", "#2020C0"] {
            assert_eq!(
                page_tint_over_glass(base, page, "#FFFFFF", SHIPPED_GLASS),
                resolve_hex(base),
                "{page} drifts against white ink already, so the fixture proves nothing"
            );
            assert_ne!(
                page_tint_over_glass(base, page, "#000000", SHIPPED_GLASS),
                resolve_hex(base),
                "{page} left the chrome on the space tint under dark ink"
            );
        }
    }

    /// The other half: a dark space still drifts under white ink, and what the
    /// user reads still clears the floor.
    #[test]
    fn a_dark_base_still_drifts_under_white_ink() {
        let base = "#12282E";
        for page in ["#FF0000", "#C81E3C", "#2020C0"] {
            let painted = page_tint_over_glass(base, page, "#FFFFFF", SHIPPED_GLASS);
            assert_ne!(painted, resolve_hex(base), "{page} lost its drift");
            let seen = perceived(base, &painted, SHIPPED_GLASS);
            assert!(
                contrast_ratio(&seen, "#FFFFFF").unwrap() >= MIN_CHROME_CONTRAST,
                "{seen} is what the user reads for {page}, and it is unreadable"
            );
        }
    }

    /// Nothing to drift, and no answer to the division by the alpha.
    #[test]
    fn glass_that_carries_nothing_leaves_the_space_alone() {
        assert_eq!(
            page_tint_over_glass("#12282E", "#DC143C", "#FFFFFF", 0.0),
            resolve_hex("#12282E")
        );
    }

    /// The frost behind both colours cancels out of the difference, so this is
    /// exact for the drift even though nothing here knows the desktop.
    fn perceived(base: &str, painted: &str, alpha: f64) -> String {
        let b = channels(&resolve_hex(base)).unwrap();
        let p = channels(painted).unwrap();
        let mix = |from: f64, to: f64| (from + (to - from) * alpha).round() as u8;
        format!(
            "#{:02X}{:02X}{:02X}",
            mix(b.0, p.0),
            mix(b.1, p.1),
            mix(b.2, p.2)
        )
    }

    /// The values are the recovered ones, asserted one by one. A palette that
    /// merely "looks right" repaints every space that uses the tint it got
    /// wrong, and nothing else in the app would notice.
    #[test]
    fn every_shipped_tint_keeps_the_value_it_had_in_swift() {
        assert_eq!(resolve_hex("atriumSolarized"), "#12282E");
        assert_eq!(resolve_hex("atriumCrimson"), "#2E1219");
        assert_eq!(resolve_hex("atriumGreen"), "#122E27");
        assert_eq!(resolve_hex("oceanBlue"), "#0F2B46");
        assert_eq!(resolve_hex("sunsetOrange"), "#5A2E0D");
        assert_eq!(resolve_hex("plum"), "#3A1A42");
        assert_eq!(resolve_hex("slate"), "#25282C");
    }

    /// A custom hex tint is already a colour and passes through, normalised.
    #[test]
    fn a_custom_hex_tint_survives_in_one_casing() {
        assert_eq!(resolve_hex("#5B8DEF"), "#5B8DEF");
        assert_eq!(resolve_hex("#5b8def"), "#5B8DEF");
        assert_eq!(resolve_hex("#000000"), "#000000");
    }

    /// Deletion-safe: an unknown ref paints the fallback rather than handing
    /// the surface a string it cannot use.
    #[test]
    fn an_unknown_ref_falls_back_rather_than_reaching_the_surface_raw() {
        assert_eq!(resolve_hex("mysteryTeal"), FALLBACK);
        assert_eq!(resolve_hex(""), FALLBACK);
        assert_eq!(resolve_hex("#12345"), FALLBACK, "too short");
        assert_eq!(resolve_hex("#12345g"), FALLBACK, "not hex");
        assert_eq!(resolve_hex("5B8DEF"), FALLBACK, "no #");
        assert_eq!(resolve_hex("#+1234"), FALLBACK, "a sign is not a digit");
    }
}

#[cfg(test)]
mod page_tint_tests {
    use super::*;

    #[test]
    fn a_page_colour_pulls_the_chrome_a_bounded_distance_from_the_space_tint() {
        // oceanBlue is #0F2B46 = (15, 43, 70). A white page at the full bound
        // moves each channel 22 percent of the way to 255 and no further:
        //   15 + 240*0.22 = 67.8 -> 68 = 0x44
        //   43 + 212*0.22 = 89.6 -> 90 = 0x5A
        //   70 + 185*0.22 = 110.7 -> 111 = 0x6F
        let shifted = page_shifted_hex("oceanBlue", "#FFFFFF", MAX_PAGE_TINT_SHIFT);
        assert_eq!(shifted, "#445A6F");

        // The space still reads as itself: every channel is nearer its own
        // base than the page's.
        assert_ne!(shifted, "#FFFFFF");
    }

    #[test]
    fn the_bound_cannot_be_exceeded_by_asking_for_more() {
        // The bound is the RULE, not a default a caller can argue with.
        let at = page_shifted_hex("oceanBlue", "#FFFFFF", MAX_PAGE_TINT_SHIFT);
        assert_eq!(page_shifted_hex("oceanBlue", "#FFFFFF", 1.0), at);
        assert_eq!(page_shifted_hex("oceanBlue", "#FFFFFF", 99.0), at);
    }

    #[test]
    fn no_shift_and_a_negative_one_both_leave_the_space_tint_alone() {
        assert_eq!(page_shifted_hex("oceanBlue", "#FFFFFF", 0.0), "#0F2B46");
        assert_eq!(page_shifted_hex("oceanBlue", "#FFFFFF", -1.0), "#0F2B46");
    }

    #[test]
    fn a_page_with_no_colour_leaves_the_chrome_exactly_as_it_was() {
        // The COMMON case, not an error: most pages carry no theme colour, and
        // they must look identical to no page at all.
        assert_eq!(page_shifted_hex("oceanBlue", "", 0.2), "#0F2B46");
        assert_eq!(
            page_shifted_hex("oceanBlue", "not a colour", 0.2),
            "#0F2B46"
        );
        assert_eq!(page_shifted_hex("oceanBlue", "#FFF", 0.2), "#0F2B46");
    }

    #[test]
    fn a_custom_hex_space_tint_shifts_the_same_way_a_named_one_does() {
        // resolve_hex runs first, so a stored #RRGGBB and a palette name reach
        // the blend identically.
        assert_eq!(
            page_shifted_hex("#0F2B46", "#FFFFFF", MAX_PAGE_TINT_SHIFT),
            page_shifted_hex("oceanBlue", "#FFFFFF", MAX_PAGE_TINT_SHIFT)
        );
    }

    /// len() counts BYTES, so a six-byte multi-byte string passed the
    /// length guard and then sliced mid-character. A panic inside an extern "C"
    /// function aborts the process, and atrium_core_page_tint documents the
    /// opposite - a page_hex naming no colour returns the tint unchanged.
    #[test]
    fn a_six_byte_multibyte_string_is_refused_rather_than_panicking() {
        // "€€" is 6 bytes and 2 chars. &s[0..2] inside it is not a char boundary.
        for hex in ["#\u{20AC}\u{20AC}", "#\u{00E9}\u{00E9}\u{00E9}"] {
            assert_eq!(
                page_shifted_hex("#0F2B46", hex, MAX_PAGE_TINT_SHIFT),
                "#0F2B46",
                "{hex} did not leave the tint unchanged"
            );
        }
    }

    #[test]
    fn non_hex_ascii_of_the_right_length_is_refused() {
        // Six ASCII bytes that are not hex digits: the length guard passes, the
        // radix parse is what has to reject them.
        assert_eq!(
            page_shifted_hex("#0F2B46", "#zzzzzz", MAX_PAGE_TINT_SHIFT),
            "#0F2B46"
        );
    }

    #[test]
    fn a_real_six_digit_hex_still_shifts() {
        // The other direction: a guard that refused everything would pass both
        // tests above and turn the whole page-tint feature off.
        assert_ne!(
            page_shifted_hex("#0F2B46", "#FFFFFF", MAX_PAGE_TINT_SHIFT),
            "#0F2B46"
        );
    }
}
