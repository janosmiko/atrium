# CI

Three tiers, split by what each gate needs. GitHub-hosted runners cannot build
Chromium: the checkout is 121 GB plus a multi-GB `out/`, hosted macOS runners
give roughly 14 GB free, and a fresh `gclient sync` is about 100 GB of download
per run. The 6-hour job limit is the second blocker, not the first.

| Tier | Workflow | Trigger | Needs | Time |
|---|---|---|---|---|
| 1 | `tier1-repo.yml` | every push and PR | node, rust | seconds |
| 2 | `tier2-chromium-gates.yml` | every push and PR | the Chromium checkout, no build | minutes |
| 3 | `tier3-build.yml` | manual or nightly | a full build | hours |

## Tier 1 - repo gates

`rust-test`, `rust-corpus`, `lab-probe` and the four selftests. It points
`ATRIUM_CHROMIUM_SRC` at an empty directory and asserts no Chromium tree is in
reach, so a gate cannot pass by quietly reading the host checkout.

Isolation comes from the runner's own `_work` clone, not from a VM. The Tart VM
is still the only place GUI probes and browsertests run - `browser_tests` puts
real windows on the desktop on macOS.

## Tier 2 - Chromium gates

`webui-test`, `tokens-verify`, `license-verify`, `codec-posture`,
`linux-verify` and `patches-verify`. These read the one Chromium checkout this
machine has, which is the same tree you build in. A second clone does not fit.

Two things follow, and both look like bugs the first time:

- A red run can mean the host tree is mid-edit, not that the push broke
  anything. `patches-verify` compares the series against the tree, so any lane
  with uncommitted work turns it red.
- Only one tier 2 job runs at a time. Two jobs reading the same checkout while
  it changes give two answers and neither is worth reading.

`gn gen` writes to `out/linuxgen-ci`, clear of `out/atrium`, so a tier 2 run
cannot disturb a build in flight.

## Tier 3 - build

Manual dispatch, plus a nightly schedule. `build-shell.sh` caps jobs at
`ncpu-4` on purpose, to leave the machine usable during a long build. Running
that on every push undoes the trade.

Browsertests are off by default. Turn them on with the `run_browsertests` input
when you dispatch.

## Registering the runner

None of this runs until a self-hosted runner exists. The repo has none.

```
# 1. Get a registration token (expires in an hour)
gh api -X POST repos/janosmiko/atrium/actions/runners/registration-token --jq .token

# 2. Install the runner. Pick a directory outside this repo and outside $SRC.
mkdir -p ~/actions-runner && cd ~/actions-runner
curl -o runner.tar.gz -L https://github.com/actions/runner/releases/latest/download/actions-runner-osx-arm64.tar.gz
tar xzf runner.tar.gz

# 3. Configure. The labels decide which tiers this runner accepts.
./config.sh --url https://github.com/janosmiko/atrium \
            --token <TOKEN FROM STEP 1> \
            --labels macOS,chromium-checkout

# 4. Run it as a service
./svc.sh install && ./svc.sh start
```

The `chromium-checkout` label is what tier 2 and tier 3 select on. A runner
without it takes tier 1 only. Drop the label if you ever add a second machine
that has no Chromium tree.

The runner executes code from this repository on your machine. That is the
point, and it is also the risk: a workflow change is a code-execution change.
Review workflow diffs the way you review anything else that runs locally.

## Fork pull requests

Tier 1 and tier 2 refuse a pull request from a fork:

```yaml
if: >-
  github.event_name != 'pull_request' ||
  github.event.pull_request.head.repo.full_name == github.repository
```

A fork PR brings the fork's own workflow file with it. Without that guard,
anyone who opens one runs their code on this Mac, next to the Chromium tree
and the Developer ID certificate. The repo is private today, so no fork can
exist. The guard has to be in place before the repo goes public, not after.

Also turn on **Require approval for all outside collaborators** in
Settings > Actions. The guard skips the job, the setting stops the run from
being queued at all.

Three more things matter because the runner is a long-lived machine rather
than a throwaway VM:

- Actions are pinned to a commit SHA, not a tag. A tag can be moved to point
  at different code after review.
- `persist-credentials: false` on every checkout. Otherwise the token is
  written into `.git/config`, where it outlives the job on a runner whose
  disk is not wiped.
- `permissions: contents: read` on every workflow. The default token can
  write to the repository, and none of these jobs needs to.

A contributor's PR therefore gets no CI until a maintainer pushes the branch
to this repository. That is the trade: a fork PR costs a maintainer one push,
and the alternative costs arbitrary code execution on a signing machine.

## Release

`release-build.yml` runs on a `v*` tag or a manual dispatch, on hosted macOS
runners. `prepare` fetches and patches Chromium, then up to 8 chained stages
resume ninja from a checkpoint artifact. A manual dispatch stops there and
uploads an ad-hoc signed `atrium-macos-arm64` zip.

On a `v*` tag two more jobs run:

| Job | Does | Needs |
|---|---|---|
| `sign` | Developer ID signs the app, notarizes and staples it, builds the DMG, signs, notarizes and staples that | `release` environment and its secrets |
| `release` | Creates the GitHub Release with the DMG | `contents: write`, no secrets |

A tag must point at a commit on `main`. `prepare` and `sign` both check it
with `ci-release.sh verify-tag`. The `sign` job checks the SHA-256 of the app
artifact against the digest the build job reported, and never runs the app.

Setup in the repository settings:

- Environment `release`: a required reviewer, deployment tags `v*` only, and
  six secrets: `MACOS_CERT_P12_BASE64`, `MACOS_CERT_P12_PASSWORD`,
  `MACOS_SIGN_IDENTITY`, `NOTARY_API_KEY_P8`, `NOTARY_API_KEY_ID`,
  `NOTARY_API_ISSUER_ID`. The notary key is an App Store Connect team key.
- A tag ruleset on `v*` that restricts create, update and delete.
- A ruleset on `main` with no force push, no delete, pull requests required.
- Default workflow token permission set to read-only.

Approve the `release` environment within 7 days, or the app artifact
expires. `build/signing/*.plist` are copies of `chrome/app/` entitlements.
`prepare` fails when they differ after a Chromium bump. The first signed
release needs one manual launch on a clean Mac, because a wrong helper
entitlement breaks the renderer and only a real launch shows it.

## Later

A dedicated Mac mini pays for itself against cloud Mac rental in roughly two
months, once the Chromium gates run often enough to compete with interactive
work. AWS EC2 Mac is a dedicated host with a 24-hour minimum allocation.
