#!/usr/bin/env python3
"""Scenario spec parser for the VM scenario runner.

The runner is bash; this turns a declarative JSON scenario into flat,
tab-delimited records bash can iterate without a jq dependency (mirrors the
python3-in-vm.sh pattern already used for `tart list` JSON).

Subcommands:
  parse.py validate <scenario.json>          exit 0 if well-formed, else 1 + reason
  parse.py meta     <scenario.json> <key>    print one metadata value
  parse.py actions  <scenario.json>          one action per line
  parse.py asserts  <scenario.json>          one assertion per line
  parse.py teardown_asserts <scenario.json>  one teardown-phase assertion per line

Record encoding (actions/asserts): TYPE<TAB>key=val<TAB>key=val...
List-valued params (e.g. region) are encoded as comma-joined values.
"""
import json
import os
import re
import subprocess
import sys

import pick_renderer

META_KEYS = ("name", "description", "cannot_pass", "fixture", "themes", "routing", "hotkeys", "seed_prefs", "startup_space", "app_process", "wip",
             "full_keyboard_access", "chromium_flags", "serve_fixtures", "spaces_absent", "import_arc_dir",
             "import_zen_dir", "import_chrome_dir", "import_firefox_dir", "load_extension_dir")
ACTION_TYPES = {"settle", "capture", "key", "type", "click", "move", "resize", "drag", "scroll", "activate",
                # a collapse that does not go through the keyboard, and
                # an eval that polls until a value settles.
                "collapse_sidebar", "eval_until",
                # a wheel over CDP, into the page's widget host.
                "cdp_wheel", "cdp_move",
                "wait_vlog", "key_during_capture", "scroll_during_capture", "reopen",
                # A grip drag filmed while it runs, for a per-frame read of how
                # far the page trails the card edge.
                "drag_during_capture",
                # A drag that doubles back, which one straight `drag` cannot.
                "drag_path",
                # A hover filmed while it runs. The floating panel's reveal has
                # no keyboard trigger, so a plain move-then-capture pair misses
                # its 240ms slide in the ssh gap between them.
                "move_during_capture",
                # A run of presses fired from ONE ssh session, so the gap
                # between them is an in-VM sleep. Chained `click` actions cost a
                # host round trip each and cannot be paced.
                "clicks",
                # the vertical axis. Separate types rather than a dy on
                # `scroll`, mirroring cgevent's own split, so a scenario cannot
                # half-specify a gesture and get a silent horizontal one.
                "vscroll", "vscroll_during_capture",
                # kill one named renderer process. See pick_renderer.py
                # for which process each target name resolves to, and why the
                # browser is not one of them.
                "crash_renderer",
                # ask a surface a question. Everything else here
                # drives input or watches output; this reads the DOM, which is
                # the only way to answer whether a gesture ARRIVED as opposed to
                # whether something visible happened afterwards.
                "eval",
                # stop the app and start it again against the SAME
                # staged files. See vm-scenario.sh's own doc for why not
                # restaging is the entire point.
                "relaunch",
                # press a real modifier once, tap another key
                # several times under it, release the modifier once. `key`
                # sends and releases its modifier PER call, so a scenario
                # chaining several `key` actions cannot express a held cycle
                # (Ctrl-Tab recency cycling's whole contract) - see
                # cgevent.swift's `heldTaps` for the mechanism.
                "key_hold_taps",
                # Write the macOS accent default inside the VM. The System
                # accent follows it live, and no synthetic input can reach the
                # System Settings pane that owns it.
                "system_accent",
                # Switch macOS light/dark with the app running.
                "system_appearance",
                # a link another macOS app hands over. `open -a App
                # <url>` on a running app sends the openURLs Apple Event,
                # which is the entry point the handler hooks - `type`ing a URL into
                # the address bar is a different path entirely and would say
                # nothing about it.
                "open_url",
                # axresize's `pos`. Every Atrium window opens at
                # pos=0,30, so two windows overlap exactly and a click at a
                # fixed coordinate names no particular surface. Resizing does
                # not separate them - the content card moves and the sidebars
                # stay stacked. Moving one does.
                "move_window",
                # cgevent's `rightclick`. A left click is the wrong button for
                # a context menu, and no other action can open one.
                "rightclick",
                # The frontmost app and its focused window's subrole, stored
                # for eval_equals.
                "frontmost"}
