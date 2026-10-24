// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! Strips credential-bearing parameters from a URL's query and fragment, and
//! a plaintext password from its userinfo, before it is persisted.
//!
//! Ported from `URLCredentialRedactor.swift`. `spaces.json` is 0600 but still
//! plaintext, and an archived tab keeps its URL for up to 30 days.
//!
//! Purely string-based, deliberately. Rebuilding a URL through a parser
//! re-encodes every value, which silently rewrites URLs that had nothing to
//! redact. Working on raw substrings means every unmatched pair, and the whole
//! URL when nothing matches, comes back byte-identical.
//!
//! OUT OF SCOPE, on purpose, not by omission: path segments (a token as
//! `/path/TOKEN123/` is never inspected - there is no scheme-agnostic way to
//! tell a credential segment from a resource id), every non-http(s) scheme,
//! and a name behind a malformed percent-escape.
//!
//! NOTE ON WHERE THIS RUNS. The WRITE path only, on a copy made inside
//! `persist::save_document`, so the in-memory model keeps the real URL for
//! navigation and autofill. See `docs/model-rules.md` R7.4 - and note that
//! nothing downstream of the model redacts, so a producer fed state that was
//! never persisted still holds the password.

/// Exact parameter-NAME matches after percent-decoding, lowercasing, and
/// normalising `-` to `_` (see `should_redact`). A name merely containing
/// one of these, e.g. `monotoken`, is left alone. Query and fragment share
/// the list, since OAuth's implicit flow puts `access_token` in the fragment.
const CREDENTIAL_PARAM_NAMES: &[&str] = &[
    "token",
    "access_token",
    "auth",
    "authorization",
    "code",
    "id_token",
    "session",
    "session_id",
    "sessionid",
    "api_key",
    "apikey",
    "subscription_key",
    "secret",
    "password",
    "key",
    "jwt",
    "bearer",
    "refresh_token",
    "otp",
    "one_time_token",
    "sso",
    "saml",
    "ticket",
    "oauth_token",
    "oauth_verifier",
    "auth_token",
    // Presigned-URL family: S3/GCS query signing, AWS SigV4
    // (`x_amz_*`), Azure SAS, and OAuth client-credentials.
    "sig",
    "signature",
    "x_amz_signature",
    "x_amz_credential",
    "x_amz_security_token",
    "sas",
    "client_secret",
];

/// `code` is dropped ONLY alongside a `state` param in the same query or
/// fragment - the OAuth authorization-response shape. A lone `?code=SAVE20` is
/// a promo code, which is far more common in ordinary browsing than a bare
/// grant, and it survives. `state` itself is never dropped, so the scoping
/// still holds if the URL is redacted twice.
///
/// `key` is dropped unconditionally. A Maps embed's `?key=` is an API key, not
/// a user secret, so that embed stops rendering on restore - accepted, because
/// the same name is also the most common name for a real secret.
fn should_redact(name: &str, has_state: bool) -> bool {
    if name == "code" {
        return has_state;
    }
    let normalized = name.replace('-', "_");
    CREDENTIAL_PARAM_NAMES.contains(&normalized.as_str())
}

/// Whether `redact` would change `url`. A custom endpoint URL has no live
/// in-memory copy the way a tab does - `atrium_browser_window.cc` rereads
/// `settings.json` fresh per request - so this refuses the value up front
/// instead of redacting it after the fact and silently breaking the endpoint.
pub fn carries_credential(url: &str) -> bool {
    redact(url) != url
}

