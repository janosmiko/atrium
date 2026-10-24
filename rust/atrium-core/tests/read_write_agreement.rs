// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! a settings.json this build can LOAD can always be SAVED.
//!
//! The read path and the write path used to disagree about what a legal file
//! is, and the write path was the stricter of the two. `settings::FileShape`
//! skips a key it has no field for with serde's `IgnoredAny`, which walks the
//! value's tokens and looks at nothing else; the writer asked for the whole file
//! as a `serde_json::Map<String, Value>`, which looks at all of it. Three shapes
//! sit in the gap. Each of them loaded fine and then made EVERY later save fail
//! with -2 and the file byte-identical, for good - including saves of settings
//! the user had just changed and this build understood perfectly.
//!
//! The documents are put through both paths in the same test on purpose. A
//! writer-only test cannot see a disagreement; only asking the same bytes twice
//! can.

use atrium_core::{atrium_core_query, atrium_core_save_settings};

mod common;
use common::{TempDir, c};

/// The four keys a settings.json must carry, plus one this build understands
/// and can be asked to change, so every case below can carry a real edit.
const HEAD: &str = r#"{"schemaVersion":1,"appIconVariant":"nord-accent",
"animationsEnabled":true,"startupBehavior":"blankTab","confirmToQuitEnabled":false"#;

/// The read, exactly as `LoadSettingsJSON` performs it.
fn query(contents: &str) -> String {
    let (store, contents) = (c("settings"), c(contents));
    let out = unsafe { atrium_core_query(store.as_ptr(), contents.as_ptr(), 0.0) };
    unsafe { common::take_string(out) }.expect("the settings read always answers")
}

/// Whether the reader took the file, read off its answer rather than a code.
///
/// The reader never says no out loud: `settings::load` answers a file it cannot
/// read with pure DEFAULTS in memory and leaves declining the WRITE to the
/// writer, which is R1.2's shape and the reason `load_checked` had to exist at
/// all. So the verdict is read off the values. Every document here carries
/// `appIconVariant: nord-accent`, which is not the default, so getting it back
/// means the file was read and not replaced.
fn the_reader_takes_it(doc: &str) -> bool {
    let answered: serde_json::Value = serde_json::from_str(&query(doc)).unwrap();
    answered["appIconVariant"] == serde_json::json!("nord-accent")
}

fn save(path: &std::path::Path, json: &str) -> i32 {
    let (path, json) = (c(path.to_str().unwrap()), c(json));
    unsafe { atrium_core_save_settings(path.as_ptr(), json.as_ptr()) }
}

/// A file on disk, the reader's view of it, and the same view with one field
/// this build owns flipped - which is the only shape the surface can send.
struct OnDisk {
    dir: TempDir,
    path: std::path::PathBuf,
}

impl OnDisk {
    fn holding(tag: &str, contents: &str) -> Self {
        let dir = TempDir::new(tag);
        let path = dir.join("settings.json");
        std::fs::write(&path, contents).unwrap();
        Self { dir, path }
    }

    fn bytes(&self) -> String {
        std::fs::read_to_string(&self.path).unwrap()
    }

    fn has_bak(&self) -> bool {
        self.dir.join("settings.json.bak").exists()
    }

    /// What the surface would send back after toggling "warn before quitting".
    fn payload_with_the_toggle_flipped(&self) -> String {
        let mut settings: serde_json::Value = serde_json::from_str(&query(&self.bytes())).unwrap();
        let was = settings["confirmToQuitEnabled"].as_bool().unwrap();
        settings["confirmToQuitEnabled"] = serde_json::json!(!was);
        settings.to_string()
    }
}

/// A nesting deeper than serde_json's recursion limit, which is 128 and counts
/// the object around it.
fn nested(depth: usize) -> String {
    format!("{}{}", "[".repeat(depth), "]".repeat(depth))
}

/// The three documents, each named by the value in it that only one of the two
/// paths could stomach.
fn the_three() -> Vec<(&'static str, String, &'static str)> {
    vec![
        (
            "a number outside f64's range",
            format!("{HEAD},\"laterHuge\":1e400}}"),
            r#""laterHuge":1e400"#,
        ),
        (
            "a nesting past the recursion limit",
            format!("{HEAD},\"laterNest\":{}}}", nested(130)),
            r#""laterNest":[[["#,
        ),
        (
            "a lone surrogate",
            format!("{HEAD},\"laterText\":\"\\ud800\"}}"),
            r#""laterText":"\ud800""#,
        ),
    ]
}

