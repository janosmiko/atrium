// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
/// Everything the decision needs about a link another macOS app handed over.
///
/// No profile here on purpose. The spec says "the last-used profile" and no such
/// concept exists yet - which profile a little window opens on is a separate
/// resolution, and folding it in would hide that fork inside this answer.
///
/// `repr(C)` because the openURLs hook fills this in from C++ and passes a
/// pointer; field order and types match `AtriumExternalLinkInputs` in the
/// header, which the ABI version covers.
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ExternalLinkInputs {
    /// The `externalLinksOpenInLittle` setting. Absent in the file reads true.
    pub external_links_open_in_little: bool,
    /// False when the app was handed no URL at all - an openURLs with an empty
    /// list, or a plain launch.
    pub has_url: bool,
    pub is_http_or_https: bool,
}

/// Where a handed-over link opens.
///
/// The discriminants are the ABI - `atrium_core_external_link_target` returns
/// one of them as an `int32_t`, and the header spells the same two numbers.
#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ExternalLinkTarget {
    LittleWindow = 0,
    /// What the browser does today, and what the setting turns back on.
    SpaceWindow = 1,
}

/// The whole routing rule.
///
/// The scheme and URL checks come first because they are not the setting's to
/// override: a little window is a web-page surface, so `mailto:`, `file:` and
/// `javascript:` keep the path they already have whatever the row says.
pub fn decide_external_link_target(input: &ExternalLinkInputs) -> ExternalLinkTarget {
    if !input.has_url || !input.is_http_or_https {
        return ExternalLinkTarget::SpaceWindow;
    }
    if input.external_links_open_in_little {
        ExternalLinkTarget::LittleWindow
    } else {
        ExternalLinkTarget::SpaceWindow
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// A plain https link from another app with the setting left alone.
    fn external_link() -> ExternalLinkInputs {
        ExternalLinkInputs {
            external_links_open_in_little: true,
            has_url: true,
            is_http_or_https: true,
        }
    }

    #[test]
    fn setting_on_opens_an_external_link_in_a_little_window() {
        assert_eq!(
            decide_external_link_target(&external_link()),
            ExternalLinkTarget::LittleWindow
        );
    }

    /// Off reverts to today's behavior, and today's
    /// behavior is a space window.
    #[test]
    fn setting_off_opens_an_external_link_in_a_space_window() {
        let mut input = external_link();
        input.external_links_open_in_little = false;
        assert_eq!(
            decide_external_link_target(&input),
            ExternalLinkTarget::SpaceWindow
        );
    }

    /// The absent key and the decision have to agree, or a profile that has
    /// never seen the row would route differently from one that has.
    #[test]
    fn a_settings_file_without_the_key_routes_to_a_little_window() {
        let settings = crate::settings::load(
            r#"{"schemaVersion":1,"appIconVariant":"nord-accent",
            "animationsEnabled":true,"startupBehavior":"blankTab"}"#,
        );

        assert_eq!(
            decide_external_link_target(&ExternalLinkInputs {
                external_links_open_in_little: settings.external_links_open_in_little,
                ..external_link()
            }),
            ExternalLinkTarget::LittleWindow
        );
    }

    /// Another app can hand over `mailto:`, `file:` or `javascript:`. A little
    /// window is a web-page surface; anything else keeps the path it has today
    /// rather than gaining a new window type it was never checked against.
    #[test]
    fn a_non_web_scheme_never_opens_a_little_window() {
        let mut input = external_link();
        input.is_http_or_https = false;
        assert_eq!(
            decide_external_link_target(&input),
            ExternalLinkTarget::SpaceWindow
        );
    }

    /// The scheme check outranks the setting, so an off setting cannot be the
    /// only thing keeping a `javascript:` URL out of a little window.
    #[test]
    fn a_non_web_scheme_is_refused_independently_of_the_setting() {
        let mut input = external_link();
        input.is_http_or_https = false;
        input.external_links_open_in_little = false;
        assert_eq!(
            decide_external_link_target(&input),
            ExternalLinkTarget::SpaceWindow
        );
    }

    /// Launching the app from another app with no URL at all - a dock drop of
    /// nothing, or an openURLs with an empty list. There is no link to put in
    /// a little window.
    #[test]
    fn an_empty_handover_opens_no_little_window() {
        let mut input = external_link();
        input.has_url = false;
        assert_eq!(
            decide_external_link_target(&input),
            ExternalLinkTarget::SpaceWindow
        );
    }
}