/// Returns `url` unchanged when it is not http(s), or when nothing in it
/// matches. Otherwise drops every matching `name=value` pair and strips
/// `user:PASSWORD@` from the authority.
pub fn redact(url: &str) -> String {
    let Some(scheme_end) = url.find("://") else {
        return url.to_string();
    };
    let scheme = url[..scheme_end].to_ascii_lowercase();
    if scheme != "http" && scheme != "https" {
        return url.to_string();
    }
    if !authority_parses(&url[scheme_end + 3..]) {
        return url.to_string();
    }

    let (before_fragment, fragment) = match url.find('#') {
        Some(i) => (&url[..i], Some(&url[i + 1..])),
        None => (url, None),
    };
    let (raw_base, query) = match before_fragment.find('?') {
        Some(i) => (&before_fragment[..i], Some(&before_fragment[i + 1..])),
        None => (before_fragment, None),
    };

    let (base, userinfo_changed) = strip_userinfo_password(raw_base);
    let redacted_query = query.map(redact_param_string);
    let redacted_fragment = fragment.map(redact_param_string);

    let query_changed = redacted_query.as_ref().is_some_and(|r| r.1);
    let fragment_changed = redacted_fragment.as_ref().is_some_and(|r| r.1);
    if !query_changed && !fragment_changed && !userinfo_changed {
        return url.to_string();
    }

    let mut out = base;
    // Redaction can empty a query out entirely - a URL made of nothing but
    // credential params - so the separator goes with it rather than dangling.
    if let Some((value, changed)) = redacted_query {
        if !changed || !value.is_empty() {
            out.push('?');
            out.push_str(&value);
        }
    }
    if let Some((value, changed)) = redacted_fragment {
        if !changed || !value.is_empty() {
            out.push('#');
            out.push_str(&value);
        }
    }
    out
}

/// The single funnel every persisted URL passes through before it hits disk
/// (R7.4) - every collection in the document that carries one, Doorways
/// included.
///
/// Returns a COPY. The caller's document keeps the real URLs, because live
/// navigation and autofill need them; only what is encoded from here is
/// stripped. Redaction is idempotent, so a document loaded from an
/// already-redacted file comes back byte for byte.
pub fn redact_document(doc: &crate::spaces::Document) -> crate::spaces::Document {
    let redact_tab = |tab: &crate::spaces::SpaceTab| {
        let mut copy = tab.clone();
        copy.url = redact(&tab.url);
        copy.pinned_url = tab.pinned_url.as_deref().map(redact);
        copy
    };

    let mut copy = doc.clone();
    for space in &mut copy.spaces {
        space.tabs = space.tabs.iter().map(redact_tab).collect();
        space.pinned_tabs = space.pinned_tabs.iter().map(redact_tab).collect();
        if let Some(split) = space.split.as_mut() {
            split.primary_url = redact(&split.primary_url);
            split.secondary_url = redact(&split.secondary_url);
        }
    }
    for favorite in &mut copy.favorites {
        favorite.url = redact(&favorite.url);
    }
    for tab in &mut copy.archived {
        tab.url = redact(&tab.url);
    }
    for item in &mut copy.shelf {
        item.url = redact(&item.url);
    }
    for tab in &mut copy.snoozed {
        tab.url = redact(&tab.url);
    }
    for doorway in &mut copy.doorways {
        doorway.url = redact(&doorway.url);
    }
    // A highlight is looked up by URL, so a redacted one stops re-applying on
    // a page whose URL carried a credential - the same trade the `key=` note
    // above accepts, and the tab row it sits beside is redacted too.
    for marginal in &mut copy.marginalia {
        marginal.url = redact(&marginal.url);
    }
    copy
}

/// Whether the authority is well enough formed to touch.
///
/// Swift gates on Foundation's URL parser returning non-nil. That parser is
/// lenient about nearly everything - stray spaces, control bytes and bad
/// escapes all still parse - so the set it rejects is narrow. An unterminated
/// IPv6-literal bracket, `https://[::1/path`, is the one shape the Swift tests
/// name, and it is what this checks.
///
/// Deliberately narrower than Foundation. A URL Foundation rejects and this
/// accepts gets redacted rather than left alone, which loses a parameter from
/// a URL that was already malformed. The corpus is what would catch a case
/// that matters.
fn authority_parses(after_scheme: &str) -> bool {
    let authority_len = after_scheme.find('/').unwrap_or(after_scheme.len());
    let authority = &after_scheme[..authority_len];
    match (authority.find('['), authority.find(']')) {
        (Some(open), Some(close)) => open < close,
        (Some(_), None) => false,
        (None, Some(_)) => false,
        (None, None) => true,
    }
}

/// Strips `user:password@` when the userinfo HAS a password. A bare
/// `user@host` is left alone, because a username carries no secret.
///
/// Scans only the authority - between `://` and the first `/` - and uses the
/// LAST `@` in it, so an `@` inside the password cannot be mistaken for the
/// host separator.
fn strip_userinfo_password(base: &str) -> (String, bool) {
    let Some(scheme_end) = base.find("://") else {
        return (base.to_string(), false);
    };
    let authority_start = scheme_end + 3;
    let rest = &base[authority_start..];
    let authority_len = rest.find('/').unwrap_or(rest.len());
    let authority = &rest[..authority_len];

    let Some(at) = authority.rfind('@') else {
        return (base.to_string(), false);
    };
    if !authority[..at].contains(':') {
        return (base.to_string(), false);
    }
    let mut out = String::with_capacity(base.len());
    out.push_str(&base[..authority_start]);
    out.push_str(&base[authority_start + at + 1..]);
    (out, true)
}

