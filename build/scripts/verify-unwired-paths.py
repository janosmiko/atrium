#!/usr/bin/env python3
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
"""Drift guard: built-but-unwired paths - a mechanism ships, a test drives it
directly and passes, and no production code ever calls it.

Thirteen of these were found by hand in two days, always the same shape:
SetBackdropTint had no caller so the AI panel's ink never followed a space
switch; SummarizeViaOllama had no caller so 13 passing unit tests guarded an
unreachable feature; shelf-section.js was missing from atrium_resources.grd
so the module never loaded; atrium.html never linked overview.css.

Three checks, matching verify-webui-messages.py's shape:

  A. Every `window.atrium*` receiver defined in the JS has a caller in
     production C++/Obj-C++ (not just a _unittest.cc/_browsertest.cc file).
  B. Every JS module in chrome/browser/resources/atrium is registered in
     atrium_resources.grd AND is reachable: imported by another module, the
     atrium.html entry point, or a script the native side injects, which the
     guard sees as its grd IDR name in production C++. An allowlist entry
     that excuses "not yet registered" expires the moment another module
     imports it - that combination is always fatal, so it fails
     even while allowlisted.
  C. Every CSS file there is linked from atrium.html or imported by another
     stylesheet.

Does NOT check kSurfaceMessages registration <-> handler agreement - that's
verify-webui-messages.py's job for the sent-name direction (the one that
crashes the browser). The reverse direction (registered but no C++ handler
compares against the name) is a silent no-op, not a crash, and each surface's
OnSurfaceMessage dispatches by bespoke per-widget string comparisons, so a
generic version would be all false positives. Not attempted here.

Shares verify-webui-messages.py's known blind spot: check A greps native
files for the receiver name as a literal identifier. A caller built from a
variable or lookup table (e.g. ExecuteJavaScript(base::StringPrintf(...)))
reads as no-caller and false-fails here, the same way a chrome.send name
passed through a variable reads as registered-but-unsent there. If that
happens, add the name to the allowlist with a reason explaining the
indirection rather than treating it as a real gap.
"""

import json
import os
import re
import sys
import tempfile

SRC = os.path.expanduser(os.environ.get("ATRIUM_CHROMIUM_SRC", "~/chromium-atrium/src"))
RESOURCES = os.path.join(SRC, "chrome/browser/resources/atrium")
GRD_PATH = os.path.join(RESOURCES, "atrium_resources.grd")
HTML_PATH = os.path.join(RESOURCES, "atrium.html")
NATIVE_DIRS = [
    os.path.join(SRC, "chrome/browser/ui/atrium"),
    os.path.join(SRC, "chrome/browser/ui/webui/atrium"),
]
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ALLOWLIST_PATH = os.path.join(REPO, "build/scripts/unwired-paths-allowlist.json")

TEST_SUFFIXES = ("_unittest.cc", "_browsertest.cc")


def load_allowlist():
    if not os.path.isfile(ALLOWLIST_PATH):
        return {}
    with open(ALLOWLIST_PATH, encoding="utf-8") as handle:
        entries = json.load(handle)
    allowed = {}
    for entry in entries:
        name = entry.get("name")
        reason = entry.get("reason")
        task = entry.get("task")
        if not name or not reason or not task:
            sys.exit(
                "verify-unwired-paths: %s entry missing name, reason, or "
                "owning task: %r" % (ALLOWLIST_PATH, entry)
            )
        allowed[name] = task
    return allowed


def strip_cpp_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", "", text)
    return text


def js_files():
    return sorted(f for f in os.listdir(RESOURCES) if f.endswith(".js"))


def css_files():
    return sorted(f for f in os.listdir(RESOURCES) if f.endswith(".css"))


# --- Check A: window.atrium* receivers need a production native caller -----

def receiver_definitions():
    """name -> (file, line) of its first `window.atriumX = ` definition."""
    defs = {}
    pattern = re.compile(r"window\.(atrium\w+)\s*=(?!=)")
    for entry in js_files():
        path = os.path.join(RESOURCES, entry)
        with open(path, encoding="utf-8") as handle:
            for lineno, line in enumerate(handle, start=1):
                match = pattern.search(line)
                if match and match.group(1) not in defs:
                    defs[match.group(1)] = (entry, lineno)
    return defs


