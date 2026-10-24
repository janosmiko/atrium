// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! the BYTES a settings save writes, over a corpus of later-build files.
//!
//! `make rust-corpus` cannot see this class and never could. `corpus-check.py`
//! hands a file to a runner and compares `json.load` against `json.load`, so
//! every question about formatting - whitespace, key order, how a number is
//! spelled - is answered "equal" before it is asked. That is the right call
//! there: the harness is deliberately implementation-neutral, and byte equality
//! would pin any second store implementation to Rust's exact spelling. So the
//! byte question belongs here, next to the writer that answers it.
//!
//! What it guards is the seam for unknown keys. A key this build has no
//! field for is now copied through as the file's own TEXT rather than as a
//! parsed value, which is the only way to keep a number the parser cannot
//! reproduce. Splicing that text verbatim also spliced the older file's
//! FORMATTING into an otherwise compact file, and every later save kept it.
//!
//! Three properties, and the third is the one that has to be read carefully:
//!
//!   1. the saved file carries no insignificant whitespace, whichever half a
//!      key came from - the property `to_sorted_json` gave for free;
//!   2. a number is spelled the way the file spells it, byte for byte;
//!   3. a preserved object's INNER key order is left exactly as found. That is
//!      not an oversight in the compaction. Sorting a subtree this build does
//!      not understand is a rewrite of a later build's data, which is what
//!      R1.2 (docs/model-rules.md) exists to forbid. The count below is
//!      expected to be non-zero and the test says so on purpose.

use std::collections::BTreeMap;

use atrium_core::settings;

/// The measurement was taken over this many documents.
const DOCUMENTS: usize = 3000;

// --- the corpus ---------------------------------------------------------

/// A later-build settings.json plus what its bytes have to survive.
struct Document {
    json: String,
    /// Number literals planted under keys this build has no field for.
    numbers: Vec<String>,
    /// String values planted the same way, as the file spells them, quotes and
    /// all. Whitespace inside these is significant and must come through.
    strings: Vec<String>,
}

/// xorshift64*, so the corpus is the same corpus on every machine and every run.
struct Rng(u64);

impl Rng {
    fn next(&mut self) -> u64 {
        let mut x = self.0;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        self.0 = x;
        x.wrapping_mul(0x2545_f491_4f6c_dd1d)
    }

    fn below(&mut self, n: usize) -> usize {
        (self.next() % n as u64) as usize
    }

    fn pick<'a, T>(&mut self, from: &'a [T]) -> &'a T {
        &from[self.below(from.len())]
    }
}

/// A value as a later build's file spells it, before any layout is applied.
enum Node {
    /// A number kept as its literal text - the whole reason spans exist.
    Number(String),
    /// A string INCLUDING its quotes and escapes, exactly as written.
    Text(String),
    Bool(bool),
    Null,
    /// Keys in the order the generator emits them, which is deliberately not
    /// sorted order.
    Object(Vec<(String, Node)>),
    Array(Vec<Node>),
}

/// How a build pretty-prints. The old writer had exactly one of these; a later
/// build is free to have any.
struct Layout {
    indent: &'static str,
    newline: bool,
    after_colon: &'static str,
}

const LAYOUTS: [Layout; 5] = [
    Layout {
        indent: "",
        newline: false,
        after_colon: "",
    },
    Layout {
        indent: "",
        newline: false,
        after_colon: " ",
    },
    Layout {
        indent: "  ",
        newline: true,
        after_colon: " ",
    },
    Layout {
        indent: "    ",
        newline: true,
        after_colon: " ",
    },
    Layout {
        indent: "\t",
        newline: true,
        after_colon: "\t",
    },
];

/// Spellings the writer's own number path would not reproduce, mixed with ones
/// it would. Every one of them has to come back character-identical.
const NUMBERS: [&str; 14] = [
    "5.0",
    "1e2",
    "1.50",
    "-0.0",
    "0.1234567890123456789",
    "18446744073709551615",
    "1e-400",
    "1.7976931348623157e308",
    "0.30000000000000004",
    "6.02e23",
    "-17",
    "42",
    "3.14159265358979",
    "1E+7",
];

/// Values whose whitespace and escapes are the file's, not the writer's.
const STRINGS: [&str; 8] = [
    r#""a b""#,
    r#""  padded  ""#,
    r#""line\nbreak""#,
    r#""tab\there""#,
    r#""brace } and , comma""#,
    r#""quote \" and backslash \\""#,
    r#""café au lait""#,
    r#""{\"looks\": \"nested\"}""#,
];

