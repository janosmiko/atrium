// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! The user's search engine list, seeded once with a starter set.

use serde::{Deserialize, Serialize};

/// A missing key drops its engine through `valid()`, not the whole file.
#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase", default)]
pub struct SearchEngine {
    pub id: String,
    pub name: String,
    pub keyword: String,
    pub url: String,
    pub enabled: bool,
    pub builtin: bool,
}

/// Bumped when a starter is added. A file at an older seed gets only the new
/// ids appended, so a starter the user deleted stays deleted.
pub const STARTER_SEED: u32 = 1;

/// A starter row: id, name, keyword, URL, and the seed it arrived in.
type Starter = (&'static str, &'static str, &'static str, &'static str, u32);

const STARTERS: &[Starter] = &[
    (
        "google",
        "Google",
        "google.com",
        "https://www.google.com/search?q=%s",
        1,
    ),
    (
        "duckduckgo",
        "DuckDuckGo",
        "duckduckgo.com",
        "https://duckduckgo.com/?q=%s",
        1,
    ),
    (
        "kagi",
        "Kagi",
        "kagi.com",
        "https://kagi.com/search?q=%s",
        1,
    ),
    (
        "bing",
        "Bing",
        "bing.com",
        "https://www.bing.com/search?q=%s",
        1,
    ),
    (
        "ecosia",
        "Ecosia",
        "ecosia.org",
        "https://www.ecosia.org/search?q=%s",
        1,
    ),
    (
        "youtube",
        "YouTube",
        "youtube.com",
        "https://www.youtube.com/results?search_query=%s",
        1,
    ),
    (
        "github",
        "GitHub",
        "github.com",
        "https://github.com/search?q=%s",
        1,
    ),
    (
        "reddit",
        "Reddit",
        "reddit.com",
        "https://www.reddit.com/search/?q=%s",
        1,
    ),
    (
        "wikipedia",
        "Wikipedia",
        "wikipedia.org",
        "https://en.wikipedia.org/w/index.php?search=%s",
        1,
    ),
    (
        "amazon",
        "Amazon",
        "amazon.com",
        "https://www.amazon.com/s?k=%s",
        1,
    ),
    (
        "gmaps",
        "Google Maps",
        "maps.google.com",
        "https://www.google.com/maps/search/%s",
        1,
    ),
    (
        "gimages",
        "Google Images",
        "images.google.com",
        "https://www.google.com/search?tbm=isch&q=%s",
        1,
    ),
    (
        "perplexity",
        "Perplexity",
        "perplexity.ai",
        "https://www.perplexity.ai/search?q=%s",
        1,
    ),
    (
        "chatgpt",
        "ChatGPT",
        "chatgpt.com",
        "https://chatgpt.com/?q=%s",
        1,
    ),
    (
        "claude",
        "Claude",
        "claude.ai",
        "https://claude.ai/new?q=%s",
        1,
    ),
    ("x", "X", "x.com", "https://x.com/search?q=%s", 1),
    (
        "stackoverflow",
        "Stack Overflow",
        "stackoverflow.com",
        "https://stackoverflow.com/search?q=%s",
        1,
    ),
    (
        "npm",
        "npm",
        "npmjs.com",
        "https://www.npmjs.com/search?q=%s",
        1,
    ),
    (
        "crates",
        "crates.io",
        "crates.io",
        "https://crates.io/search?q=%s",
        1,
    ),
];

/// Which seed a starter arrived in. 0 for an id the table does not carry, so
/// an unknown id is never appended.
fn seed_of(table: &[Starter], id: &str) -> u32 {
    table.iter().find(|s| s.0 == id).map_or(0, |s| s.4)
}

fn top_seed(table: &[Starter]) -> u32 {
    table.iter().map(|s| s.4).max().unwrap_or(0)
}

fn starters_from(table: &[Starter]) -> Vec<SearchEngine> {
    table
        .iter()
        .map(|(id, name, keyword, url, _)| SearchEngine {
            id: (*id).into(),
            name: (*name).into(),
            keyword: (*keyword).into(),
            url: (*url).into(),
            enabled: true,
            builtin: true,
        })
        .collect()
}

pub fn starters() -> Vec<SearchEngine> {
    starters_from(STARTERS)
}

/// The id shape a space's `searchEngine` may name, so an id no space can
/// select never enters the list: 1-64 lowercase alphanumerics or hyphens,
/// starting with a letter or digit.
pub(crate) fn engine_id_ok(v: &str) -> bool {
    let mut chars = v.chars();
    match chars.next() {
        Some(c) if c.is_ascii_lowercase() || c.is_ascii_digit() => {}
        _ => return false,
    }
    v.len() <= 64 && chars.all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == '-')
}

