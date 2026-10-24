# Licensing

Atrium's own code ships under Apache-2.0. Chromium's own code stays BSD-3-Clause and cannot be relicensed.

Auditability is close to a precondition for adopting a browser from an unknown vendor. The CRA open-source exemption does not survive commercial activity, so the legal burden is the same under any license. The Widevine CDM ships in official binaries only, never in source builds. This matches upstream Chromium.

## The Apache-2.0/BSD-3 boundary, by directory

This repository (`atrium`, the outer checkout) and the Chromium checkout it patches ($SRC) are two different trees. The tables below answer "which license governs which file" for both.

In this repository:

| Path | License | Why |
|---|---|---|
| `patches/*.patch` | BSD-3-Clause for edits to existing Chromium files, Apache-2.0 for wholly new Atrium files | The patch series is tracked here (`make patches-regen`). An edit to an existing BSD-3 file does not relicense that file. A patch that adds a wholly new file under `chrome/**/atrium/` is entirely Atrium's authorship. |
| `rust/atrium-core/**` | Apache-2.0 | Atrium's own crate, built standalone or pulled into the Chromium build via GN. No Chromium code in this tree. |
| `build/**` (scripts, `args.gn`, test harnesses, probes) | Apache-2.0 | Atrium's own build and verification tooling. |
| `docs/**`, `NOTICE`, `LICENSE`, `Makefile` | Apache-2.0 (docs are covered by the license, not licensed content in the code sense) | Project documentation authored for Atrium. |
| `fixtures/**` | Apache-2.0 unless a fixture is itself a vendored third-party asset - none found as of this audit | Test fixtures authored for Atrium's own tests. |

In the Chromium checkout the patch series applies to ($SRC, not tracked in this repo):

| Path pattern | License |
|---|---|
| Any file already present in upstream Chromium, patched or not | BSD-3-Clause, unchanged. A patch modifying such a file does not relicense it - see the header status below. |
| New files added by our patches under `chrome/browser/ui/atrium/`, `chrome/browser/resources/atrium/`, and similar Atrium-only paths | Apache-2.0 - these did not exist upstream, so Atrium is the sole author. |
| The generated third-party notice set (`chrome://credits`, `about_credits.html`) and the several hundred components it lists | Each component's own license, reproduced verbatim by `tools/licenses/licenses.py`. See `NOTICE`. |

**Header status (measured 2026-10-24).** `patches/` adds 4 new files. Three are Atrium-owned (`content/shell/browser/atrium_bridge.h`, `atrium_tab_host.h`, `atrium_tab_host.mm`) and carry no header. The fourth is a Chromium-style unit test with a Chromium header. Other new Atrium files live in `overlay/` and carry the Apache-2.0 header.

## The sync server's license is open

Apache-2.0 covers Atrium's own browser code. It does not automatically cover the sync server. The server is a component we deliberately hand to customers to run themselves.

The trade-off, as it stands:

- **Apache-2.0 for the server too** keeps one license to explain. It makes self-hosting an easy yes and makes the wire format auditable by the customer running it. It also hands a competitor the server half, though the server is the cheap half under an end-to-end design: the client carries the protocol, crypto and conflict resolution.
- **A protective license for the server** (BSL or similar) guards the server half. It asks an enterprise buyer to run a non-open-source server inside their own infrastructure, which is the conversation self-hosting was meant to avoid.
- **Splitting the crate from the server** is a third option: Apache-2.0 on the shared protocol crate so both sides can be verified against it, a protective license on the server.

This needs deciding before the sync server ships, not before it is written.