ASSERT_TYPES = {"ax_window_count",
                # how many windows are built around a given chrome
                # route. A count of windows cannot tell a little window from a
                # space window - both are AXStandardWindow, both ~1024x680,
                # both "Untitled" - so the check needs the kind, not the number.
                "ax_window_kind",
                "ax_window_subrole",
                # compares a value an `eval` action stored. The
                # action refuses to store anything when it could not look, so a
                # missing value fails loudly rather than comparing as empty.
                "eval_equals", "eval_changed",
                "theme_changed", "region_stable", "resize_applied",
                # the position half of resize_applied, for a scenario
                # that has to say WHICH window a click lands on.
                "window_moved",
                "quit", "vlog",
                "vlog_absent", "vlog_after", "vlog_count", "region_mean",
                # per-pixel movement, for what a region MEAN cannot
                # see. See moved_outcome.py for the two ways a mean has already
                # read zero for behaviour that worked.
                "pixels_changed",
                # A before/after variance ratio, for what region_mean's
                # brightness bound cannot separate from a tint. See
                # assert_variance_collapsed in lib/asserts.sh.
                "variance_collapsed",
                # Two regions read alike in EVERY frame of a capture, for a
                # crossfade where the last frame alone is the settled state.
                "regions_track",
                # the largest jump between two frames in a row, for
                # a backdrop that flips while its mean over the burst looks fine.
                "region_steady",
                # region_mean on every frame, for a flash at first paint that
                # is gone by the last frame.
                "region_every_frame",
                # where a band ends, for a width that has no mean.
                # Two bands of different widths average alike when the colour
                # past the step is the same either way.
                "band_step",
                # read a value back out of a staged file. See
                # file_value.py for what it compares and why.
                "file_contains",
                # several captured values across the run log are
                # pairwise distinct - needs a sequential scan, not
                # vlog_after's single-line grep.
                "recency_walk_distinct"}

# Which staged file a `file_contains` may read, and the metadata key that puts
# it in the VM. Named rather than given as a remote path: a path lets a
# scenario read something this harness never staged, and there would then be no
# host-side copy to tell a write from the fixture it started as.
#
# `spaces` maps to `fixture`, which validate() already requires, so it is always
# available; the rest only exist when the scenario declares them.
STAGED_FILES = {"spaces": "fixture", "settings": "settings", "themes": "themes",
                "hotkeys": "hotkeys", "routing": "routing"}

# The prefix of the lines the HARNESS itself writes into the assembled run log
# (the relaunch boundary). A pattern gating on one of those is findable by
# construction, so the source sweep below must not report it as a line someone
# deleted. Kept in sync with RELAUNCH_MARKER in vm-scenario.sh.
HARNESS_LINE_PREFIX = "atrium harness:"


def load(path):
    with open(path, encoding="utf-8") as fh:
        return json.load(fh)


def encode(record):
    """dict -> TYPE<TAB>k=v... ; list values become comma-joined."""
    parts = [record["type"]]
    for key, val in record.items():
        if key == "type":
            continue
        # file_contains compares a JSON VALUE, so its expected one crosses to
        # bash as JSON text rather than str(). str(True) is "True", which no
        # JSON file on earth holds, and str(None) is "None" - both would arrive
        # as a mismatch the scenario author never wrote.
        if record.get("type") == "file_contains" and key == "value":
            val = json.dumps(val)
        elif isinstance(val, list):
            val = ",".join(str(v) for v in val)
        parts.append(f"{key}={val}")
    return "\t".join(parts)


