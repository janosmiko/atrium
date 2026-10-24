# Security

Licensing context: [LICENSING.md](LICENSING.md).

## Why this file exists

Atrium carries a patch stack against upstream Chromium, which ships stable updates
every four weeks plus out-of-band security releases. A user running a stale Atrium
is running code with known, public exploits. Once Atrium is distributed - and
especially once it is sold - the EU Cyber Resilience Act (CRA) makes that an
obligation, not just a risk. See "EU CRA obligations" below for the dates.

## Reporting a vulnerability

Report a vulnerability through GitHub private vulnerability reporting:
<https://github.com/janosmiko/atrium/security/advisories/new>.

1. Report privately. Do not open a public GitHub issue for an unpatched
   vulnerability.
2. Include: affected version (`build/VERSION`'s `chromium_tag` plus Atrium's own
   commit/tag), reproduction steps, and impact.

## Phishing and malware protection

**Chosen 2026-08-14: Google Safe Browsing, with real API keys.** Chromium already
implements the client, so this is keys and configuration rather than a build.

**What this costs the user, stated plainly because it cuts against the local-first
pitch:** Safe Browsing lookups tell Google something about what the user visits. The
protocol sends hash prefixes rather than full URLs for the local-list path, so it is
not a feed of browsing history, but it is not nothing either. A user who would rather
send Google nothing must be able to turn it off, which is why that toggle is part of
this work rather than a later nicety.

**Today Atrium ships with no protection at all.** `safe_browsing_mode` defaults to 1,
so the code and the interstitial are compiled in, but `v4_protocol_config.cc` calls
`google_apis::GetAPIKey()` with no presence guard. With no key the update URL carries
an empty `key_param`, Google's servers never populate the local hash-prefix database,
and `V4LocalDatabaseManager` reports every URL as safe. The interstitial exists and
can never fire.

**Keys are secrets.** They come from the environment or a secret manager and are never
committed. `build/args.gn` sets no `google_api_key` and must not gain one - Google's
Chromium API key terms restrict those keys to personal and development use, explicitly
not redistribution, so a committed key would be both a leak and a licence breach.

## Upstream rebase cadence and maximum acceptable lag

The mechanics (check, rebase, sync, finish) are documented in
`docs/BUILD.md`, "Pulling Chromium stable updates". This section adds the cadence
commitment CRA compliance needs:

| Update type | Maximum acceptable lag | Rationale |
|---|---|---|
| Same-milestone (Chromium security release) | 7 days from upstream tag to a rebased, tested `atrium/<tag>` branch | Same-milestone bumps are low-risk (`update-chromium.sh check` says so) and these are the releases that patch actively-exploited bugs |
| Milestone jump (~every 4 weeks) | Never more than one Chromium milestone behind | Skipping two milestones compounds patch-stack drift and rebase risk; one milestone of headroom keeps a single jump tractable |

**Current lag is measurable:** `make update-check` compares `build/VERSION`'s pinned
`chromium_tag` against ChromiumDash's latest stable and prints both, exiting 10 when
an update is available. Run it, or wire it into CI on a schedule, to get the current
lag at any time - it does not need a checkout to run past the "check" step.

## SBOM

`make sbom` (`build/scripts/generate-sbom.sh`) generates an SPDX 2.2 document listing every
`third_party/` package in the Chromium checkout, plus the vendored `atrium-core`
Rust crate, at `build/sbom/atrium.spdx.json` (gitignored - regenerate per release,
do not commit it). It reuses Chromium's own `tools/licenses/licenses.py`, the same
scanner behind `about://credits`, so the SBOM and the in-app credits page can never
disagree about what shipped.

Regenerate on every release cut and attach the file to the release, alongside the
release notes below.

## Release notes

`docs/RELEASE-NOTES.md` is the running log. Every entry names the upstream Chromium
version (`chromium_tag`) and lists the security fixes that version carries, as the
release checklist requires. See that file for the entry template.

## Auto-update channel

Not implemented yet. The auto-update requirement needs a signed update channel (Sparkle or equivalent)
verified end to end on a clean machine - both the implementation and the
verification are out of scope for this pass (no Swift/C++ changes, no VM).

## EU CRA obligations

| Obligation | Date | Owner |
|---|---|---|
| Report actively-exploited vulnerabilities to ENISA within 24 hours of becoming aware | 11 Sep 2026 | Project maintainer |
| Full obligations: security updates across the expected product lifetime, CE marking, SBOM, coordinated vulnerability disclosure | 11 Dec 2027 | Project maintainer |

The free-and-open-source carve-out in the CRA does not survive commercial activity
([LICENSING.md](LICENSING.md)), so a monetized Atrium is a "manufacturer of a product with
digital elements" under the Act. Both dates land inside this product's likely life,
so this file builds the machinery in rather than retrofitting it.