/// Splits on EITHER `&` or `;` - the legacy separator some servers still emit,
/// so `?a=1;token=SECRET` is two pairs and not one named `a` with the token
/// riding along - drops the pairs that match, and rejoins the rest with each
/// surviving pair's OWN original delimiter. A string with nothing to drop
/// comes back byte-identical even if it uses `;` throughout.
fn redact_param_string(raw: &str) -> (String, bool) {
    if raw.is_empty() {
        return (raw.to_string(), false);
    }

    let pairs = tokenize(raw);
    let names: Vec<String> = pairs.iter().map(|p| decoded_name(p.text)).collect();
    let has_state = names.iter().any(|n| n == "state");

    let kept: Vec<bool> = names.iter().map(|n| !should_redact(n, has_state)).collect();
    if kept.iter().all(|k| *k) {
        return (raw.to_string(), false);
    }

    let mut out = String::with_capacity(raw.len());
    for i in 0..pairs.len() {
        if !kept[i] {
            continue;
        }
        out.push_str(pairs[i].text);
        if let Some(sep) = pairs[i].delimiter {
            if kept[i + 1..].iter().any(|k| *k) {
                out.push(sep);
            }
        }
    }
    (out, true)
}

/// One `name=value` pair plus the delimiter that followed it, carried so a
/// surviving pair's original separator is reproduced exactly.
struct Pair<'a> {
    text: &'a str,
    delimiter: Option<char>,
}

fn tokenize(raw: &str) -> Vec<Pair<'_>> {
    let mut pairs = Vec::new();
    let mut start = 0;
    for (i, c) in raw.char_indices() {
        if c == '&' || c == ';' {
            pairs.push(Pair {
                text: &raw[start..i],
                delimiter: Some(c),
            });
            start = i + c.len_utf8();
        }
    }
    pairs.push(Pair {
        text: &raw[start..],
        delimiter: None,
    });
    pairs
}

/// The pair's name - everything before the first `=`, or the whole pair -
/// percent-decoded to a FIXED POINT, capped at three passes so input that
/// decodes to itself cannot loop, then lowercased.
///
/// That catches `%2574oken` (`%25` becomes `%`, one more pass gives `token`)
/// as well as a single-decode `%74oken`. Only the NAME goes through this; the
/// bytes on disk are never altered, the whole pair is dropped or kept as it is.
fn decoded_name(text: &str) -> String {
    let raw_name = match text.find('=') {
        Some(i) => &text[..i],
        None => text,
    };
    let mut current = raw_name.to_string();
    for _ in 0..3 {
        let Some(decoded) = percent_decode(&current) else {
            break;
        };
        if decoded == current {
            break;
        }
        current = decoded;
    }
    current.to_lowercase()
}