def validate(spec):
    errors = []
    if not spec.get("name"):
        errors.append("missing 'name'")
    if not spec.get("fixture"):
        errors.append("missing 'fixture' (spaces JSON)")
    if not isinstance(spec.get("actions"), list) or not spec["actions"]:
        errors.append("missing/empty 'actions'")
    if not isinstance(spec.get("assertions"), list) or not spec["assertions"]:
        errors.append("missing/empty 'assertions'")
    for i, act in enumerate(spec.get("actions", [])):
        t = act.get("type")
        if t not in ACTION_TYPES:
            errors.append(f"action[{i}]: unknown type {t!r} (want {sorted(ACTION_TYPES)})")
        if t == "capture" and not act.get("id"):
            errors.append(f"action[{i}]: capture needs an 'id'")
        if t == "key" and not act.get("combo"):
            errors.append(f"action[{i}]: key needs a 'combo'")
        if t == "type" and not act.get("text"):
            errors.append(f"action[{i}]: type needs a 'text'")
        if t == "click" and not act.get("from_eval") and (
                act.get("x") is None or act.get("y") is None):
            errors.append(f"action[{i}]: click needs 'x' and 'y'")
        if t == "rightclick" and not act.get("from_eval") and (
                act.get("x") is None or act.get("y") is None):
            errors.append(f"action[{i}]: rightclick needs 'x' and 'y'")
        if t == "move" and (act.get("x") is None or act.get("y") is None):
            errors.append(f"action[{i}]: move needs 'x' and 'y'")
        if t == "system_accent" and act.get("value") in (None, ""):
            errors.append(f"action[{i}]: system_accent needs a 'value'")
        if t == "system_appearance" and act.get("value") not in ("dark", "light"):
            errors.append(f"action[{i}]: system_appearance needs 'dark' or 'light'")
        if t == "eval" and (not act.get("id") or not act.get("expression")):
            errors.append(f"action[{i}]: eval needs 'id' and 'expression'")
        if t == "scroll" and (act.get("x") is None or act.get("y") is None or act.get("dx") is None):
            errors.append(f"action[{i}]: scroll needs 'x', 'y' and 'dx'")
        if t == "cdp_wheel" and (act.get("target") is None or act.get("x") is None or act.get("y") is None or act.get("dy") is None):
            errors.append(f"action[{i}]: cdp_wheel needs 'target', 'x', 'y' and 'dy'")
        if t == "vscroll" and (act.get("x") is None or act.get("y") is None or act.get("dy") is None):
            errors.append(f"action[{i}]: vscroll needs 'x', 'y' and 'dy'")
        if t == "vscroll_during_capture" and not (act.get("x") is not None and act.get("y") is not None
                                                  and act.get("dy") is not None and act.get("capture_id")):
            errors.append(f"action[{i}]: vscroll_during_capture needs 'x', 'y', 'dy' and 'capture_id'")
        if t == "scroll_during_capture" and not (act.get("x") is not None and act.get("y") is not None
                                                  and act.get("dx") is not None and act.get("capture_id")):
            errors.append(f"action[{i}]: scroll_during_capture needs 'x', 'y', 'dx' and 'capture_id'")
        if t == "drag_during_capture" and not all(act.get(k) is not None
                                                  for k in ("x1", "y1", "x2", "y2", "capture_id")):
            errors.append(f"action[{i}]: drag_during_capture needs 'x1', 'y1', 'x2', 'y2' and 'capture_id'")
        if t == "clicks":
            xs = act.get("x")
            # A single press is `click`, which every other scenario uses and
            # which the runner paces with its own ssh hop. Refused here so a
            # spec cannot ask for a burst and silently get one press.
            if not isinstance(xs, list) or len(xs) < 2:
                errors.append(f"action[{i}]: clicks needs an 'x' list of 2 or more "
                              f"points, got {xs!r}")
            elif any(isinstance(v, bool) or not isinstance(v, int) for v in xs):
                errors.append(f"action[{i}]: clicks 'x' takes integers, got {xs!r}")
            if act.get("y") is None:
                errors.append(f"action[{i}]: clicks needs a 'y'")
        if t == "drag_path":
            pts = act.get("path")
            if (not isinstance(pts, list) or len(pts) < 4 or len(pts) % 2 or
                    any(isinstance(v, bool) or not isinstance(v, int) for v in pts)):
                errors.append(f"action[{i}]: drag_path needs a 'path' of x, y integer "
                              f"pairs, 2 points or more, got {pts!r}")
            steps = act.get("steps", 20)
            # Zero steps would send no motion at all, only a press and a release.
            if isinstance(steps, bool) or not isinstance(steps, int) or steps < 1:
                errors.append(f"action[{i}]: drag_path 'steps' must be 1 or more, got {steps!r}")
        if t == "open_url" and not act.get("url"):
            errors.append(f"action[{i}]: open_url needs a 'url'")
        if t == "wait_vlog" and not act.get("pattern"):
            errors.append(f"action[{i}]: wait_vlog needs a 'pattern'")
        if t == "key_during_capture":
            if not (act.get("combo") and act.get("capture_id")):
                errors.append(f"action[{i}]: key_during_capture needs 'combo' and 'capture_id'")
            # The probe placements measure from different origins, so a spec
            # carrying two says two things about one frame.
            placements = ("mid_at_ms", "mid_after_chord_ms",
                          "mid_after_chord_return_ms")
            if sum(act.get(name) is not None for name in placements) > 1:
                errors.append(f"action[{i}]: key_during_capture takes one of "
                              f"{', '.join(repr(name) for name in placements)}, not several")
            for field in placements:
                value = act.get(field)
                if value is not None and (isinstance(value, bool) or not isinstance(value, (int, float))):
                    errors.append(f"action[{i}]: key_during_capture '{field}' must be a number, got {value!r}")
        if t == "move_during_capture":
            if act.get("x") is None or act.get("y") is None:
                errors.append(f"action[{i}]: move_during_capture needs an 'x' and a 'y'")
            if not act.get("capture_id"):
                errors.append(f"action[{i}]: move_during_capture needs a 'capture_id'")
            if act.get("mid_at_ms") is not None and act.get("mid_after_move_ms") is not None:
                errors.append(f"action[{i}]: move_during_capture takes 'mid_at_ms' or "
                              f"'mid_after_move_ms', not both")
            for field in ("mid_at_ms", "mid_after_move_ms"):
                value = act.get(field)
                if value is not None and (isinstance(value, bool) or not isinstance(value, (int, float))):
                    errors.append(f"action[{i}]: move_during_capture '{field}' must be a number, got {value!r}")
        if t == "key_hold_taps":
            if not act.get("modifier"):
                errors.append(f"action[{i}]: key_hold_taps needs a 'modifier'")
            if not act.get("key"):
                errors.append(f"action[{i}]: key_hold_taps needs a 'key'")
            taps = act.get("taps")
            # A single tap is just `key` with a modifier - refused here so a
            # scenario cannot silently ask for the new action shape and get
            # the OLD, provably-wrong single-press behaviour by omission.
            if not isinstance(taps, int) or isinstance(taps, bool) or taps < 2:
                errors.append(f"action[{i}]: key_hold_taps needs an integer 'taps' >= 2, got {taps!r}")
        # the browser process is refused HERE as well as in
        # pick_renderer.choose, so a spec naming it never reaches a VM at all.
        # One check would do; two mean the refusal survives someone rewriting
        # either half, and killing the browser is not a mistake worth one round.
        if t == "crash_renderer":
            target = act.get("target")
            if target in pick_renderer.REFUSED_TARGETS:
                errors.append(
                    f"action[{i}]: crash_renderer refuses target {target!r} - "
                    f"{pick_renderer.REFUSED_TARGETS[target]}"
                )
            elif target not in pick_renderer.TARGETS:
                errors.append(
                    f"action[{i}]: crash_renderer needs a 'target' in "
                    f"{sorted(pick_renderer.TARGETS)}, got {target!r}"
                )
    # theme_changed/region_stable assertions must reference capture ids that
    # exist. key_during_capture registers TWO ids per action: its own
    # capture_id (the settled/last frame) and "<capture_id>_mid" (the frame
    # lib/mid_frame.py picks) - see that action's own doc in vm-scenario.sh
    # for why one burst yields both.
    # An eval_until stores its answer RAW, where an eval stores it JSON-quoted.
    # So an eval_equals on an eval_until's id must expect the string eval_until
    # settled on, or it can never pass (42 scenarios once did).
    until_expected = {
        a["id"]: a.get("expected")
        for a in spec.get("actions", []) or []
        if isinstance(a, dict) and a.get("type") == "eval_until" and a.get("id")
    }
    for i, asrt in enumerate(spec.get("assertions", []) or []):
        if not isinstance(asrt, dict) or asrt.get("type") != "eval_equals":
            continue
        ref = asrt.get("id")
        if ref in until_expected and asrt.get("expected") != until_expected[ref]:
            errors.append(
                f"assertions[{i}]: eval_equals {ref!r} expects "
                f"{asrt.get('expected')!r} but its eval_until settles on "
                f"{until_expected[ref]!r} - an eval_until answer is stored raw, "
                f"so this assertion can never pass"
            )

    capture_ids = {a.get("id") for a in spec.get("actions", []) if a.get("type") == "capture"}
    for a in spec.get("actions", []):
        if a.get("type") in ("key_during_capture", "move_during_capture") and a.get("capture_id"):
            capture_ids.add(a["capture_id"])
            capture_ids.add(f"{a['capture_id']}_mid")
        if a.get("type") in ("scroll_during_capture", "vscroll_during_capture") and a.get("capture_id"):
            capture_ids.add(a["capture_id"])
    capture_ids.discard(None)
    def check_assertions(records, label):
        for i, asrt in enumerate(records):
            t = asrt.get("type")
            if t not in ASSERT_TYPES:
                errors.append(f"{label}[{i}]: unknown type {t!r} (want {sorted(ASSERT_TYPES)})")
            if t == "ax_window_kind":
                # Both halves are checked here as well as in asserts.sh. An
                # absent pattern would match every window, and an absent
                # expected compares against "" - each turns a gate that cannot
                # fail into one that reads like a real measurement.
                pat = asrt.get("pattern")
                if not isinstance(pat, str) or not pat:
                    errors.append(f"{label}[{i}]: ax_window_kind needs a non-empty 'pattern'")
                exp = asrt.get("expected")
                if not isinstance(exp, int) or isinstance(exp, bool) or exp < 0:
                    errors.append(f"{label}[{i}]: ax_window_kind needs a non-negative integer 'expected'")
            if t == "theme_changed":
                for ref in ("before", "after"):
                    if asrt.get(ref) not in capture_ids:
                        errors.append(f"{label}[{i}]: theme_changed '{ref}' -> unknown capture id {asrt.get(ref)!r}")
            if t == "region_stable":
                for ref in ("before", "after"):
                    if asrt.get(ref) not in capture_ids:
                        errors.append(f"{label}[{i}]: region_stable '{ref}' -> unknown capture id {asrt.get(ref)!r}")
            if t == "pixels_changed":
                for ref in ("before", "after"):
                    if asrt.get(ref) not in capture_ids:
                        errors.append(f"{label}[{i}]: pixels_changed '{ref}' -> unknown capture id {asrt.get(ref)!r}")
                region = asrt.get("region")
                if not isinstance(region, list) or len(region) != 4:
                    errors.append(f"{label}[{i}]: pixels_changed needs a 'region' of [x, y, w, h]")
                # Caught here as well as in moved_outcome.py. One check would do;
                # two mean the refusal survives someone rewriting either half,
                # and a bar that cannot fail is the whole reason this type exists
                # (a threshold tuned against 45 sat quietly missing at
                # 9.9 for weeks). A run costs six minutes; this costs none.
                bar = asrt.get("min_changed_pct")
                if not isinstance(bar, (int, float)) or isinstance(bar, bool):
                    errors.append(
                        f"{label}[{i}]: pixels_changed needs a numeric "
                        f"'min_changed_pct' (share of the region's pixels), got {bar!r}"
                    )
                elif not 0 < bar <= 100:
                    errors.append(
                        f"{label}[{i}]: pixels_changed 'min_changed_pct' is {bar} - "
                        f"a bar outside (0, 100] either passes on any two frames "
                        f"or can never be reached"
                    )
            if t == "vlog_after" and not (asrt.get("pattern") and asrt.get("after_pattern")):
                errors.append(f"{label}[{i}]: vlog_after needs 'pattern' (the later line) and 'after_pattern' (the earlier one)")
            if t == "region_mean" and not (asrt.get("capture") and asrt.get("max_mean") is not None):
                errors.append(f"{label}[{i}]: region_mean needs 'capture' and 'max_mean'")
            if t == "variance_collapsed":
                if not (asrt.get("before") and asrt.get("after") and asrt.get("max_ratio") is not None):
                    errors.append(f"{label}[{i}]: variance_collapsed needs 'before', 'after' and 'max_ratio'")
                else:
                    for ref in ("before", "after"):
                        if asrt.get(ref) not in capture_ids:
                            errors.append(f"{label}[{i}]: variance_collapsed '{ref}' -> unknown capture id {asrt.get(ref)!r}")
            if t == "band_step":
                needed = ("capture", "x", "y", "len", "thick", "dir", "expected", "tol")
                if any(asrt.get(k) is None for k in needed):
                    errors.append(f"{label}[{i}]: band_step needs {', '.join(needed)}")
                elif asrt.get("capture") not in capture_ids:
                    errors.append(f"{label}[{i}]: band_step 'capture' -> unknown capture id {asrt.get('capture')!r}")
                elif asrt.get("dir") not in ("right", "down"):
                    errors.append(f"{label}[{i}]: band_step 'dir' is {asrt.get('dir')!r}, not 'right' or 'down'")
            if t == "regions_track":
                needed = ("capture", "ax", "ay", "aw", "ah", "bx", "by", "bw", "bh", "max_delta", "min_swing")
                if any(asrt.get(k) is None for k in needed):
                    errors.append(f"{label}[{i}]: regions_track needs {', '.join(needed)}")
                elif asrt.get("capture") not in capture_ids:
                    errors.append(f"{label}[{i}]: regions_track 'capture' -> unknown capture id {asrt.get('capture')!r}")
            if t == "region_steady":
                if any(asrt.get(k) is None for k in ("capture", "region", "max_step")):
                    errors.append(f"{label}[{i}]: region_steady needs capture, region, max_step")
                elif asrt.get("capture") not in capture_ids:
                    errors.append(f"{label}[{i}]: region_steady 'capture' -> unknown capture id {asrt.get('capture')!r}")
            if t == "region_every_frame":
                if any(asrt.get(k) is None for k in ("capture", "x", "y", "w", "h", "max_mean")):
                    errors.append(f"{label}[{i}]: region_every_frame needs capture, x, y, w, h, max_mean")
                elif asrt.get("capture") not in capture_ids:
                    errors.append(f"{label}[{i}]: region_every_frame 'capture' -> unknown capture id {asrt.get('capture')!r}")
            if t == "vlog_count" and not (asrt.get("pattern") and asrt.get("max")):
                errors.append(f"{label}[{i}]: vlog_count needs 'pattern' and 'max' - a ceiling is the whole point, since presence alone cannot tell an event-driven sampler from a per-paint one")
            if t in ("vlog", "vlog_absent") and not asrt.get("pattern"):
                errors.append(f"{label}[{i}]: {t} needs a 'pattern'")
            if t == "recency_walk_distinct":
                pattern = asrt.get("pattern")
                if not pattern:
                    errors.append(f"{label}[{i}]: recency_walk_distinct needs a 'pattern'")
                else:
                    try:
                        if re.compile(pattern).groups < 1:
                            errors.append(
                                f"{label}[{i}]: recency_walk_distinct 'pattern' needs a "
                                f"capturing group around the target id, got {pattern!r}"
                            )
                    except re.error as exc:
                        errors.append(f"{label}[{i}]: recency_walk_distinct 'pattern' does not compile: {exc}")
                count = asrt.get("count")
                if not isinstance(count, int) or isinstance(count, bool) or count < 2:
                    errors.append(f"{label}[{i}]: recency_walk_distinct needs an integer 'count' >= 2, got {count!r}")
            if t == "file_contains":
                which = asrt.get("file")
                if which not in STAGED_FILES:
                    errors.append(
                        f"{label}[{i}]: file_contains needs a 'file' in "
                        f"{sorted(STAGED_FILES)}, got {which!r}"
                    )
                elif not spec.get(STAGED_FILES[which]):
                    # Caught here rather than in the VM: the run costs six
                    # minutes and would fail on an empty path at the very end.
                    errors.append(
                        f"{label}[{i}]: file_contains reads the {which!r} file, "
                        f"but this scenario stages none - give the spec a "
                        f"{STAGED_FILES[which]!r}"
                    )
                if not asrt.get("path"):
                    errors.append(
                        f"{label}[{i}]: file_contains needs a 'path' - a dotted "
                        f"JSON path, e.g. 'appState.activeSpaceID'"
                    )
                # Membership, not truthiness: false, 0 and null are all values a
                # scenario has every reason to assert.
                if "value" not in asrt:
                    errors.append(f"{label}[{i}]: file_contains needs a 'value'")

    check_assertions(spec.get("assertions", []), "assert")
    # the optional second list, evaluated after the app has stopped.
    # Same types, same rules - only the moment they read the run log differs.
    teardown = spec.get("teardown_assertions", [])
    if not isinstance(teardown, list):
        errors.append("'teardown_assertions' must be a list")
    else:
        check_assertions(teardown, "teardown_assert")
        # A `quit` in here would send Cmd+Q to an app the harness has already
        # stopped, so it can only ever fail. The scenario means the plain
        # `quit` assertion, which belongs in `assertions`.
        for i, asrt in enumerate(teardown):
            if asrt.get("type") == "quit":
                errors.append(
                    f"teardown_assert[{i}]: 'quit' cannot run here - the harness "
                    f"has already stopped the app by this point. Move it to "
                    f"'assertions'."
                )
    # encode() ships each record as one line, so an embedded newline splits it
    # and the runner reads the tail as an unknown record type mid-run
    #. file_contains 'value' crosses json-escaped and is safe.
    records = [(f"action[{i}]", a) for i, a in enumerate(spec.get("actions", []))]
    records += [(f"assert[{i}]", a) for i, a in enumerate(spec.get("assertions", []))]
    records += [(f"teardown_assert[{i}]", a)
                for i, a in enumerate(spec.get("teardown_assertions", []))]
    for label, record in records:
        for key, val in record.items():
            if record.get("type") == "file_contains" and key == "value":
                continue
            if isinstance(val, str) and "\n" in val:
                errors.append(
                    f"{label}: {key!r} holds a newline - encode() ships each "
                    f"record as one line, so the text after the newline would "
                    f"reach the runner as a record of an unknown type. Keep "
                    f"the field to a single line."
                )
    errors.extend(invalid_regex_patterns(spec))
    return errors


