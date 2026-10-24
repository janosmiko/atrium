# rusqlite or Chromium's //sql, for the importers

answer this in writing before any importer code is written.

**Recommendation: neither, for three of the four importers. Read with
Chromium's own importer where one exists, and map in Rust.**

## What the importers actually read

Measured from the Swift sources, not assumed.

| Importer | Reads | SQLite? |
|---|---|---|
| Arc | `StorableSidebar.json` | no |
| Chrome | `Bookmarks` (JSON) | no |
| Safari | `Bookmarks.plist`, History | no SQLite in our reader |
| Firefox | `places.sqlite` | **yes**, and only this one |

`grep -c sqlite` over `ArcImporter.swift`, `ChromeImporter.swift` and
`SafariImporter.swift` returns 0. Only `FirefoxPlacesReader.swift` opens a
database, and it does so through the system `libsqlite3` that macOS already
ships, with `immutable=1` on a copy the caller made first - `places.sqlite` is
locked while Firefox runs.

So the question is not "how do the importers do SQL". It is "how does the ONE
Firefox reader do SQL".

## What Chromium already has

`chrome/utility/importer/` ships `firefox_importer.cc` and `safari_importer.mm`,
and `firefox_importer.cc` already uses `//sql`. They are gated in
`chrome/utility/BUILD.gn` - Firefox on `!is_chromeos`, Safari on `is_mac` - and
`gn ls` shows none of them in our build today, because Atrium does not depend on
that target.

That is the important find. The reading half of three of these importers exists
in the tree, maintained upstream, and already answers the SQL question.

## The three options

| | Cost | What it buys |
|---|---|---|
| **A. Chromium's importer reads, Rust maps** | Depend on an existing target; write the mapping | No new dependency, no SQL in our code, upstream maintains the parsing |
| **B. Chromium's `//sql` from C++, Rust maps** | Write the queries ourselves | Full control of the query; still no new dependency |
| **C. Vendor rusqlite** | New `third_party/rust` crate plus `libsqlite3-sys`, which builds C | SQL in Rust, one language for the whole importer |

## Why A, and what it does not cover

The parsing of another browser's profile is exactly the kind of code that rots:
schema changes, version quirks, locked files, encrypted logins. Chromium
carries that burden already and has for years.

What is NOT in Chromium's importers is the part that is ours: mapping bookmarks
and history into Atrium's spaces, tabs and favourites, with the (profileID,
url) keying and the twelve-per-profile cap. That mapping is pure model logic,
it belongs in Rust, and it needs no database at all - Chromium's importer hands
over structured entries.

**Arc has no upstream importer** and never will. It reads one JSON file, which
Rust does with serde and no new dependency.

## Why not C

Vendoring rusqlite adds `libsqlite3-sys`, which compiles C, into a tree that
already links SQLite through `//sql`. Two SQLite copies in one binary is a
real cost - binary size, and two sets of settings for durability, WAL and
temp-store that have to agree. `//sql` also carries Chromium's own hardening
around corrupt databases, which a fresh rusqlite integration would not.

The one honest argument for C is that a Rust importer is one language end to
end. That is worth something, and it is not worth a second SQLite.

## What this does not decide

Whether Atrium's importers should keep their own UX and mapping at all, or
adopt more of Chromium's import flow. That is a product question. This answers
only the storage-access half, which is what blocked the phase.

## If A turns out to be wrong

The fallback is B, not C. `//sql` is already linked, and writing two queries
against `moz_bookmarks` and `moz_places` is a day, not a dependency.
