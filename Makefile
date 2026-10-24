# Atrium build tasks. Wraps the scripts in build/ - see docs/BUILD.md for details.
# Chromium builds take minutes (incremental) to hours (clean); run them in a
# terminal you can leave open.

# the rest of them. .PHONY was written when this file had about 40
# targets and never grew with it, leaving 48 unlisted - harmless only while no
# file in the repo root happens to share a target name, and a silent skip when
# one does. make unions multiple .PHONY lines, so this is a second line rather
# than a rewrite of the first.
#
# `make phony-verify` fails when a target is missing from here, so the next one
# added does not drift out again.
.PHONY: lint phony-verify tokens tokens-verify lab-probe lab-probe-selftest hooks hooks-selftest install-native-hosts cpp-test rust-coverage swipe-bench swipe-test swipe-probe fold-test drag-test drop-test paging-test reflow-test grid-test courtyard-test rewind-test mirror-test render-test install-test space-menu-test theme-studio-test drag-probe drop-probe paging-probe fav-probe sidebar-regress drag-regress sidebar-diagnose sidebar-watch cold-probe drag-input-probe menu-probe scroll-probe scroll-diagnose swipe-settle-probe wheel-capture favicon-probe vm-cdp webui-preview webui-verify unwired-verify webui-test webui-coverage webui-probe vm-run-gtest vm-run-gtest-selftest

.DEFAULT_GOAL := help

.PHONY: scenario-lib-test help build build-shell codec-posture codec-posture-selftest icon-verify icon-verify-selftest icons icns run run-detached dev-install verify-detach patches patches-regen patches-verify tag-swift corpus-check rust-test rust-corpus rust-verify rust-sync overlay-verify overlay-sync overlay-adopt linux-verify linux-verify-selftest unwired-verify-selftest css-verify css-verify-selftest injected-js-verify license-verify license-verify-selftest license-fix update-check bootstrap all vm-start vm-stop vm-deploy vm-lifetime vm-shell vm-scenario vm-scenario-all vm-scenario-list crash-pick-test file-value-test quit-outcome-test moved-outcome-test reset reset-stores sbom favicon-tint-probe

