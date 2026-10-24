// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! The icon links a page names in its head, for a favourite whose origin has
//! no /favicon.ico. A scanner, not an HTML parser: it reads link and base
//! tags and nothing else, and the bytes came from a remote host.
//!
//! Known gaps, each a wrong or missing icon, never a crash: script and style
//! bodies are not skipped, an unbalanced quote in a bare value swallows the
//! rest of the tag, and `</ head>` with a space misses the head stop.

use serde::Serialize;

/// Enough for any head. A page longer than this is cut, not refused.
const MAX_SCAN_BYTES: usize = 256 * 1024;
const MAX_CANDIDATES: usize = 8;

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
struct IconLink {
    pub href: String,
    pub rel: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub sizes: Option<String>,
    #[serde(rename = "type", skip_serializing_if = "Option::is_none")]
    pub mime: Option<String>,
}

#[derive(Debug, Default, Clone, PartialEq, Eq, Serialize)]
struct IconLinks {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub base: Option<String>,
    pub icons: Vec<IconLink>,
}

fn icon_links(html: &[u8]) -> IconLinks {
    let cut = &html[..html.len().min(MAX_SCAN_BYTES)];
    let text = String::from_utf8_lossy(cut);
    let mut out = IconLinks::default();
    let mut rest: &str = &text;
    while let Some(lt) = rest.find('<') {
        let tail = &rest[lt..];
        if tail.starts_with("<!--") {
            match tail.find("-->") {
                Some(end) => rest = &tail[end + 3..],
                None => break,
            }
            continue;
        }
        let closing = tail[1..].starts_with('/');
        let name_start = if closing { 2 } else { 1 };
        let name_end = tail[name_start..]
            .find(|c: char| c.is_whitespace() || c == '>' || c == '/')
            .map(|i| i + name_start)
            .unwrap_or(tail.len());
        let name = tail[name_start..name_end].to_ascii_lowercase();
        if (closing && name == "head") || (!closing && name == "body") {
            break;
        }
        let tag_end = find_tag_end(tail);
        let attrs = &tail[name_end..tag_end];
        if !closing && name == "link" {
            if let Some(link) = link_from(attrs) {
                out.icons.push(link);
            }
        } else if !closing && name == "base" && out.base.is_none() {
            out.base = attribute(attrs, "href");
        }
        rest = &tail[tag_end.min(tail.len() - 1) + 1..];
    }
    let mut seen = std::collections::HashSet::new();
    out.icons.retain(|link| seen.insert(link.href.clone()));
    out.icons.sort_by_key(rank);
    out.icons.truncate(MAX_CANDIDATES);
    out
}

/// Index of a tag's closing `>`, skipping one inside a quoted attribute value.
fn find_tag_end(tail: &str) -> usize {
    let mut quote: Option<u8> = None;
    for (i, b) in tail.bytes().enumerate() {
        match quote {
            Some(q) if b == q => quote = None,
            Some(_) => {}
            None if b == b'"' || b == b'\'' => quote = Some(b),
            None if b == b'>' => return i,
            None => {}
        }
    }
    tail.len()
}

pub fn icon_links_json(html: &[u8]) -> String {
    serde_json::to_string(&icon_links(html)).unwrap_or_else(|_| "{\"icons\":[]}".to_string())
}

fn link_from(attrs: &str) -> Option<IconLink> {
    let map = parse_attrs(attrs);
    let rel = map.get("rel")?.to_ascii_lowercase();
    let tokens: Vec<&str> = rel.split_whitespace().collect();
    let is_icon = tokens
        .iter()
        .any(|t| *t == "icon" || *t == "apple-touch-icon" || *t == "apple-touch-icon-precomposed");
    if !is_icon {
        return None;
    }
    let href = map.get("href")?.clone();
    if href.is_empty() {
        return None;
    }
    Some(IconLink {
        href,
        rel: tokens.join(" "),
        sizes: map.get("sizes").cloned(),
        mime: map.get("type").map(|t| t.to_ascii_lowercase()),
    })
}

/// The value of `name` in a tag's attribute text, quoted or bare.
fn attribute(attrs: &str, name: &str) -> Option<String> {
    parse_attrs(attrs).remove(name)
}