def _is_invalid_grep_pattern(pattern):
    """True if grep -aE (POSIX ERE, what the VM harness actually runs
    every vlog/wait_vlog pattern through) rejects <pattern> as a syntax
    error, rather than simply finding no match.

    a pattern used a PCRE lookahead+backreference the harness's
    grep does not support. It could never match, the run still burned a
    full VM cycle to discover that, and the pre-run warning for this same
    pattern ('appears in no Atrium source file') was advisory and did not
    stop the batch. An invalid regex can never pass, so this is checked as
    a hard error, not a warning - unlike unfindable_patterns() below, which
    stays a warning because a true positive there (a genuinely deleted
    line) is indistinguishable from a false one (an interpolated value).
    """
    try:
        result = subprocess.run(
            ["grep", "-aE", "-q", pattern, os.devnull],
            capture_output=True,
            check=False,
        )
    except OSError:
        return False  # No grep on this host to check against - say nothing.
    # rc 0 (matched) or 1 (no match) both mean grep parsed the pattern fine;
    # anything else (2 on both BSD and GNU grep) is a syntax error.
    return result.returncode not in (0, 1)


def invalid_regex_patterns(spec):
    """Patterns that are not valid grep -aE regexes - would fail every run."""
    errors = []
    records = [(f"action[{i}]", a) for i, a in enumerate(spec.get("actions", []))]
    records += [(f"assert[{i}]", a) for i, a in enumerate(spec.get("assertions", []))]
    records += [(f"teardown_assert[{i}]", a)
                for i, a in enumerate(spec.get("teardown_assertions", []))]
    for label, record in records:
        if record.get("type") not in ("wait_vlog", "vlog", "vlog_absent", "vlog_after",
                                      "vlog_count"):
            continue
        for key in ("pattern", "after_pattern"):
            pattern = record.get(key)
            if not pattern:
                continue
            if _is_invalid_grep_pattern(pattern):
                errors.append(
                    f"{label}: {key} {pattern!r} is not a valid grep -aE "
                    f"(POSIX extended regex) pattern - the harness matches "
                    f"single lines with grep -aE, so PCRE-only syntax such as "
                    f"lookahead or backreferences will never compile there. "
                    f"This can never pass; fix the pattern before spending a "
                    f"VM run on it."
                )
    return errors