/// The three documents, through the export, on the disk.
///
/// Each assertion is one half of the bug: the save lands at all, the user's
/// edit to a DIFFERENT setting lands with it, the value that used to stop the
/// save is still spelled the way the file spelled it, and the file this build
/// just wrote still loads.
#[test]
fn the_three_documents_that_loaded_and_could_never_be_saved_now_save() {
    for (what, doc, spelling) in the_three() {
        let file = OnDisk::holding(&what.replace(' ', "-"), &doc);

        assert_eq!(
            save(&file.path, &file.payload_with_the_toggle_flipped()),
            1,
            "{what}: the save was declined"
        );

        let after = file.bytes();
        assert!(
            after.contains(r#""confirmToQuitEnabled":true"#),
            "{what}: the user's edit did not land in {after}"
        );
        assert!(
            after.contains(spelling),
            "{what}: {spelling} is gone from {after}"
        );
        assert!(
            the_reader_takes_it(&after),
            "{what}: the file this build just wrote no longer loads"
        );
    }
}

/// The only shape of test that can see this class of defect: the same
/// bytes through both paths, asserting they answer alike.
///
/// Both directions matter. A document the reader takes must be one the writer
/// takes, which is the defect. A document the reader REFUSES must be one the
/// writer refuses too, which is R1.2 and must not be loosened by fixing the
/// first - so the same battery carries a newer schema version, bytes that are
/// not JSON, and each of the three values moved under a key this build
/// DECLARES, where the reader stops on them as well.
#[test]
fn the_reader_and_the_writer_agree_on_every_document() {
    let cases: Vec<(&str, String)> = vec![
        ("the minimal file", format!("{HEAD}}}")),
        (
            "a key this build has no field for",
            format!("{HEAD},\"laterOnly\":{{\"a\":1}}}}"),
        ),
        // The three. The reader takes them, so the writer has to.
        ("an unknown key at 1e400", format!("{HEAD},\"n\":1e400}}")),
        (
            "an unknown key nested 130 deep",
            format!("{HEAD},\"n\":{}}}", nested(130)),
        ),
        (
            "an unknown key holding a lone surrogate",
            format!("{HEAD},\"n\":\"\\ud800\"}}"),
        ),
        // The same three values under a key this build declares. The reader
        // has to look at those, so it stops - and the writer must stop too.
        (
            "a declared number at 1e400",
            format!("{HEAD},\"glassOpacity\":1e400}}"),
        ),
        (
            "a declared field nested 130 deep",
            format!("{HEAD},\"downloadsDirectory\":{}}}", nested(130)),
        ),
        (
            "a declared string holding a lone surrogate",
            format!("{HEAD},\"downloadsDirectory\":\"\\ud800\"}}"),
        ),
        // R1.2's own cases, unchanged.
        (
            "a schema this build cannot read",
            r#"{"schemaVersion":99,"appIconVariant":"nord-accent",
            "animationsEnabled":true,"startupBehavior":"blankTab"}"#
                .to_string(),
        ),
        ("bytes that are not JSON", "not json at all".to_string()),
        ("a truncated file", format!("{HEAD},")),
    ];

    for (what, doc) in cases {
        let file = OnDisk::holding(&format!("agree-{}", what.replace(' ', "-")), &doc);

        let reader_takes_it = the_reader_takes_it(&doc);
        // A payload this build can read, so the only thing the code can answer
        // about is the file on disk. -2 is the write path declining it.
        let writer_takes_it = save(&file.path, &query(&format!("{HEAD}}}"))) != -2;

        assert_eq!(
            reader_takes_it, writer_takes_it,
            "{what}: the reader says {reader_takes_it} and the writer says \
             {writer_takes_it} about the same bytes"
        );
        if !reader_takes_it {
            assert_eq!(file.bytes(), doc, "{what}: a refused file was written over");
        }
    }
}

/// A save is still a fixed point once one of these values is in the file, or
/// the writer would rewrite the file on every save forever - which is what
/// `persist`'s own header promises it does not do.
#[test]
fn a_second_save_changes_nothing() {
    for (what, doc, _) in the_three() {
        let file = OnDisk::holding(&format!("fixed-{}", what.replace(' ', "-")), &doc);

        assert_eq!(save(&file.path, &file.payload_with_the_toggle_flipped()), 1);
        let once = file.bytes();
        assert_eq!(save(&file.path, &query(&once)), 1);

        assert_eq!(file.bytes(), once, "{what}: the second save moved the file");
    }
}