/// Keys a later build added, which this one has no field for.
const LATER_KEYS: [&str; 10] = [
    "laterPanel",
    "zzTheme",
    "aardvarkMode",
    "spaceTint",
    "mruDepth",
    "peekScrim",
    "boostRules",
    "dispatchWall",
    "quietHours",
    "tabTint",
];

/// Keys this build DOES declare, given values it changes on load, so the
/// preserved half is not only unknown keys. All three come back through the
/// same span path: clamped, replaced by a default, and dropped as malformed.
const PERTURBED: [(&str, &str); 3] = [
    ("glassOpacity", "5.0"),
    ("appIconVariant", r#""later-variant""#),
    ("surroundColor", r#""rgb(1, 2, 3)""#),
];

fn render(node: &Node, layout: &Layout, depth: usize, out: &mut String) {
    let (open, close) = match node {
        Node::Number(text) => return out.push_str(text),
        Node::Text(text) => return out.push_str(text),
        Node::Bool(b) => return out.push_str(if *b { "true" } else { "false" }),
        Node::Null => return out.push_str("null"),
        Node::Object(_) => ('{', '}'),
        Node::Array(_) => ('[', ']'),
    };

    let gap = |out: &mut String, depth: usize| {
        if layout.newline {
            out.push('\n');
            for _ in 0..depth {
                out.push_str(layout.indent);
            }
        }
    };

    out.push(open);
    match node {
        Node::Object(pairs) => {
            for (i, (key, value)) in pairs.iter().enumerate() {
                if i > 0 {
                    out.push(',');
                }
                gap(out, depth + 1);
                out.push('"');
                out.push_str(key);
                out.push_str("\":");
                out.push_str(layout.after_colon);
                render(value, layout, depth + 1, out);
            }
            if !pairs.is_empty() {
                gap(out, depth);
            }
        }
        Node::Array(items) => {
            for (i, item) in items.iter().enumerate() {
                if i > 0 {
                    out.push(',');
                }
                gap(out, depth + 1);
                render(item, layout, depth + 1, out);
            }
            if !items.is_empty() {
                gap(out, depth);
            }
        }
        _ => unreachable!("scalars returned above"),
    }
    out.push(close);
}

/// Collects the literals a document plants, so the assertions can name them.
fn collect(node: &Node, numbers: &mut Vec<String>, strings: &mut Vec<String>) {
    match node {
        Node::Number(text) => numbers.push(text.clone()),
        Node::Text(text) => strings.push(text.clone()),
        Node::Object(pairs) => {
            for (_, value) in pairs {
                collect(value, numbers, strings);
            }
        }
        Node::Array(items) => {
            for item in items {
                collect(item, numbers, strings);
            }
        }
        Node::Bool(_) | Node::Null => {}
    }
}

fn scalar(rng: &mut Rng) -> Node {
    match rng.below(5) {
        0 | 1 => Node::Number(rng.pick(&NUMBERS).to_string()),
        2 | 3 => Node::Text(rng.pick(&STRINGS).to_string()),
        _ if rng.below(2) == 0 => Node::Bool(rng.below(2) == 0),
        _ => Node::Null,
    }
}

/// Nested keys are drawn in a deliberately unsorted order - a later build's
/// object is whatever order that build wrote it in.
fn value(rng: &mut Rng, depth: usize) -> Node {
    if depth == 0 || rng.below(3) == 0 {
        return scalar(rng);
    }
    if rng.below(2) == 0 {
        let n = 2 + rng.below(3);
        let mut pairs = Vec::new();
        for _ in 0..n {
            // Descending so the order is wrong in the sorted sense on purpose.
            let key = format!("z{}", 9 - pairs.len());
            pairs.push((key, value(rng, depth - 1)));
        }
        Node::Object(pairs)
    } else {
        let n = 1 + rng.below(3);
        Node::Array((0..n).map(|_| value(rng, depth - 1)).collect())
    }
}

fn document(rng: &mut Rng) -> Document {
    let layout = rng.pick(&LAYOUTS);
    let mut numbers = Vec::new();
    let mut strings = Vec::new();

    // The four keys every settings.json has carried since v1, so the version
    // gate lets the file in and the load has something to reproduce.
    let mut body = String::from("{");
    let mut first = true;
    let pair = |body: &mut String, key: &str, text: &str, first: &mut bool| {
        if !*first {
            body.push(',');
        }
        *first = false;
        if layout.newline {
            body.push('\n');
            body.push_str(layout.indent);
        }
        body.push('"');
        body.push_str(key);
        body.push_str("\":");
        body.push_str(layout.after_colon);
        body.push_str(text);
    };

    // A declared key this build alters on load, half the time. It replaces the
    // starting value when it names one of the four, because a settings.json
    // with the key twice is not a file any build wrote.
    let perturbed = (rng.below(2) == 0).then(|| *rng.pick(&PERTURBED));
    let mut declared = vec![
        ("schemaVersion", "1"),
        ("appIconVariant", "\"nord-accent\""),
        ("animationsEnabled", "true"),
        ("startupBehavior", "\"blankTab\""),
    ];
    if let Some((key, text)) = perturbed {
        match declared.iter_mut().find(|(k, _)| *k == key) {
            Some(slot) => slot.1 = text,
            None => declared.push((key, text)),
        }
    }
    for (key, text) in &declared {
        pair(&mut body, key, text, &mut first);
    }

    // One to four keys this build has no field for.
    let mut keys: Vec<&str> = LATER_KEYS.to_vec();
    for _ in 0..1 + rng.below(4) {
        let key = keys.remove(rng.below(keys.len()));
        let depth = 1 + rng.below(3);
        let node = value(rng, depth);
        collect(&node, &mut numbers, &mut strings);
        let mut text = String::new();
        render(&node, layout, 1, &mut text);
        pair(&mut body, key, &text, &mut first);
    }

    if layout.newline {
        body.push('\n');
    }
    body.push('}');

    Document {
        json: body,
        numbers,
        strings,
    }
}

fn corpus() -> Vec<Document> {
    let mut rng = Rng(0x5EED_0468);
    (0..DOCUMENTS).map(|_| document(&mut rng)).collect()
}

/// What `save_settings` writes, without the disk, so the corpus can be this
/// dense. The disk path is covered by `persist`'s own tests.
fn save(json: &str) -> String {
    let (settings, found) = settings::load_as_found(json)
        .unwrap_or_else(|e| panic!("a generated document must load: {e:?}\n{json}"));
    found.merge(&settings).to_json()
}

// --- the byte questions -------------------------------------------------

/// The first insignificant whitespace character, or None. Whitespace inside a
/// string is the file's own and does not count.
fn insignificant_whitespace(json: &str) -> Option<char> {
    let b = json.as_bytes();
    let mut i = 0;
    while i < b.len() {
        match b[i] {
            b'"' => i = end_of_string(b, i),
            c if c.is_ascii_whitespace() => return Some(c as char),
            _ => i += 1,
        }
    }
    None
}

/// Every nested object's keys in written order, grouped by the top-level key
/// the object sits under.
///
/// Grouped rather than flat because the writer sorts the OUTERMOST object, so
/// a flat list would report a reordering that is just the top level being
/// tidied. The question here is only about what is inside a preserved value.
fn nested_key_orders(json: &str) -> BTreeMap<String, Vec<Vec<String>>> {
    let b = json.as_bytes();
    let mut stack: Vec<(u8, Vec<String>)> = Vec::new();
    let mut out: BTreeMap<String, Vec<Vec<String>>> = BTreeMap::new();
    let mut under = String::new();
    let mut i = 0;

    while i < b.len() {
        match b[i] {
            b'{' | b'[' => {
                stack.push((b[i], Vec::new()));
                i += 1;
            }
            b'}' | b']' => {
                if let Some((b'{', keys)) = stack.pop()
                    && !stack.is_empty()
                {
                    out.entry(under.clone()).or_default().push(keys);
                }
                i += 1;
            }
            b'"' => {
                let at = i;
                i = end_of_string(b, i);
                let mut j = i;
                while b.get(j).is_some_and(u8::is_ascii_whitespace) {
                    j += 1;
                }
                if b.get(j) == Some(&b':') {
                    if stack.len() == 1 {
                        under = json[at..i].to_string();
                    }
                    if let Some((b'{', keys)) = stack.last_mut() {
                        keys.push(json[at..i].to_string());
                    }
                }
            }
            _ => i += 1,
        }
    }
    out
}

/// Just past the closing quote of the string starting at `at`.
fn end_of_string(b: &[u8], at: usize) -> usize {
    let mut i = at + 1;
    while i < b.len() {
        match b[i] {
            b'\\' => i += 2,
            b'"' => return i + 1,
            _ => i += 1,
        }
    }
    i
}

/// Whether `text` appears as a whole literal, not as a slice of a longer one.
fn contains_literal(json: &str, text: &str) -> bool {
    let boundary = |c: Option<char>| !c.is_some_and(|c| c.is_ascii_alphanumeric() || c == '.');
    json.match_indices(text).any(|(at, _)| {
        boundary(json[..at].chars().next_back()) && boundary(json[at + text.len()..].chars().next())
    })
}

// --- the checks ---------------------------------------------------------

/// The original defect, over the corpus that measured it.
///
/// A preserved span used to be spliced verbatim, so a later build's newlines
/// and indentation landed inside an otherwise compact file and stayed there on
/// every later save.
#[test]
fn no_saved_file_carries_the_older_files_formatting() {
    let mut with_newlines = 0;
    let mut with_whitespace = 0;

    for doc in corpus() {
        let saved = save(&doc.json);
        if saved.contains('\n') {
            with_newlines += 1;
        }
        if insignificant_whitespace(&saved).is_some() {
            with_whitespace += 1;
        }
    }

    println!(
        "{with_newlines}/{DOCUMENTS} saved files carry a raw newline, \
         {with_whitespace}/{DOCUMENTS} carry insignificant whitespace"
    );
    assert_eq!(
        (with_newlines, with_whitespace),
        (0, 0),
        "of {DOCUMENTS} later-build documents, {with_newlines} saved files carry a raw \
         newline and {with_whitespace} carry insignificant whitespace"
    );
}

/// The span exists so the writer never has to decide how a number is
/// spelled, and compaction must not start deciding.
#[test]
fn every_number_is_spelled_the_way_the_file_spells_it() {
    let mut lost = Vec::new();

    for doc in corpus() {
        let saved = save(&doc.json);
        for number in &doc.numbers {
            if !contains_literal(&saved, number) {
                lost.push(number.clone());
            }
        }
    }

    assert!(
        lost.is_empty(),
        "{} numbers were respelled: {lost:?}",
        lost.len()
    );
}

/// The other half. Whitespace inside a string is the file's own value, and
/// the compaction has to step over strings rather than through them.
#[test]
fn whitespace_inside_a_string_is_left_alone() {
    let mut lost = Vec::new();

    for doc in corpus() {
        let saved = save(&doc.json);
        for text in &doc.strings {
            if !saved.contains(text.as_str()) {
                lost.push(text.clone());
            }
        }
    }

    assert!(
        lost.is_empty(),
        "{} strings were rewritten: {lost:?}",
        lost.len()
    );
}

/// This is an assertion that something did NOT happen.
///
/// Compacting a span could just as easily have sorted the objects inside it,
/// and the file would look tidier for it. It must not. A nested object under a
/// key this build has no field for belongs to a later build, and reordering it
/// is the rewrite R1.2 forbids - the same rule that stops a refused file being
/// overwritten at all. So this counts the saved files whose inner key order is
/// NOT sorted and requires that number to stay large.
#[test]
fn a_preserved_objects_inner_key_order_is_left_alone_on_purpose() {
    let mut unsorted = 0;
    let mut checked = 0;

    for doc in corpus() {
        let was = nested_key_orders(&doc.json);
        let mut now = nested_key_orders(&save(&doc.json));
        // searchEngines is a typed field this build serializes itself, not a
        // preserved span - its nested order is this build's own business.
        now.remove("\"searchEngines\"");
        assert_eq!(was, now, "a nested object was reordered\n{}", doc.json);

        if !now.is_empty() {
            checked += 1;
            if now.values().flatten().any(|keys| !keys.is_sorted()) {
                unsorted += 1;
            }
        }
    }

    println!(
        "{unsorted}/{checked} saved files still carry an unsorted nested object, \
         which is the intended outcome"
    );
    assert!(
        unsorted > 0,
        "{checked} documents carried a nested object and none was left unsorted, so this \
         test proves nothing - the corpus stopped generating unsorted objects"
    );
}

/// The property the whole writer is built around: a save the file already
/// agrees with rewrites nothing. Splicing raw formatting kept this true - the
/// same bytes went back - but only because the formatting was carried forward
/// forever, which is the defect. Compaction has to reach the fixed point on the
/// FIRST save and stay there.
#[test]
fn a_second_save_writes_the_same_bytes() {
    for doc in corpus() {
        let once = save(&doc.json);
        let twice = save(&once);
        assert_eq!(once, twice, "a save is not a fixed point\n{}", doc.json);
    }
}
