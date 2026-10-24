# Contributing to Atrium

## Licensing your contribution

Atrium is licensed under the Apache License 2.0. Read [LICENSE](LICENSE) before
you start.

By opening a pull request you agree to two things:

1. **Developer Certificate of Origin.** You certify [DCO 1.1](https://developercertificate.org/)
   for every commit. Sign off each one:

   ```
   git commit -s -m "fix: ..."
   ```

   The sign-off line must carry your real name and a working email address.

2. **Inbound licensing.** You license your contribution to the project under
   the Apache License 2.0, the same terms the project ships under.

You keep the copyright in what you write. The DCO transfers no ownership. It
records that you had the right to send the code and that you are happy for it
to ship under these terms.

If you cannot agree to both, open an issue describing the change instead of a
pull request. A described bug is still useful.

## License headers

Every new Atrium-authored source file carries the Apache-2.0 header.
Add it with:

```
make license-fix
```

`make license-verify` fails if a file is missing one, and fails again if a
file in `build/scripts/license-header-allowlist.json` gains a header without
its allowlist entry being removed.

Do not add the header to a file that already existed in upstream Chromium.
Those stay BSD-3-Clause. [docs/LICENSING.md](docs/LICENSING.md) draws the
boundary.

## Lint

`make lint` requires ruff 0.16.3 on `PATH` and fails with a clear message on
any other version, so an editor-managed ruff upgrade cannot silently change
which rules it enforces. The rule set itself lives in `ruff.toml`.

## Before you open a pull request

| Check | Command |
|---|---|
| WebUI suites and drift guards | `make webui-test` |
| Rust core | `make rust-test` |
| License headers | `make license-verify` |
| Patch series matches the tree | `make patches-verify` |
| Style (shellcheck, ruff, rustfmt) | `make lint` |

A pull request from a fork gets no CI. The runners are one maintainer's Mac,
holding the Chromium checkout and the signing certificate, so they refuse
fork code. Run the checks above yourself and say in the PR that you did.

Do not test the UI on your own desktop session - use the VM harness
(`make vm-start`, `make vm-deploy`, `make vm-scenario`). See
[docs/BUILD.md](docs/BUILD.md).

## What makes a change easy to accept

- One change per pull request.
- A test that fails without the fix. Reverting your change should turn a named
  test red.
- Conventional commit subjects (`feat:`, `fix:`, `refactor:`, `docs:`,
  `test:`, `chore:`, `perf:`, `ci:`), under 72 characters, imperative mood.
- The body explains why. The diff already shows what.

## Reporting a security issue

Do not open a public issue. See [docs/SECURITY.md](docs/SECURITY.md).
