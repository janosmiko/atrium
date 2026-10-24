# Golden store corpus

> The sidebar model snapshot lives one level up, in `fixtures/sidebar-model`.
> See `SidebarModelSnapshotTests` for what it pins and why.

Real on-disk store files at every schema version Atrium has shipped, plus the
state each one loads to. The Swift stores are the only written record of these
rules today. The port to Rust deletes them, so the corpus has to exist first -
after the cut there is nothing left to compare a new store against.

## Layout

```
manifest.json          every case: which store, which input, which expectation
<store>/<case>.json             the file as it sits on a user's disk
<store>/<case>.expected.json    what the store holds after loading it
```

An input at the current schema has an expectation identical to itself: loading
and writing it back changes nothing. An input at an older schema has an
expectation showing the migrated result, which is the record of what the
migration does.

## Rules

- Fixtures carry no real user data. URLs are `example.com`, profiles are
  `default`, and identifiers are fixed so a re-run produces the same bytes.
- Nothing here is generated at test time. Both files are read, never written,
  unless you are deliberately refreshing them.

## Running the check

`make test` runs `GoldenStoreCorpusTests`, which loads every input through the
matching Swift store, writes the loaded state back out, and compares it to the
expectation.

A second implementation must read these same files. That is the point of the
manifest: the harness is thin and the corpus is data, so the Rust stores can
be held to the same expectations without a fixture being edited to suit them.

## Running it against something that is not Swift

`build/scripts/corpus-check.py` knows nothing about any language. It copies an input,
hands the copy to a command, and compares what comes back:

```
make corpus-check RUNNER=./my-runner
```

The runner is called once per case as `<runner> <store-name> <file>`, and has
to load that file through the named store and write the state back to the same
path.

A runner that does nothing passes the eight cases already at the current schema
and fails the six that need a migration. That is the check working, and it is
how the harness was proved before a second store existed.

A migration does not always mean a schema bump. `settings/v1-memory-saver-never`
is at the current schema and still migrates, because every settings field added
after v1 is optional (R1.3) - so a key can be retired without the version
moving, and the corpus is where that is recorded.

The Rust core is the real second implementation:

```
make rust-corpus        # 14/14
```

## Refreshing an expectation

Only when a migration deliberately changes:

```
ATRIUM_CORPUS_REGENERATE=1 make test
```

Then read the diff. An expectation that changed without a matching change to a
migration is a regression, not a refresh.