# Where the log lines a scenario gates on are actually written.
SOURCE_ROOTS = (
    os.path.expanduser("~/chromium-atrium/src/chrome/browser/ui/atrium"),
    os.path.expanduser("~/chromium-atrium/src/chrome/browser/resources/atrium"),
)

# Individual upstream files, for the few lines a scenario gates on that Atrium
# does not write. A crashed renderer is the case: the fatal-signal report and
# the browser's notice of a dead child both come from Chromium, so without
# these a legitimate gate reads as a deleted line. Listed one by one
# rather than by directory - content/browser alone is 268 files, and this runs
# on every scenario.
SOURCE_FILES = (
    os.path.expanduser("~/chromium-atrium/src/base/debug/stack_trace_posix.cc"),
    os.path.expanduser("~/chromium-atrium/src/content/browser/"
                       "child_process_task_port_provider_mac.cc"),
)

# What ends the literal prefix of a pattern: a regex metacharacter, or a digit.
# Digits matter because most of these lines interpolate a runtime value into the
# middle of the string - "Atrium: tab " << id << " closed" is written as
# "Atrium: tab 1 closed" in a scenario, and only the part before the id was ever
# a literal in the source.
_LITERAL_END = re.compile(r"[\\\[\](){}.*+?^$|0-9]")