def native_file_texts():
    """(path, stripped_text, is_test) for every C++/Obj-C++/header under the
    native dirs, comments stripped so a mention inside a comment doesn't
    count as a caller."""
    files = []
    for base in NATIVE_DIRS:
        if not os.path.isdir(base):
            continue
        for entry in sorted(os.listdir(base)):
            if not entry.endswith((".cc", ".h", ".mm")):
                continue
            path = os.path.join(base, entry)
            with open(path, encoding="utf-8", errors="replace") as handle:
                text = strip_cpp_comments(handle.read())
            files.append((entry, text, entry.endswith(TEST_SUFFIXES)))
    return files


def check_receivers(allowlist, stale):
    defs = receiver_definitions()
    if not defs:
        return []
    native = native_file_texts()
    failures = []
    for name, (js_file, lineno) in sorted(defs.items()):
        word = re.compile(r"\b%s\b" % re.escape(name))
        prod_hit = any(word.search(text) for _, text, is_test in native if not is_test)
        if name in allowlist:
            if prod_hit:
                stale.append(
                    "window.%s (%s) - now has a production caller, "
                    "delete its entry from %s (owning task %s)"
                    % (name, js_file, ALLOWLIST_PATH, allowlist[name])
                )
            continue
        if prod_hit:
            continue
        test_hits = [entry for entry, text, is_test in native if is_test and word.search(text)]
        if test_hits:
            detail = "only called from test file(s): %s" % ", ".join(test_hits)
        else:
            detail = "no caller found anywhere in %s" % " or ".join(NATIVE_DIRS)
        failures.append(
            "%s:%d [receiver-no-caller] window.%s has no production native caller - %s"
            % (js_file, lineno, name, detail)
        )
    return failures


# --- Check B: JS module registered in grd AND imported/entry point --------

def grd_js_includes():
    if not os.path.isfile(GRD_PATH):
        return None
    with open(GRD_PATH, encoding="utf-8") as handle:
        text = handle.read()
    return set(re.findall(r'<include[^>]+file="([^"]+\.js)"', text))


def html_entry_scripts():
    if not os.path.isfile(HTML_PATH):
        return set()
    with open(HTML_PATH, encoding="utf-8") as handle:
        text = handle.read()
    return set(re.findall(r'<script[^>]+src="([^"]+\.js)"', text))


def js_import_graph():
    """set of every ./x.js named on the right of an ES import anywhere."""
    imported = set()
    pattern = re.compile(r"""from\s*['"]\./([\w.-]+\.js)['"]""")
    for entry in js_files():
        path = os.path.join(RESOURCES, entry)
        with open(path, encoding="utf-8") as handle:
            imported.update(pattern.findall(handle.read()))
    return imported


def natively_loaded_js():
    """Every JS file whose grd IDR name production native code loads.

    A script injected into page frames from C++ has no importer by design,
    so the IDR reference is the only wiring the guard can see.
    """
    if not os.path.isfile(GRD_PATH):
        return set()
    with open(GRD_PATH, encoding="utf-8") as handle:
        text = handle.read()
    pairs = re.findall(r'<include\s+name="(\w+)"\s+file="([^"]+\.js)"', text)
    prod = [body for _, body, is_test in native_file_texts() if not is_test]
    return {
        js for idr, js in pairs
        if any(re.search(r"\b%s\b" % re.escape(idr), body) for body in prod)
    }


