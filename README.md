# Atrium

A Chromium-based browser for macOS, built around spaces instead of tab strips.

This repository is not a fork of Chromium. It holds a patch series, the Rust
core, the build and verification tooling, and the docs. You apply it to a
pinned Chromium checkout and build from there.

## License: Apache-2.0

Atrium's own code is licensed under the **Apache License 2.0** (see
[LICENSE](LICENSE)). You may use, modify and redistribute it under those terms.

Publishing the source is about trust, not contributors. A browser sees your
banking, your mail and your passwords. Being able to audit it is close to a
precondition for installing one from a vendor you have never heard of.

### Which license governs which file

Chromium's own code stays **BSD-3-Clause** and cannot be relicensed. A patch
that modifies an upstream Chromium file does not change that file's license.
Wholly new files under Atrium-only paths are Atrium's authorship and carry the
Apache-2.0 header.

[docs/LICENSING.md](docs/LICENSING.md) draws the boundary path by path.
[NOTICE](NOTICE) carries Chromium's required attribution. `make license-verify`
asserts every Atrium-authored file says which license it is under.

## Building

Expect a large checkout and a long first build. See [docs/BUILD.md](docs/BUILD.md).

```
./build/scripts/bootstrap.sh   # fetch Chromium and depot_tools (~1h45m, 111 GB)
make patches                   # apply the Atrium patch series
make build                     # build Atrium.app
make install-native-hosts      # let 1Password and other native hosts reach Atrium
```

`make help` lists every target.

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md) first. Contributions are accepted under
the Developer Certificate of Origin, and inbound contributions are licensed to
the project under the same Apache-2.0 terms.

## Command palette

Cmd+T opens the palette. Type any of the words below to reach a verb; the rest of
what you type goes to the address bar and search as usual.

A click on the sidebar's address row, or on the floating bar's URL, opens the
same palette on top of that row.

| Verb | Words that find it |
|---|---|
| Capture Viewport | `visible` |
| Capture Region | `crop`, `area`, `select` |
| Capture Full Page | `whole`, `full` |
| Settings | `settings`, `preferences` |
| Courtyard | `courtyard`, `spaces` |
| Overview | `overview` |
| New Space | `new space`, `space` |
| Toggle Sidebar | `toggle sidebar`, `sidebar` |

`screenshot`, `shot`, `grab` and `snap` find all three capture verbs, so one word
offers the choice. Two letters is the shortest query that offers a verb, and a
verb bound to a shortcut shows it at the right of its row.

A capture is copied to the clipboard and saved to your Downloads folder as
`Atrium Screenshot <date> at <time>.png`. Region capture dims the page and waits
for you to drag a rectangle; Escape cancels.

## Repository layout

| Path | What it holds |
|---|---|
| `patches/` | The Chromium patch series, one file per change |
| `rust/atrium-core/` | The model core, shared by every surface over a C ABI |
| `build/scripts/` | Build, verification, VM and probe tooling |
| `docs/` | Architecture, strategy, build and release notes |
| `fixtures/` | Test fixtures |