/// The same asymmetry one level down, and it was the worse half of the two.
///
/// `persist::backup_if_valid` decided a file was worth keeping by parsing it as
/// a `serde_json::Value`, which is the narrow reader again. So for exactly these
/// three files - the ones where a downgrade is most likely to be in play - the
/// main file was rewritten with no `.bak` beside it. It takes the settings
/// loader now, the same one that decides whether the save may proceed.
#[test]
fn the_recovery_copy_is_taken_for_a_file_the_reader_accepts() {
    for (what, doc, _) in the_three() {
        let file = OnDisk::holding(&format!("bak-{}", what.replace(' ', "-")), &doc);

        assert_eq!(save(&file.path, &file.payload_with_the_toggle_flipped()), 1);
        assert!(file.has_bak(), "{what}: the save left no .bak");
        assert_eq!(
            std::fs::read_to_string(file.dir.join("settings.json.bak")).unwrap(),
            doc,
            "{what}: the .bak is not the file as it was"
        );
    }
}

/// A file the READER refuses still gets no `.bak`, which is the half of
/// `backup_if_valid` worth keeping: copying rubbish over a good recovery file is
/// worse than having none.
#[test]
fn the_recovery_copy_is_still_refused_for_a_file_the_reader_refuses() {
    let good = format!("{HEAD}}}");
    let file = OnDisk::holding("bak-refused", &good);
    assert_eq!(save(&file.path, &query(&good)), 1);
    let good_bak = std::fs::read_to_string(file.dir.join("settings.json.bak")).unwrap();

    std::fs::write(&file.path, "not json at all").unwrap();
    assert_eq!(save(&file.path, &query(&good)), -2);
    assert_eq!(
        std::fs::read_to_string(file.dir.join("settings.json.bak")).unwrap(),
        good_bak,
        "the good recovery copy survived"
    );
}