def check_js_registration(allowlist, stale):
    grd = grd_js_includes()
    if grd is None:
        return ["%s [grd-missing] not found - every JS module registration check is skipped" % GRD_PATH]
    entries = html_entry_scripts()
    imported = js_import_graph()
    injected = natively_loaded_js()
    failures = []
    for entry in js_files():
        in_grd = entry in grd
        is_imported = entry in entries or entry in imported or entry in injected
        wired = in_grd and is_imported
        if entry in allowlist:
            if wired:
                stale.append(
                    "%s - now registered in the grd and imported, delete "
                    "its entry from %s (owning task %s)"
                    % (entry, ALLOWLIST_PATH, allowlist[entry])
                )
                continue
            # mirror.js: "not yet registered" stops being true
            # the moment something imports the module - an unregistered
            # import is always fatal, so the exemption expires here too.
            if not in_grd and is_imported:
                failures.append(
                    "%s:1 [js-allowlist-stale-on-import] allowlisted as not "
                    "yet registered (owning task %s), but another module "
                    "now imports it - an unregistered import is fatal, so "
                    "register it in %s or remove the import"
                    % (entry, allowlist[entry], GRD_PATH)
                )
            continue
        if not in_grd:
            failures.append(
                "%s:1 [js-not-in-grd] not listed in %s - the module never loads"
                % (entry, GRD_PATH)
            )
            continue
        if not is_imported:
            failures.append(
                "%s:1 [js-orphan] registered in the grd but never imported by "
                "another module, not an atrium.html entry point, and its IDR "
                "name is not loaded by production native code"
                % entry
            )
    return failures


# --- Check C: CSS linked from atrium.html or imported ----------------------

def html_linked_css():
    if not os.path.isfile(HTML_PATH):
        return set()
    with open(HTML_PATH, encoding="utf-8") as handle:
        text = handle.read()
    return set(re.findall(r'<link[^>]+href="([^"]+\.css)"', text))


def css_import_graph():
    imported = set()
    pattern = re.compile(r"""@import\s*['"]\.?/?([\w.-]+\.css)['"]""")
    for entry in css_files():
        path = os.path.join(RESOURCES, entry)
        with open(path, encoding="utf-8") as handle:
            imported.update(pattern.findall(handle.read()))
    return imported


def check_css_links(allowlist, stale):
    if not os.path.isfile(HTML_PATH):
        return ["%s [html-missing] not found - every CSS link check is skipped" % HTML_PATH]
    linked = html_linked_css()
    imported = css_import_graph()
    failures = []
    for entry in css_files():
        wired = entry in linked or entry in imported
        if entry in allowlist:
            if wired:
                stale.append(
                    "%s - now linked from %s, delete its entry from %s "
                    "(owning task %s)"
                    % (entry, HTML_PATH, ALLOWLIST_PATH, allowlist[entry])
                )
            continue
        if not wired:
            failures.append(
                "%s:1 [css-unlinked] not linked from %s and not @import'd by "
                "another stylesheet" % (entry, HTML_PATH)
            )
    return failures


def _write(path, content):
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(content)