/// Tokenizes a tag's attribute text into `name=value` pairs in one pass. This
/// stays quote-aware so a `name=` inside another attribute's quoted value is
/// never read as a second attribute.
///
/// `attrs` excludes the tag's closing `>`, so a self-closing tag's own `/`
/// lands as the last byte of a bare trailing value. That byte is dropped,
/// unless doing so would empty the value - `href=/` must still mean `/`.
fn parse_attrs(attrs: &str) -> std::collections::HashMap<String, String> {
    let self_closing = attrs.ends_with('/');
    let mut out = std::collections::HashMap::new();
    let bytes = attrs.as_bytes();
    let mut i = 0;
    while i < bytes.len() {
        while i < bytes.len() && (bytes[i].is_ascii_whitespace() || bytes[i] == b'/') {
            i += 1;
        }
        let name_start = i;
        while i < bytes.len()
            && !bytes[i].is_ascii_whitespace()
            && bytes[i] != b'='
            && bytes[i] != b'/'
        {
            i += 1;
        }
        if i == name_start {
            break;
        }
        let name = attrs[name_start..i].to_ascii_lowercase();
        let mut j = i;
        while j < bytes.len() && bytes[j].is_ascii_whitespace() {
            j += 1;
        }
        if j >= bytes.len() || bytes[j] != b'=' {
            i = j;
            continue;
        }
        j += 1;
        while j < bytes.len() && bytes[j].is_ascii_whitespace() {
            j += 1;
        }
        let (value, next) = if j < bytes.len() && (bytes[j] == b'"' || bytes[j] == b'\'') {
            let quote = bytes[j] as char;
            let value_start = j + 1;
            match attrs[value_start..].find(quote) {
                Some(off) => (
                    attrs[value_start..value_start + off].to_string(),
                    value_start + off + 1,
                ),
                None => (attrs[value_start..].to_string(), attrs.len()),
            }
        } else {
            let value_start = j;
            let end = attrs[value_start..]
                .find(|c: char| c.is_whitespace())
                .map(|off| value_start + off)
                .unwrap_or(attrs.len());
            let mut value = attrs[value_start..end].to_string();
            if self_closing && end == attrs.len() && value.len() > 1 {
                value.pop();
            }
            (value, end)
        };
        out.entry(name).or_insert(value);
        i = next;
    }
    out
}

/// Lower ranks first. The decoder reads no SVG, so those go last.
fn rank(link: &IconLink) -> (u8, u32) {
    let svg = link.mime.as_deref() == Some("image/svg+xml")
        || link.href.to_ascii_lowercase().ends_with(".svg");
    let apple = link.rel.contains("apple-touch-icon");
    let tier = match (svg, apple) {
        (true, _) => 2,
        (false, true) => 1,
        (false, false) => 0,
    };
    (tier, size_distance(link.sizes.as_deref()))
}