/// The scheme and bare host, so a loopback check compares the host alone
/// rather than pattern-matching the whole url string.
pub(crate) fn scheme_and_host(url: &str) -> Option<(&str, &str)> {
    let (scheme, rest) = url.split_once("://")?;
    let authority_end = rest.find(['/', '?', '#']).unwrap_or(rest.len());
    let authority = &rest[..authority_end];
    let authority = authority.rsplit_once('@').map_or(authority, |(_, h)| h);
    let host = if let Some(bracketed) = authority.strip_prefix('[') {
        bracketed.split(']').next().unwrap_or(bracketed)
    } else {
        authority.split(':').next().unwrap_or(authority)
    };
    Some((scheme, host))
}

/// `127.0.0.1`, `::1` and `localhost` only, not a host that merely contains
/// one of those strings (127.0.0.1.evil.com, localhost.attacker.net).
fn is_loopback_host(host: &str) -> bool {
    host == "127.0.0.1" || host == "::1" || host.eq_ignore_ascii_case("localhost")
}

/// https is always allowed. Plain http is allowed only to a loopback host, so
/// a scenario can point an engine at the VM's own fixture server.
fn allowed_engine_url(url: &str) -> bool {
    // Case-insensitive because GURL canonicalises a scheme to lower case on the
    // C++ side, and the two must answer alike or the page keeps an engine the
    // store dropped.
    let Some((scheme, host)) = scheme_and_host(url) else {
        return false;
    };
    if scheme.eq_ignore_ascii_case("https") {
        return true;
    }
    scheme.eq_ignore_ascii_case("http") && is_loopback_host(host)
}

/// https, or http to a loopback host, with exactly one `%s`.
pub(crate) fn usable_url(url: &str) -> bool {
    allowed_engine_url(url) && url.matches("%s").count() == 1
}

/// Trims the keyword in place, since the trimmed value is what the loader
/// stores and what a keyword collision is judged on.
pub(crate) fn valid(e: &mut SearchEngine) -> bool {
    e.keyword = e.keyword.trim().to_string();
    engine_id_ok(&e.id)
        && !e.name.trim().is_empty()
        && !e.keyword.is_empty()
        && usable_url(&e.url)
}

/// Drops what the loader cannot use, lower-cases keywords, keeps the first of
/// two enabled engines on one keyword, and appends the starters a file at an
/// older seed has never seen.
pub fn normalize(list: Vec<SearchEngine>, seed: u32) -> (Vec<SearchEngine>, u32) {
    normalize_with(STARTERS, list, seed)
}