def literal_prefix(pattern):
    """The longest leading run of a regex that is plain text in the source."""
    m = _LITERAL_END.search(pattern)
    return (pattern[: m.start()] if m else pattern).strip()


def unfindable_patterns(spec):
    """Patterns whose literal prefix appears in no Atrium source file.

    Three VM runs in one task were spent waiting for log lines that had been
    deleted in that same task. A run costs about six minutes; this costs
    milliseconds.

    These are WARNINGS, never errors, and the reason is worth stating. Most of
    these lines interpolate a runtime value - `"Atrium: edit focus gate " <<
    Name(decision)` is written in a scenario as "Atrium: edit focus gate fire",
    and no source file contains that whole string. Static text matching cannot
    tell that apart from a line that is genuinely gone, so this points at
    patterns worth a second look rather than refusing to run them.
    """
    sources = []
    for root in SOURCE_ROOTS:
        if not os.path.isdir(root):
            return []  # No checkout to search. Say nothing rather than guess.
        for name in os.listdir(root):
            if name.endswith((".cc", ".mm", ".h", ".js")):
                try:
                    with open(os.path.join(root, name), encoding="utf-8",
                              errors="replace") as handle:
                        sources.append(handle.read())
                except OSError:
                    continue
    for path in SOURCE_FILES:
        try:
            with open(path, encoding="utf-8", errors="replace") as handle:
                sources.append(handle.read())
        except OSError:
            continue
    if not sources:
        return []

    errors = []
    records = [(f"action[{i}]", a) for i, a in enumerate(spec.get("actions", []))]
    records += [(f"assert[{i}]", a) for i, a in enumerate(spec.get("assertions", []))]
    records += [(f"teardown_assert[{i}]", a)
                for i, a in enumerate(spec.get("teardown_assertions", []))]
    for label, record in records:
        if record.get("type") not in ("wait_vlog", "vlog", "vlog_after"):
            continue
        for key in ("pattern", "after_pattern"):
            pattern = record.get(key)
            if not pattern:
                continue
            # A URL in a pattern is fixture data, not a string in the source.
            if "://" in pattern:
                continue
            # The relaunch boundary is written by this harness, not by the app,
            # so no source file will ever carry it.
            if pattern.startswith(HARNESS_LINE_PREFIX):
                continue
            prefix = literal_prefix(pattern)
            # Under 12 characters a prefix matches too much to mean anything.
            if len(prefix) < 12:
                continue
            if not any(prefix in source for source in sources):
                errors.append(
                    f"{label}: {key} {prefix!r} appears in no Atrium source "
                    f"file. Either the line was renamed or deleted, or the "
                    f"pattern spans an interpolated value. Check it before "
                    f"spending a VM run on it."
                )
    return errors