/// Distance from 32 px of the nearest declared size. `any` or none count as 32.
fn size_distance(sizes: Option<&str>) -> u32 {
    let Some(sizes) = sizes else { return 0 };
    let mut best = u32::MAX;
    for token in sizes.split_whitespace() {
        if token.eq_ignore_ascii_case("any") {
            return 0;
        }
        if let Some(width) = token
            .split(['x', 'X'])
            .next()
            .and_then(|w| w.parse::<u32>().ok())
        {
            best = best.min(width.abs_diff(32));
        }
    }
    if best == u32::MAX { 0 } else { best }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn hrefs(html: &str) -> Vec<String> {
        icon_links(html.as_bytes())
            .icons
            .into_iter()
            .map(|i| i.href)
            .collect()
    }

    #[test]
    fn clockify_shape_keeps_one_relative_icon_first() {
        let html = r#"<html><head>
<link rel="apple-touch-icon" href="assets/favicons/apple-touch-icon.webp">
<link id="favicon-shortcut" rel="shortcut icon" href="assets/favicons/favicon2.ico">
<link id="favicon" rel="icon" type="image/x-icon" href="assets/favicons/favicon2.ico">
</head><body></body></html>"#;
        assert_eq!(
            hrefs(html),
            vec![
                "assets/favicons/favicon2.ico".to_string(),
                "assets/favicons/apple-touch-icon.webp".to_string()
            ]
        );
    }

    #[test]
    fn mavenlink_shape_keeps_an_absolute_href_and_its_type() {
        let html = r#"<head><link rel="shortcut icon" type="image/x-icon" href="https://cdn.test/a/23dca2_default" sizes="any" /></head>"#;
        let links = icon_links(html.as_bytes());
        assert_eq!(links.icons.len(), 1);
        assert_eq!(links.icons[0].href, "https://cdn.test/a/23dca2_default");
        assert_eq!(links.icons[0].mime.as_deref(), Some("image/x-icon"));
        assert_eq!(links.icons[0].sizes.as_deref(), Some("any"));
    }

    #[test]
    fn a_quoted_value_naming_another_attribute_is_not_forged() {
        let html = r#"<head><link title=" href=/evil" rel="icon" href="/real.png"></head>"#;
        assert_eq!(hrefs(html), vec!["/real.png".to_string()]);
    }

    #[test]
    fn a_quoted_href_containing_rel_text_is_still_the_href() {
        let html = r#"<head><link href="a rel=stylesheet b" rel="icon" other="x"></head>"#;
        assert_eq!(hrefs(html), vec!["a rel=stylesheet b".to_string()]);
    }

    #[test]
    fn a_bare_href_of_a_single_slash_keeps_the_slash() {
        let html = r#"<head><link rel="icon" href=/></head>"#;
        assert_eq!(hrefs(html), vec!["/".to_string()]);
    }

    #[test]
    fn a_self_closing_tags_bare_href_drops_only_the_self_close_slash() {
        let html = r#"<head><link rel="icon" href=https://a.test//></head>"#;
        assert_eq!(hrefs(html), vec!["https://a.test/".to_string()]);
    }

    #[test]
    fn bare_and_single_quoted_attributes_parse() {
        let html =
            "<head><LINK REL=icon HREF=/i.png><link rel='shortcut icon' href='/j.ico'></head>";
        assert_eq!(
            hrefs(html),
            vec!["/i.png".to_string(), "/j.ico".to_string()]
        );
    }

    #[test]
    fn base_href_is_reported() {
        let html =
            r#"<head><base href="https://example.test/app/"><link rel="icon" href="i.png"></head>"#;
        assert_eq!(
            icon_links(html.as_bytes()).base.as_deref(),
            Some("https://example.test/app/")
        );
    }

    #[test]
    fn a_commented_out_link_does_not_count() {
        let html = r#"<head><!-- <link rel="icon" href="/old.ico"> --><link rel="icon" href="/new.ico"></head>"#;
        assert_eq!(hrefs(html), vec!["/new.ico".to_string()]);
    }

    #[test]
    fn mask_icon_and_stylesheet_do_not_count() {
        let html = r#"<head><link rel="mask-icon" href="/m.svg"><link rel="stylesheet" href="/s.css"><link rel="icon" href="/i.png"></head>"#;
        assert_eq!(hrefs(html), vec!["/i.png".to_string()]);
    }

    #[test]
    fn svg_ranks_last_and_sizes_nearest_32_first() {
        let html = r#"<head>
<link rel="icon" type="image/svg+xml" href="/i.svg">
<link rel="icon" sizes="192x192" href="/i192.png">
<link rel="icon" sizes="32x32" href="/i32.png">
<link rel="icon" sizes="16x16" href="/i16.png">
</head>"#;
        assert_eq!(
            hrefs(html),
            vec![
                "/i32.png".to_string(),
                "/i16.png".to_string(),
                "/i192.png".to_string(),
                "/i.svg".to_string()
            ]
        );
    }

    #[test]
    fn scanning_stops_at_body_and_at_the_cap() {
        let html = r#"<head><link rel="icon" href="/head.ico"></head><body><link rel="icon" href="/body.ico"></body>"#;
        assert_eq!(hrefs(html), vec!["/head.ico".to_string()]);
        let mut long = String::from("<head>");
        long.push_str(&" ".repeat(MAX_SCAN_BYTES));
        long.push_str(r#"<link rel="icon" href="/late.ico"></head>"#);
        assert!(hrefs(&long).is_empty());
    }

    #[test]
    fn closing_head_tag_stops_the_scan_without_a_body_tag() {
        let html =
            r#"<head><link rel="icon" href="/head.ico"></head><link rel="icon" href="/after.ico">"#;
        assert_eq!(hrefs(html), vec!["/head.ico".to_string()]);
    }

    #[test]
    fn a_quoted_greater_than_does_not_truncate_the_tag() {
        let html = r#"<head><link rel="icon" href="a>b.png"></head>"#;
        assert_eq!(hrefs(html), vec!["a>b.png".to_string()]);
    }

    #[test]
    fn no_head_and_no_links_gives_nothing() {
        assert!(icon_links(b"<html><body>hi</body></html>").icons.is_empty());
        assert!(icon_links(b"").icons.is_empty());
        assert!(icon_links(&[0xff, 0xfe, b'<', b'l']).icons.is_empty());
    }

    #[test]
    fn at_most_eight_candidates_come_back() {
        let mut html = String::from("<head>");
        for i in 0..12 {
            html.push_str(&format!(r#"<link rel="icon" href="/i{i}.png">"#));
        }
        assert_eq!(icon_links(html.as_bytes()).icons.len(), MAX_CANDIDATES);
    }

    #[test]
    fn json_shape_matches_the_header() {
        let html = r#"<head><base href="/b/"><link rel="icon" sizes="32x32" type="image/png" href="i.png"></head>"#;
        let json = icon_links_json(html.as_bytes());
        assert_eq!(
            json,
            r#"{"base":"/b/","icons":[{"href":"i.png","rel":"icon","sizes":"32x32","type":"image/png"}]}"#
        );
    }
}