help: ## Show this help
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-14s\033[0m %s\n", $$1, $$2}'

build: ## Build the Atrium app (chrome target -> Atrium.app)
	./build/scripts/build-shell.sh chrome

build-shell: ## Build the M1 content_shell fallback
	./build/scripts/build-shell.sh content_shell

tokens: ## Regenerate build/generated/tokens.css from the Rust token generator
	./build/scripts/generate-tokens.sh

tokens-verify: ## Drift guard: assert the WebUI tokens.css matches the committed reference
	./build/scripts/verify-tokens.sh

lab-probe: ## Assert the lab's interaction contract headlessly (no deps, no build)
	node build/scripts/lab-probe.mjs

lab-probe-selftest: ## Prove the probe can fail: inverted assertion must exit non-zero
	@node build/scripts/lab-probe.mjs --invert >/dev/null 2>&1 && (echo "SELFTEST FAILED: --invert exited 0" && exit 1) || echo "selftest OK: --invert exits non-zero"

codec-posture: ## Pin the codec-licensing facts (args.gn + Chromium's decoder gating)
	./build/scripts/verify-codec-posture.sh

codec-posture-selftest: ## Prove the codec guard can fail: inverted assertion must exit non-zero
	@./build/scripts/verify-codec-posture.sh --invert >/dev/null 2>&1 && (echo "SELFTEST FAILED: --invert exited 0" && exit 1) || echo "selftest OK: --invert exits non-zero"

icon-verify: ## Assert the BUILT bundle carries Atrium's app.icns, not the stock one (needs make build first)
	./build/scripts/verify-app-icon.sh

icon-verify-selftest: ## Prove the icon guard can fail: inverted assertion must exit non-zero
	@./build/scripts/verify-app-icon.sh --invert >/dev/null 2>&1 && (echo "SELFTEST FAILED: --invert exited 0" && exit 1) || echo "selftest OK: --invert exits non-zero"

hooks: ## Install this repo's git hooks (pre-push patch-series guard)
	./build/scripts/install-hooks.sh

hooks-selftest: ## Prove the pre-push guard can fail: a drifted tree must exit non-zero
	@./build/scripts/verify-hooks.sh

sbom: ## Generate build/sbom/atrium.spdx.json, an SPDX SBOM of every third_party/ package
	./build/scripts/generate-sbom.sh

icons: ## Regenerate app icon variant PNGs from build/icons-src/*.svg (qlmanage)
	./build/scripts/build-icons.sh

icns: ## Compile build/icons-src/*.icon into build/Assets.car and build/atrium.icns (actool)
	./build/scripts/build-assets-car.sh

run: build ## Rebuild and launch Atrium
	./build/scripts/run-atrium.sh $(FLAGS)

run-detached: build ## Rebuild and launch Atrium in the background (returns immediately; tail build/atrium-run.log)
	./build/scripts/run-atrium.sh $(FLAGS) >/dev/null 2>&1 & disown

dev-install: ## Build a standalone Atrium, install it to ~/Applications, and open it (quits a running Atrium)
	./build/scripts/dev-install.sh

verify-detach: ## Regression guard: prove Atrium keeps running unattended (~5 min; needs a build already present)
	./build/scripts/verify-detach-survival.sh

patches: ## Apply the patch series to a clean Chromium checkout (git am)
	./build/scripts/apply-patches.sh

patches-regen: ## Re-export the numbered series from the atrium/<tag> commit stack
	./build/scripts/regen-patches.sh

patches-verify: ## Drift guard: assert patches == branch == working tree == VERSION
	./build/scripts/verify-patches.sh

tag-swift: ## Mark the last commit that still has Swift, in both repos (re-run before the cut)
	./build/scripts/tag-swift.sh

corpus-check: ## Run the golden store corpus against any store implementation (RUNNER=<cmd>)
	./build/scripts/corpus-check.py --roundtrip "$${RUNNER:?set RUNNER to a command that loads a store file and writes it back}"

rust-test: ## Run the atrium-core crate's own tests (no Chromium checkout needed)
	cargo test --manifest-path rust/atrium-core/Cargo.toml

cpp-test: ## Run the atrium C++ gtests (builds Chromium's unit_tests target; FILTER= narrows)
	./build/scripts/cpp-test.sh $(FILTER)

rust-corpus: ## Read the whole golden store corpus through the Rust core
	@cargo build -q --manifest-path rust/atrium-core/Cargo.toml --bin corpus-runner
	@./build/scripts/corpus-check.py --roundtrip rust/atrium-core/target/debug/corpus-runner

rust-coverage: ## rust-test under coverage, gated at 92% lines (ATRIUM_RUST_COVERAGE_MIN overrides)
	@./build/scripts/rust-coverage.sh

rust-verify: ## Drift guard: assert the repo and Chromium copies of atrium-core are identical
	./build/scripts/verify-rust.sh

rust-sync: ## One-step fix for rust-verify: copy the canonical repo crate over the Chromium copy
	./build/scripts/sync-rust.sh

overlay-verify: ## Drift guard: assert overlay/ and the owned roots in the Chromium tree are identical
	./build/scripts/overlay-verify.sh

overlay-sync: ## One-step fix for overlay-verify: copy overlay/ over the owned roots in the Chromium tree
	./build/scripts/sync-overlay.sh

overlay-adopt: ## Rescue an edit made in the Chromium tree by mistake: copy it back into overlay/
	./build/scripts/adopt-overlay.sh

linux-verify: ## Drift guard: gn gen for target_os=linux, and no Objective-C in the Linux atrium sources
	./build/scripts/verify-linux-gen.sh

linux-verify-selftest: ## Prove linux-verify can fail: a dirty source list must exit non-zero (no gn, no checkout)
	./build/scripts/verify-linux-gen.sh --selftest

swipe-bench: ## Measure the HTML sidebar's swipe frame timings (needs --remote-debugging-port=9333)
	node ./build/scripts/swipe-bench.mjs

swipe-test: ## Drive the HTML sidebar's swipe machine headlessly (no build, no browser)
	node --test ./build/scripts/swipe-machine-test.mjs

swipe-probe: ## Assert the swipe against a running Atrium (VM only; use port 9334 so it cannot collide with a local Atrium)
	node ./build/scripts/swipe-gesture-probe.mjs

fold-test: ## Drive the HTML sidebar's folder open/closed state headlessly (no build, no browser)
	node --test ./build/scripts/fold-state-test.mjs

drag-test: ## Drive the HTML sidebar's row-drag maths AND wiring headlessly (no build, no browser)
# row-drag-install-test.mjs was in no target at all, so nothing ran it. It
# caught a real regression the moment it was run.
	node --test ./build/scripts/row-drag-test.mjs ./build/scripts/row-drag-install-test.mjs ./build/scripts/parked-snapshot-drag-test.mjs

drop-test: ## Drive the HTML sidebar's cross-section drop decision headlessly (no build, no browser)
	node --test ./build/scripts/drop-decision-test.mjs

paging-test: ## Drive the HTML sidebar's drag edge-paging timing headlessly (no build, no browser)
	node --test ./build/scripts/edge-paging-test.mjs

reflow-test: ## Drive the HTML sidebar's live-reflow order math headlessly (no build, no browser)
	node --test ./build/scripts/reflow-test.mjs

grid-test: ## Drive the HTML favourites grid's nearest-slot geometry headlessly (no build, no browser)
	node --test ./build/scripts/grid-drop-test.mjs

courtyard-test: ## Drive the Courtyard's session model headlessly (no build, no browser)
	node --test ./build/scripts/courtyard-test.mjs

rewind-test: ## Drive Rewind's snapshot ring, scrubber summary and scoped restore headlessly (no build, no browser)
	node --test ./build/scripts/rewind-test.mjs

mirror-test: ## Drive Mirror View's profile choice list headlessly (no build, no browser)
	node --test ./build/scripts/mirror-test.mjs

render-test: ## Drive the HTML sidebar's per-space render and rail panes headlessly (no build, no browser)
	node --test ./build/scripts/sidebar-render-test.mjs

install-native-hosts: ## Link Chrome's native messaging manifests (1Password, ...) into Atrium's folder
	./build/scripts/link-native-hosts.sh

install-test: ## Drive the HTML sidebar's install wiring and chrome.send traffic headlessly (no build, no browser)
	node --test ./build/scripts/sidebar-install-test.mjs

space-menu-test: ## Drive the sidebar's empty-space and favourite-tile context menus headlessly (no build, no browser)
	node --test ./build/scripts/sidebar-space-menu-test.mjs

theme-studio-test: ## Drive the settings-pane Theme Studio module and its colour picker headlessly (no build, no browser)
	node --test ./build/scripts/theme-studio-test.mjs ./build/scripts/theme-picker-test.mjs

drag-probe: ## Assert a real row drag reorders against a running Atrium (VM only, port 9334)
	node ./build/scripts/row-drag-probe.mjs

drop-probe: ## Assert real pin/unpin/favourite drops against a running Atrium (VM only, needs a clean fixture)
	node ./build/scripts/row-drop-probe.mjs

paging-probe: ## Assert drag edge-paging and a cross-space drop against a running Atrium (VM only, needs 2+ spaces)
	node ./build/scripts/edge-paging-probe.mjs

fav-probe: ## Assert a real favourite-tile drag-reorder against a running Atrium (VM only, needs 3+ favourites)
	node ./build/scripts/fav-drag-probe.mjs

sidebar-regress: ## Run every ported sidebar interaction against one build (VM only; rewrites the VM's spaces.json)
	./build/scripts/sidebar-regression.sh

drag-regress: sidebar-regress ## Alias for sidebar-regress

sidebar-diagnose: ## Read-only: dump what a running Atrium's HTML sidebar is rendering (needs --remote-debugging-port=9333)
	node ./build/scripts/sidebar-diagnose.mjs

sidebar-watch: ## Dump, then record what the sidebar sends when YOU click a tab row (20s)
	node ./build/scripts/sidebar-diagnose.mjs --watch

cold-probe: ## Assert a cold sidebar row reopens its tab when clicked (VM only)
	node ./build/scripts/cold-tab-probe.mjs

drag-input-probe: ## Drag a sidebar row with REAL browser input and report which drag events fire (VM only)
	node ./build/scripts/drag-input-probe.mjs

menu-probe: ## Drive the tab row context menu with a real right-click (VM only)
	node ./build/scripts/menu-probe.mjs

scroll-probe: ## Assert only the tab sections scroll and a swipe locks them (VM only, needs 12+ tabs)
	node ./build/scripts/scrollzone-probe.mjs

scroll-diagnose: ## Read-only: scroll the sidebar yourself and see what actually moved (needs --remote-debugging-port=9333)
	ATRIUM_CDP_PORT=9333 node ./build/scripts/scroll-diagnose.mjs

swipe-settle-probe: ## Send a wheel flick and check the rail lands on one space and stays (VM only, needs 3+ spaces)
	node ./build/scripts/swipe-settle-probe.mjs

wheel-capture: ## Record a REAL trackpad flick's wheel stream from a running Atrium (read-only; needs --remote-debugging-port=9333)
	ATRIUM_CDP_PORT=9333 node ./build/scripts/wheel-capture.mjs

favicon-probe: ## Assert real favicons in the HTML sidebar (VM only; serves its own page, port 9334)
	node ./build/scripts/favicon-probe.mjs

favicon-tint-probe: ## Assert a loaded favicon leaves no tint behind it (VM only; serves a transparent icon, port 9334)
	node ./build/scripts/favicon-tint-probe.mjs

vm-cdp: ## Forward the probe VM's DevTools port here, killing whatever holds it first
	./build/scripts/vm-cdp.sh

webui-preview: ## Open a WebUI chrome route in a browser to look at it (ROUTE=sidebar|foyer)
	open "$$(node ./build/scripts/webui-preview.mjs $${ROUTE:-sidebar})"

webui-verify: ## Drift guard: assert every chrome.send name is registered (unregistered = FATAL browser crash)
	./build/scripts/verify-webui-messages.py

unwired-verify: ## Drift guard: window.atrium* receivers have a native caller, JS modules are registered+imported, CSS is linked
	./build/scripts/verify-unwired-paths.py

unwired-verify-selftest: ## Prove unwired-verify can fail: an allowlisted module gaining an importer must exit non-zero
	./build/scripts/verify-unwired-paths.py --selftest

css-verify: ## Drift guard: every WebUI stylesheet parses, so no rule is silently dropped
	./build/scripts/verify-webui-css.py

css-verify-selftest: ## Prove css-verify can fail: a nested comment opener must exit non-zero
	./build/scripts/verify-webui-css.py --selftest

injected-js-verify: ## Drift guard: injected JS survives a document with no root element
	./build/scripts/verify-injected-js.py

license-verify: ## Drift guard: every Atrium-authored source file carries the Apache-2.0 header
	./build/scripts/verify-license-headers.py

license-verify-selftest: ## Prove license-verify can fail: a file with no header must exit non-zero
	./build/scripts/verify-license-headers.py --selftest

license-fix: ## Write the Apache-2.0 header into any Atrium-authored file missing one
	./build/scripts/verify-license-headers.py --fix

webui-test: ## Every WebUI unit suite and drift guard in one go (no build, no browser)
	@node --test ./build/scripts/*-test.mjs
	@./build/scripts/overlay-literal-gate.sh
	@./build/scripts/build-shell-test.sh
	@./build/scripts/overlay-verify-test.sh
	@./build/scripts/verify-patches-test.sh
	@./build/scripts/verify-webui-messages.py
	@./build/scripts/verify-webui-messages.py --selftest
	@./build/scripts/verify-tokens.sh
	@./build/scripts/verify-unwired-paths.py
	@./build/scripts/verify-unwired-paths.py --selftest
	@./build/scripts/verify-webui-css.py
	@./build/scripts/verify-webui-css.py --selftest
	@./build/scripts/verify-injected-js.py

webui-coverage: ## webui-test under coverage, gated at 80% lines (ATRIUM_COVERAGE_MIN overrides)
	@./build/scripts/webui-coverage.sh

webui-probe: ## Every VM probe (VM Atrium with --atrium-html-surfaces=sidebar --remote-debugging-port=9334, then ATRIUM_CDP_PORT=9334)
	@node ./build/scripts/swipe-gesture-probe.mjs
	@node ./build/scripts/favicon-probe.mjs
	@node ./build/scripts/favicon-tint-probe.mjs
	@node ./build/scripts/resizer-input-probe.mjs

update-check: ## Report whether a newer Chromium stable is available (ChromiumDash)
	./build/scripts/update-chromium.sh check

bootstrap: ## One-time Chromium checkout setup (see docs/BUILD.md first)
	./build/scripts/bootstrap.sh

all: build ## Build the Atrium app

vm-start: ## Boot the atrium-probe VM headless and wait for SSH
	./build/scripts/vm.sh start

vm-stop: ## Stop the atrium-probe VM
	./build/scripts/vm.sh stop

vm-deploy: ## rsync the built app into the VM
	./build/scripts/vm.sh deploy

vm-lifetime: ## How long does the deployed Atrium stay up unattended? (one number, ~4s)
	./build/scripts/vm-lifetime.sh

vm-shell: ## SSH into the atrium-probe VM
	./build/scripts/vm.sh shell

vm-run-gtest: ## Run a gtest binary in the VM with the live checkout mounted (BIN=browser_tests FILTER=Suite.Case); single-window only, see docs/BUILD.md
	./build/scripts/vm.sh run-gtest $(BIN) --gtest_filter=$(FILTER)

vm-run-gtest-selftest: ## Prove run-gtest can fail: a failed test and a filter matching nothing must exit non-zero
	./build/scripts/vm.sh run-gtest-selftest

vm-scenario: ## Run one asserting VM scenario (SCENARIO=presenter-startup). Needs a live VM + clean snapshot.
	./build/scripts/vm-scenario.sh $(SCENARIO)

vm-scenario-all: ## Run every build/scenarios/*.json; non-zero exit if any assertion fails
	./build/scripts/vm-scenario.sh all

vm-scenario-list: ## List available VM scenarios
	./build/scripts/vm-scenario.sh --list

# severity=warning, not the default. The five remaining findings are
# NOTES - two functions a test file defines and calls indirectly, a sed that
# prefixes every line where parameter expansion cannot, a grep|wc whose pipeline
# grep -c would change, and one deliberate client-side splice in a python template.
# Each is a considered shape rather than an oversight, so the gate fails on warnings
# and errors and reports notes without blocking. Run plain `shellcheck` to see them.
# pin so a ruff upgrade cannot change what lint enforces.
REQUIRED_RUFF_VERSION := 0.16.3

lint: ## shellcheck over .sh, ruff over .py, rustfmt over the crate (no build, no VM)
	@command -v shellcheck >/dev/null || { echo "lint: shellcheck not on PATH"; exit 1; }
	@command -v ruff >/dev/null || { echo "lint: ruff not on PATH"; exit 1; }
	@[ "$$(ruff --version | awk '{print $$2}')" = "$(REQUIRED_RUFF_VERSION)" ] || \
		{ echo "lint: ruff $(REQUIRED_RUFF_VERSION) required, found $$(ruff --version) - see CONTRIBUTING.md"; exit 1; }
	shellcheck --severity=warning $$(git ls-files '*.sh')
	ruff check $$(git ls-files '*.py')
# --check rather than a plain `cargo fmt`, so the gate REPORTS drift instead of
# quietly fixing it - a formatting rewrite that lands inside an unrelated commit
# is how six files drifted unnoticed before this line existed.
	cargo fmt --manifest-path rust/atrium-core/Cargo.toml --check

phony-verify: ## Drift guard: every Makefile target is named in .PHONY
	@python3 -c 'import re,sys; \
lines=open("Makefile").read().split("\n"); \
t=[m.group(1) for l in lines for m in [re.match(r"^([a-z][a-z0-9_-]*):", l)] if m]; \
p=set(w for l in lines if l.startswith(".PHONY:") for w in l.split(":",1)[1].split()); \
missing=[x for x in dict.fromkeys(t) if x not in p]; \
sys.exit("phony-verify: missing from .PHONY: " + " ".join(missing)) if missing else print("verify OK: all " + str(len(set(t))) + " targets are .PHONY")'

scenario-lib-test: ## Every build/scenarios/lib unit test, python and shell (no VM, no build)
	python3 -m unittest discover -s build/scenarios/lib -p "*_test.py"
	./build/scenarios/lib/asserts_shell_test.sh
	./build/scenarios/lib/keycombo_shell_test.sh
	bash ./build/scripts/vm-scenario-quoting-test.sh

crash-pick-test: ## Drive the crash_renderer target picker, incl. its refusal to signal the browser (no VM, no build)
	python3 ./build/scenarios/lib/pick_renderer_test.py

file-value-test: ## Drive the file_contains grader, incl. its refusal to pass on a fixture that already held the value (no VM, no build)
	python3 ./build/scenarios/lib/file_value_test.py

quit-outcome-test: ## Drive the quit grader, incl. its refusal to pass a Cmd+Q that only ended on the SIGTERM fallback (no VM, no build)
	python3 ./build/scenarios/lib/quit_outcome_test.py

moved-outcome-test: ## Drive the pixels_changed grader, incl. its refusal of a bar that could never go red (no VM, no build)
	python3 ./build/scenarios/lib/moved_outcome_test.py

reset: ## Move Atrium's persisted state aside for a clean first-run (non-destructive; requires FLAGS=--yes)
	./build/scripts/reset-atrium.sh $(FLAGS)

reset-stores: ## Like reset, but keeps Chromium profiles/logins (spaces/themes/settings/hotkeys/routing only)
	./build/scripts/reset-atrium.sh --stores-only $(FLAGS)