def run_selftest():
    """Reproduce the mirror.js incident against a fixture
    tree, proving check_js_registration both passes clean and fails the
    exact shape that shipped six hours of blank surfaces."""
    global RESOURCES, GRD_PATH, HTML_PATH, NATIVE_DIRS
    orig = (RESOURCES, GRD_PATH, HTML_PATH)
    orig_native = NATIVE_DIRS
    ok = True
    try:
        with tempfile.TemporaryDirectory() as tmp:
            RESOURCES = tmp
            GRD_PATH = os.path.join(tmp, "atrium_resources.grd")
            HTML_PATH = os.path.join(tmp, "atrium.html")
            allowlist = {"bench.js": "a reason"}

            _write(HTML_PATH, '<script type="module" src="entry.js"></script>')
            _write(os.path.join(tmp, "entry.js"), "import {init} from './sidebar.js';\n")
            _write(os.path.join(tmp, "bench.js"), "// deliberately unregistered\n")
            _write(GRD_PATH,
                   '<include name="IDR_ENTRY_JS" file="entry.js" type="BINDATA" />\n'
                   '<include name="IDR_SIDEBAR_JS" file="sidebar.js" type="BINDATA" />')

            print("selftest 1/6: allowlisted + unimported + unregistered passes")
            _write(os.path.join(tmp, "sidebar.js"), "// no imports yet\n")
            failures = check_js_registration(allowlist, [])
            if failures:
                print("SELFTEST FAILED: clean fixture reported failures: %r" % failures)
                ok = False

            print("selftest 2/6: sidebar.js importing the allowlisted module must fail")
            _write(os.path.join(tmp, "sidebar.js"), "import {mirror} from './bench.js';\n")
            failures = check_js_registration(allowlist, [])
            tagged = [f for f in failures if "js-allowlist-stale-on-import" in f]
            if not tagged:
                print(
                    "SELFTEST FAILED: importing an allowlisted, unregistered "
                    "module did not fail: %r" % failures
                )
                ok = False

            print("selftest 3/6: a moved grd must fail, not pass with nothing checked")
            moved_grd = GRD_PATH
            GRD_PATH = os.path.join(tmp, "atrium_resources.grd.moved")
            failures = check_js_registration(allowlist, [])
            if not failures or "grd-missing" not in failures[0]:
                print("SELFTEST FAILED: a missing grd did not fail loudly: %r" % failures)
                ok = False
            GRD_PATH = moved_grd

            print("selftest 4/6: a moved atrium.html must fail, not pass with nothing checked")
            moved_html = HTML_PATH
            HTML_PATH = os.path.join(tmp, "atrium.html.moved")
            failures = check_css_links(allowlist, [])
            if not failures or "html-missing" not in failures[0]:
                print("SELFTEST FAILED: a missing atrium.html did not fail loudly: %r" % failures)
                ok = False
            HTML_PATH = moved_html

            # vim-page.js is injected into page frames from C++,
            # so nothing imports it and the import graph alone reads it as
            # dead. The IDR name the native side loads is the wiring.
            native = os.path.join(tmp, "native")
            os.makedirs(native)
            NATIVE_DIRS = [native]
            _write(os.path.join(tmp, "inject.js"), "// injected, never imported\n")
            _write(GRD_PATH,
                   '<include name="IDR_ENTRY_JS" file="entry.js" type="BINDATA" />\n'
                   '<include name="IDR_SIDEBAR_JS" file="sidebar.js" type="BINDATA" />\n'
                   '<include name="IDR_INJECT_JS" file="inject.js" type="BINDATA" />')
            _write(os.path.join(tmp, "sidebar.js"), "// no imports yet\n")

            print("selftest 5/6: a grd IDR loaded by production C++ counts as wired")
            _write(os.path.join(native, "injector.cc"),
                   "auto s = LoadDataResourceString(IDR_INJECT_JS);\n")
            failures = check_js_registration(allowlist, [])
            if failures:
                print("SELFTEST FAILED: a natively loaded module reported "
                      "failures: %r" % failures)
                ok = False

            print("selftest 6/6: dropping that IDR reference must fail")
            _write(os.path.join(native, "injector.cc"), "auto s = nothing;\n")
            failures = check_js_registration(allowlist, [])
            if not any("js-orphan" in f and "inject.js" in f for f in failures):
                print("SELFTEST FAILED: a module with no importer and no IDR "
                      "reference did not fail: %r" % failures)
                ok = False
    finally:
        RESOURCES, GRD_PATH, HTML_PATH = orig
        NATIVE_DIRS = orig_native

    if not ok:
        return 1
    print("selftest OK: the allowlist exemption holds unimported and fails once imported, "
          "a moved grd or html marker fails loudly, and a natively loaded "
          "module counts as wired only while its IDR reference stands")
    return 0


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--selftest":
        return run_selftest()

    if not os.path.isdir(RESOURCES):
        print("verify-unwired-paths: skipped, no Chromium tree at %s" % SRC)
        return 0

    if not js_files() and not css_files():
        print("verify-unwired-paths: FAIL - no .js or .css files found under %s, nothing to check" % RESOURCES)
        return 1

    allowlist = load_allowlist()
    stale = []
    failures = []
    failures += check_receivers(allowlist, stale)
    failures += check_js_registration(allowlist, stale)
    failures += check_css_links(allowlist, stale)

    if failures or stale:
        if failures:
            print("verify-unwired-paths: FAIL - %d built-but-unwired path(s)" % len(failures))
            for line in failures:
                print("  " + line)
            print(
                "Each is a mechanism with no caller: wire it in, or if the "
                "check is wrong, add {name, reason, task} to %s"
                % ALLOWLIST_PATH
            )
        if stale:
            print("verify-unwired-paths: FAIL - %d stale allowlist entr%s" % (
                len(stale), "y" if len(stale) == 1 else "ies"
            ))
            for line in stale:
                print("  " + line)
            print(
                "A wired entry left in the allowlist is a permanent "
                "exemption in disguise - delete it."
            )
        return 1

    print(
        "verify OK: every window.atrium* receiver has a native caller, every "
        "JS module is registered and imported, every CSS file is linked"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