fn normalize_with(
    table: &[Starter],
    list: Vec<SearchEngine>,
    seed: u32,
) -> (Vec<SearchEngine>, u32) {
    let mut out: Vec<SearchEngine> = Vec::with_capacity(list.len());
    for mut e in list {
        e.keyword = e.keyword.to_lowercase();
        if !valid(&mut e) {
            continue;
        }
        let taken = e.enabled && out.iter().any(|o| o.enabled && o.keyword == e.keyword);
        if taken || out.iter().any(|o| o.id == e.id) {
            continue;
        }
        out.push(e);
    }
    for s in starters_from(table) {
        if seed_of(table, &s.id) > seed && !out.iter().any(|o| o.id == s.id) {
            out.push(s);
        }
    }
    (out, top_seed(table).max(seed))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn engine(id: &str, keyword: &str, url: &str) -> SearchEngine {
        SearchEngine {
            id: id.into(),
            name: id.into(),
            keyword: keyword.into(),
            url: url.into(),
            enabled: true,
            builtin: false,
        }
    }

    #[test]
    fn a_fresh_file_gets_every_starter_and_seed_one() {
        let (list, seed) = normalize(Vec::new(), 0);
        assert_eq!(seed, STARTER_SEED);
        assert_eq!(list.len(), starters().len());
        assert!(
            list.iter()
                .any(|e| e.id == "youtube" && e.builtin && e.enabled)
        );
    }

    #[test]
    fn a_seeded_file_is_not_reseeded() {
        let mut list = starters();
        list.retain(|e| e.id != "reddit");
        let (out, seed) = normalize(list, STARTER_SEED);
        assert_eq!(seed, STARTER_SEED);
        assert!(
            !out.iter().any(|e| e.id == "reddit"),
            "a deleted builtin stays gone"
        );
    }

    #[test]
    fn a_user_engine_survives_beside_the_starters() {
        let mut list = starters();
        list.push(engine(
            "u1",
            "docs.rs",
            "https://docs.rs/releases/search?query=%s",
        ));
        let (out, _) = normalize(list, STARTER_SEED);
        assert!(out.iter().any(|e| e.id == "u1"));
    }

    #[test]
    fn invalid_engines_are_dropped_not_refused() {
        let bad = vec![
            engine("a", "", "https://a.example/?q=%s"),
            engine("b", "b.example", "http://b.example/?q=%s"),
            engine("c", "c.example", "https://c.example/?q="),
            engine("d", "d.example", "https://d.example/?q=%s&r=%s"),
            engine("", "e.example", "https://e.example/?q=%s"),
        ];
        let (out, _) = normalize(bad, STARTER_SEED);
        assert!(out.is_empty());
    }

    #[test]
    fn loopback_http_is_allowed_nothing_else() {
        let list = vec![
            engine("https", "h1", "https://example.com/?q=%s"),
            engine("loop4", "h2", "http://127.0.0.1:8000/e.html?q=%s"),
            engine("loop6", "h3", "http://[::1]:8000/e.html?q=%s"),
            engine("loopname", "h4", "http://localhost:8000/e.html?q=%s"),
            engine("trap4", "h5", "http://127.0.0.1.evil.com/?q=%s"),
            engine("trapname", "h6", "http://localhost.attacker.net/?q=%s"),
            engine("plainhttp", "h7", "http://example.com/?q=%s"),
            engine("file", "h8", "file:///tmp/e.html?q=%s"),
            // GURL lower-cases a scheme, so the C++ port accepts these two. A
            // literal match here would drop them and leave the two sides
            // disagreeing about which engines exist.
            engine("upper", "h9", "HTTPS://example.com/?q=%s"),
            engine("upperloop", "h10", "HTTP://127.0.0.1:8000/e.html?q=%s"),
            // Userinfo is not the host: this one really does reach evil.com.
            engine("userinfo", "h11", "http://localhost@evil.com/?q=%s"),
            // The reverse: the real host after the last '@' is loopback, and
            // "evil.com" is only a (fake) username, so this one is legitimate.
            engine(
                "userinfo-loopback",
                "h13",
                "http://evil.com@127.0.0.1/?q=%s",
            ),
            engine("twoterms", "h12", "http://127.0.0.1:8000/a?q=%s&x=%s"),
        ];
        let (out, _) = normalize(list, STARTER_SEED);
        assert_eq!(
            out.iter().map(|e| e.id.as_str()).collect::<Vec<_>>(),
            [
                "https",
                "loop4",
                "loop6",
                "loopname",
                "upper",
                "upperloop",
                "userinfo-loopback"
            ]
        );
    }

    #[test]
    fn a_duplicate_enabled_keyword_keeps_the_first() {
        let list = vec![
            engine("one", "same.example", "https://one.example/?q=%s"),
            engine("two", "same.example", "https://two.example/?q=%s"),
        ];
        let (out, _) = normalize(list, STARTER_SEED);
        assert_eq!(
            out.iter().map(|e| e.id.as_str()).collect::<Vec<_>>(),
            ["one"]
        );
    }

    #[test]
    fn a_disabled_engine_may_share_a_keyword() {
        let mut list = vec![engine("one", "same.example", "https://one.example/?q=%s")];
        let mut two = engine("two", "same.example", "https://two.example/?q=%s");
        two.enabled = false;
        list.push(two);
        let (out, _) = normalize(list, STARTER_SEED);
        assert_eq!(out.len(), 2);
    }

    #[test]
    fn a_keyword_is_stored_trimmed() {
        let (out, _) = normalize(
            vec![engine("x", "  docs.rs  ", "https://docs.rs/?q=%s")],
            STARTER_SEED,
        );
        assert_eq!(out[0].keyword, "docs.rs");
    }

    #[test]
    fn an_id_no_space_can_select_is_dropped() {
        let bad = vec![
            engine("Upper", "a.example", "https://a.example/?q=%s"),
            engine("has space", "b.example", "https://b.example/?q=%s"),
            engine("-leading", "c.example", "https://c.example/?q=%s"),
            engine(&"x".repeat(65), "d.example", "https://d.example/?q=%s"),
        ];
        let (out, _) = normalize(bad, STARTER_SEED);
        assert!(out.is_empty());
    }

    const FAKE: &[Starter] = &[
        ("one", "One", "one.example", "https://one.example/?q=%s", 1),
        ("two", "Two", "two.example", "https://two.example/?q=%s", 2),
    ];

    #[test]
    fn a_second_generation_appends_only_its_own_ids() {
        let (out, seed) = normalize_with(FAKE, Vec::new(), 1);
        assert_eq!(seed, 2);
        assert_eq!(
            out.iter().map(|e| e.id.as_str()).collect::<Vec<_>>(),
            ["two"],
            "the deleted seed-1 id stays gone, the seed-2 id arrives"
        );
    }

    #[test]
    fn the_starter_seed_matches_the_table() {
        assert_eq!(STARTER_SEED, top_seed(STARTERS));
    }

    #[test]
    fn keywords_are_lower_cased() {
        let (out, _) = normalize(
            vec![engine("x", "Docs.RS", "https://docs.rs/?q=%s")],
            STARTER_SEED,
        );
        assert_eq!(out[0].keyword, "docs.rs");
    }
}