/// One percent-decoding pass. Returns None when the input is not valid UTF-8
/// once decoded, which leaves the name compared in whatever form it reached -
/// never crashing on adversarial input, and never guaranteed to find a match
/// hiding behind a malformed escape.
fn percent_decode(s: &str) -> Option<String> {
    if !s.contains('%') {
        return Some(s.to_string());
    }
    let bytes = s.as_bytes();
    let mut out: Vec<u8> = Vec::with_capacity(bytes.len());
    let mut i = 0;
    while i < bytes.len() {
        if bytes[i] == b'%' && i + 2 < bytes.len() {
            let hex = std::str::from_utf8(&bytes[i + 1..i + 3]).ok()?;
            match u8::from_str_radix(hex, 16) {
                Ok(b) => {
                    out.push(b);
                    i += 3;
                    continue;
                }
                Err(_) => return None,
            }
        }
        out.push(bytes[i]);
        i += 1;
    }
    String::from_utf8(out).ok()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_url_with_nothing_to_drop_comes_back_byte_identical() {
        for url in [
            "https://example.com/a/b?x=1&y=2",
            "https://example.com/a;b?p=1;q=2",
            "https://example.com/?a=1+2%2B3",
            "https://user@example.com/",
        ] {
            assert_eq!(redact(url), url, "{url}");
        }
    }

    #[test]
    fn a_non_http_scheme_is_left_alone() {
        for url in ["chrome://settings?token=abc", "mailto:a@b.test?token=abc"] {
            assert_eq!(redact(url), url, "{url}");
        }
    }

    #[test]
    fn a_userinfo_password_is_stripped_and_a_bare_username_is_not() {
        assert_eq!(
            redact("https://admin:hunter2@example.net/panel"),
            "https://example.net/panel"
        );
        assert_eq!(
            redact("https://admin@example.net/"),
            "https://admin@example.net/"
        );
    }

    /// An `@` inside the password must not be read as the host separator.
    #[test]
    fn the_last_at_in_the_authority_is_the_separator() {
        assert_eq!(
            redact("https://user:pa@ss@example.net/x"),
            "https://example.net/x"
        );
    }

    #[test]
    fn a_matching_param_is_dropped_from_query_and_fragment() {
        assert_eq!(
            redact("https://example.com/?a=1&token=SECRET&b=2"),
            "https://example.com/?a=1&b=2"
        );
        assert_eq!(
            redact("https://example.com/#access_token=SECRET&b=2"),
            "https://example.com/#b=2"
        );
    }

    /// The legacy separator counts as one, or a token after a `;` survives.
    #[test]
    fn a_semicolon_separates_pairs_too() {
        assert_eq!(
            redact("https://example.com/?a=1;token=SECRET"),
            "https://example.com/?a=1"
        );
    }

    #[test]
    fn a_query_made_only_of_credentials_loses_its_separator() {
        assert_eq!(
            redact("https://example.com/?token=SECRET"),
            "https://example.com/"
        );
    }

    /// A promo code is not an OAuth grant.
    #[test]
    fn a_lone_code_survives_but_one_beside_state_does_not() {
        assert_eq!(
            redact("https://example.com/?code=SAVE20"),
            "https://example.com/?code=SAVE20"
        );
        assert_eq!(
            redact("https://example.com/?code=abc&state=xyz"),
            "https://example.com/?state=xyz"
        );
    }

    /// `state` is never dropped, so redacting twice gives the same answer.
    #[test]
    fn redacting_twice_changes_nothing_the_second_time() {
        for url in [
            "https://example.com/?code=abc&state=xyz",
            "https://admin:hunter2@example.net/?token=x&a=1",
        ] {
            let once = redact(url);
            assert_eq!(redact(&once), once, "{url}");
        }
    }

    #[test]
    fn carries_credential_flags_a_hyphenated_azure_style_param() {
        assert!(carries_credential(
            "https://res.example.azure.com/v1?api-key=SECRET"
        ));
    }

    #[test]
    fn a_signed_s3_url_drops_the_credential_and_signature_but_keeps_the_rest() {
        assert_eq!(
            redact(
                "https://mybucket.s3.amazonaws.com/key\
                 ?X-Amz-Algorithm=AWS4-HMAC-SHA256\
                 &X-Amz-Credential=AKIAIOSFODNN7EXAMPLE%2F20230101%2Fus-east-1%2Fs3%2Faws4_request\
                 &X-Amz-Date=20230101T000000Z\
                 &X-Amz-Expires=3600\
                 &X-Amz-SignedHeaders=host\
                 &X-Amz-Signature=abcdef1234567890"
            ),
            "https://mybucket.s3.amazonaws.com/key\
             ?X-Amz-Algorithm=AWS4-HMAC-SHA256\
             &X-Amz-Date=20230101T000000Z\
             &X-Amz-Expires=3600\
             &X-Amz-SignedHeaders=host"
        );
    }

    #[test]
    fn an_azure_sas_url_drops_the_sig_but_keeps_the_rest() {
        assert_eq!(
            redact(
                "https://myaccount.blob.core.windows.net/mycontainer/blob.txt\
                 ?sv=2022-11-02&ss=b&srt=sco&sp=rwdlacupitfx\
                 &se=2023-01-01T00%3A00%3A00Z&spr=https\
                 &sig=abc123SIGNATURE%3D"
            ),
            "https://myaccount.blob.core.windows.net/mycontainer/blob.txt\
             ?sv=2022-11-02&ss=b&srt=sco&sp=rwdlacupitfx\
             &se=2023-01-01T00%3A00%3A00Z&spr=https"
        );
    }

    #[test]
    fn a_gcs_v2_signed_url_drops_the_signature_but_keeps_the_rest() {
        assert_eq!(
            redact(
                "https://storage.googleapis.com/mybucket/myobject\
                 ?GoogleAccessId=example%40my-project.iam.gserviceaccount.com\
                 &Expires=1893456000\
                 &Signature=abcDEF123%2Bsignature%3D%3D"
            ),
            "https://storage.googleapis.com/mybucket/myobject\
             ?GoogleAccessId=example%40my-project.iam.gserviceaccount.com\
             &Expires=1893456000"
        );
    }

    #[test]
    fn client_secret_and_x_amz_security_token_are_redacted() {
        assert_eq!(
            redact("https://example.com/?client_secret=SECRET&x-amz-security-token=TOKEN&a=1"),
            "https://example.com/?a=1"
        );
    }

    #[test]
    fn carries_credential_leaves_a_harmless_url_alone() {
        assert!(!carries_credential(
            "https://api.example.com/v1?api-version=2024-02-01"
        ));
    }

    #[test]
    fn a_name_matches_only_in_full() {
        assert_eq!(
            redact("https://example.com/?monotoken=1"),
            "https://example.com/?monotoken=1"
        );
    }

    #[test]
    fn a_percent_encoded_name_is_decoded_before_matching() {
        assert_eq!(
            redact("https://example.com/?%74oken=x"),
            "https://example.com/"
        );
        assert_eq!(
            redact("https://example.com/?%2574oken=x"),
            "https://example.com/"
        );
    }

    #[test]
    fn a_name_is_matched_case_insensitively() {
        assert_eq!(
            redact("https://example.com/?TOKEN=x"),
            "https://example.com/"
        );
    }

    /// An unterminated IPv6 bracket is the one shape Foundation's parser
    /// rejects outright, so Swift leaves it alone and so does this.
    #[test]
    fn an_unparseable_authority_is_left_alone() {
        let url = "https://[::1/path?token=abc";
        assert_eq!(redact(url), url);
        assert_eq!(
            redact("https://[::1]/path?token=abc"),
            "https://[::1]/path",
            "a properly closed bracket is an ordinary host"
        );
    }

    #[test]
    fn a_malformed_escape_does_not_crash_or_loop() {
        let url = "https://example.com/?%zz=1&%=2&tok%=3";
        assert_eq!(redact(url), url);
    }

    /// One space carrying a split arrangement and one highlight, both on a URL
    /// with a credential in it.
    fn doc_with_a_split_and_a_marginal(url: &str) -> crate::spaces::Document {
        let seed = format!(
            r#"{{"spaces":[{{"id":"A","name":"Work","emoji":"W","tint":"t",
            "profileID":"Work","pinnedTabs":[],
            "tabs":[{{"url":"{url}","title":"Mail"}}],"order":0,"groups":[],
            "split":{{"primaryURL":"{url}","secondaryURL":"{url}",
            "orientation":"horizontal","ratio":0.5}}}}],
            "marginalia":[{{"id":"mark-1","url":"{url}","spaceID":"A",
            "quote":"quoted","note":"","createdAt":1.0}}],
            "appState":{{"activeSpaceID":"A","sidebarWidth":220,"schemaVersion":3}}}}"#
        );
        crate::spaces::load(&seed, 0.0).unwrap()
    }

    /// Nothing sweeps a highlight, so a token in one outlives even the 30 days
    /// an archived tab's URL gets.
    #[test]
    fn a_marginals_url_goes_through_the_funnel() {
        let doc = doc_with_a_split_and_a_marginal("https://mail.example/?token=SECRET");
        let copy = redact_document(&doc);
        assert_eq!(copy.marginalia[0].url, "https://mail.example/");
    }

    /// The two URLs of a split are persisted beside the tab rows they came
    /// from, and R7.4 admits no exception for them.
    #[test]
    fn a_splits_two_urls_go_through_the_funnel() {
        let doc = doc_with_a_split_and_a_marginal("https://mail.example/?token=SECRET");
        let copy = redact_document(&doc);
        let split = copy.spaces[0].split.as_ref().expect("the split survives");
        assert_eq!(split.primary_url, "https://mail.example/");
        assert_eq!(split.secondary_url, "https://mail.example/");
    }
}