/// A key this build DECLARES is still spelled by this build, even in a file
/// whose other keys are only reachable as text.
///
/// This is the line between the two halves. Falling back to text for the whole
/// file would be the easy fix and would take the writer's own forms with it: a
/// slash is escaped here (`emit_string`), a whole float loses its `.0`. The
/// keys are asked one at a time so a value the parser refuses costs only that
/// key. A string parses exactly and its escaping is
/// therefore this build's to choose; that has to stay true here.
#[test]
fn a_declared_string_is_still_spelled_by_this_build_in_a_file_it_cannot_wholly_parse() {
    let doc =
        format!("{HEAD},\"downloadsDirectory\":\"/Users/someone/Downloads\",\"laterHuge\":1e400}}");
    let file = OnDisk::holding("declared-spelling", &doc);

    assert_eq!(save(&file.path, &file.payload_with_the_toggle_flipped()), 1);

    let after = file.bytes();
    assert!(
        after.contains(r#""downloadsDirectory":"\/Users\/someone\/Downloads""#),
        "the declared string lost this build's escaping in {after}"
    );
    assert!(after.contains(r#""laterHuge":1e400"#), "{after}");
}

// --- clearing a value the file spells in a way this build cannot use ---

const A_COLOUR_THIS_BUILD_CANNOT_USE: &str = "oklch(0.2 0.05 210)";

fn with_surround(colour: &str) -> String {
    format!("{HEAD},\"surroundColor\":\"{colour}\"}}")
}

/// Clearing a colour this build CAN read works, and has all along. It is here
/// as the control for the pair below.
#[test]
fn clearing_a_colour_this_build_can_read_lands() {
    let file = OnDisk::holding("clear-hex", &with_surround("#12282E"));

    let mut payload: serde_json::Value = serde_json::from_str(&query(&file.bytes())).unwrap();
    assert_eq!(payload["surroundColor"], serde_json::json!("#12282E"));
    payload.as_object_mut().unwrap().remove("surroundColor");

    assert_eq!(save(&file.path, &payload.to_string()), 1);
    assert!(!file.bytes().contains("surroundColor"), "{}", file.bytes());
}

/// A clear lands in a file that also holds a value this build's JSON parser
/// refuses - which is the same defect wearing the other hat.
///
/// The colour here is one this build reads perfectly. What used to swallow the
/// clear was a key SOMEWHERE ELSE in the file that the writer's parse would not
/// take: every save of that file was declined, so no edit of any kind reached
/// the disk, a clear included.
#[test]
fn clearing_a_colour_lands_in_a_file_this_build_cannot_wholly_parse() {
    let doc = format!("{HEAD},\"surroundColor\":\"#12282E\",\"laterHuge\":1e400}}");
    let file = OnDisk::holding("clear-beside-junk", &doc);

    let mut payload: serde_json::Value = serde_json::from_str(&query(&file.bytes())).unwrap();
    assert_eq!(payload["surroundColor"], serde_json::json!("#12282E"));
    payload.as_object_mut().unwrap().remove("surroundColor");

    assert_eq!(save(&file.path, &payload.to_string()), 1);
    let after = file.bytes();
    assert!(
        !after.contains("surroundColor"),
        "the clear did not land: {after}"
    );
    assert!(
        after.contains(r#""laterHuge":1e400"#),
        "the value that used to stop the save is gone from {after}"
    );
}

/// Setting a colour over one this build cannot read lands too. The caller's
/// value differs from what the file loaded to, which is an edit, and an edit
/// always wins over the file's own spelling.
#[test]
fn setting_a_colour_over_one_this_build_cannot_read_lands() {
    let file = OnDisk::holding(
        "set-over-junk",
        &with_surround(A_COLOUR_THIS_BUILD_CANNOT_USE),
    );

    let mut payload: serde_json::Value = serde_json::from_str(&query(&file.bytes())).unwrap();
    payload["surroundColor"] = serde_json::json!("#AABBCC");

    assert_eq!(save(&file.path, &payload.to_string()), 1);
    let after = file.bytes();
    assert!(after.contains(r##""surroundColor":"#AABBCC""##), "{after}");
    assert!(!after.contains("oklch"), "{after}");
}

/// And an edit to a DIFFERENT setting lands over it, which is the case the
/// whole task turns on.
#[test]
fn an_edit_to_another_setting_lands_over_a_colour_this_build_cannot_read() {
    let file = OnDisk::holding("other-edit", &with_surround(A_COLOUR_THIS_BUILD_CANNOT_USE));

    assert_eq!(save(&file.path, &file.payload_with_the_toggle_flipped()), 1);

    let after = file.bytes();
    assert!(after.contains(r#""confirmToQuitEnabled":true"#), "{after}");
    assert!(after.contains("oklch(0.2 0.05 210)"), "{after}");
}

/// What is left, stated on purpose rather than left to be found again.
///
/// A clear of a colour this build cannot read does NOT land, and it cannot: the
/// payload for "I cleared it" and the payload for "I did not touch it" are the
/// same bytes. `surroundColor` is normalised to `None` on the way in, `None`
/// serialises as an ABSENT key, and the surface only ever echoes back what the
/// read gave it - so both intents arrive as a payload with no `surroundColor`
/// in it. There is nothing in the file, the payload, or the loaded value that
/// tells the two apart.
///
/// Given a coin toss, this is the side that cannot destroy anything. Keeping the
/// file's text costs a deliberate clear, which the user can still reach in two
/// steps - set a colour, save, clear it, save - and which changes nothing they
/// can see, because the surface renders `None` as automatic before and after.
/// Dropping the key instead would throw a later build's colour away on an
/// unrelated edit, which is the exact harm this writer exists to prevent.
///
/// It is also not a rule invented here. `startupBehavior` and `glassOpacity`
/// make the same trade for the same reason - `abi_settings.rs` pins a later
/// build's `someLaterMode` and `5.0` surviving a save - and the only thing that
/// made `surroundColor` look different is that its normalised value is an
/// absence rather than a value.
#[test]
fn a_clear_of_a_colour_this_build_cannot_read_keeps_the_file_s_text() {
    let file = OnDisk::holding("clear-junk", &with_surround(A_COLOUR_THIS_BUILD_CANNOT_USE));

    let payload: serde_json::Value = serde_json::from_str(&query(&file.bytes())).unwrap();
    assert!(
        payload.get("surroundColor").is_none(),
        "the read already gives the caller nothing to send back"
    );

    assert_eq!(save(&file.path, &payload.to_string()), 1);
    assert!(
        file.bytes().contains("oklch(0.2 0.05 210)"),
        "{}",
        file.bytes()
    );
}