def main():
    if len(sys.argv) < 3:
        sys.stderr.write(__doc__ or "")
        return 2
    cmd, path = sys.argv[1], sys.argv[2]
    try:
        spec = load(path)
    except (OSError, json.JSONDecodeError) as exc:
        sys.stderr.write(f"cannot read scenario {path}: {exc}\n")
        return 1

    if cmd == "validate":
        for warning in unfindable_patterns(spec):
            sys.stderr.write(f"scenario warning: {warning}\n")
        errors = validate(spec)
        if errors:
            for err in errors:
                sys.stderr.write(f"scenario invalid: {err}\n")
            return 1
        print("ok")
        return 0
    if cmd == "meta":
        if len(sys.argv) < 4:
            sys.stderr.write("meta needs a key\n")
            return 2
        key = sys.argv[3]
        val = spec.get(key, "")
        # app_process defaults to Atrium; startup_space/themes may be empty.
        if key == "app_process" and not val:
            val = "Atrium"
        print(val if val is not None else "")
        return 0
    if cmd == "actions":
        for act in spec.get("actions", []):
            print(encode(act))
        return 0
    if cmd == "asserts":
        for asrt in spec.get("assertions", []):
            print(encode(asrt))
        return 0
    if cmd == "teardown_asserts":
        for asrt in spec.get("teardown_assertions", []):
            print(encode(asrt))
        return 0
    sys.stderr.write(f"unknown subcommand {cmd!r}\n")
    return 2


if __name__ == "__main__":
    sys.exit(main())
