#!/usr/bin/env python3
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
"""Drift guard: every chrome.send name the WebUI routes use must be registered.

An unregistered name is not a no-op. content::WebUIImpl::ProcessWebUIMessage
NOTREACHEDs on a message with no callback, which is FATAL in the BROWSER
process - the app dies at startup, and the renderer-death observer cannot
recover it. "sidebarReady" did exactly that on 2026-08-03.

Registration is driven by kSurfaceMessages in atrium_surface_host.h, which
atrium_ui.cc iterates. This script asserts the JS and that list agree.

A route may take an injected sender instead of calling chrome.send itself -
buildResizer does, for testability - so the calls read as send(<name>, ... .
Matching only the literal chrome.send let sidebarWidthLive and
sidebarWidthCommit through, and dragging the resizer killed the browser. Both spellings are scanned.
"""

import os
import re
import sys

# chrome.send("x") and a send("x") reached through an injected sender. The
# second is anchored on a non-identifier character so it cannot match the tail
# of another call, and it deliberately still matches chrome.send itself.
SEND_PATTERNS = (
    r"chrome\.send\(\s*['\"]([^'\"]+)['\"]",
    r"(?:^|[^\w.])send\(\s*['\"]([^'\"]+)['\"]",
    r"(?:^|[^\w.])emit\(\s*['\"]([^'\"]+)['\"]",
)

# a registered name that appears NOWHERE in the JS as a string cannot be
# reached from any surface, whatever the call shape. Call-shape matching keeps
# losing this race - it missed send( for the resizer's two verbs, which aborted the
# browser, and emit( for sidebarTabMirrored, which I then wrongly called orphaned.
# So reachability is asked shape-agnostically: is the name written down at all.
#
# Weaker per name (a literal in a comment counts) and stronger as a guard, because
# no new indirection defeats it. Names here are deliberately registered ahead of
# the UI that will send them; each needs a reason naming what will.
LANDED_BEFORE_SENDER = {
    "shelfListAdd": "UI trigger is not built yet, the handler landed first "
                    "(see kSurfaceMessages' own note)",
    "mediaMuteToggled": "no sender since the resting media row dropped its "
                        "mute button, delete the handler or restore the button",
}

SRC = os.path.expanduser(os.environ.get("ATRIUM_CHROMIUM_SRC", "~/chromium-atrium/src"))
RESOURCES = os.path.join(SRC, "chrome/browser/resources/atrium")
HOST_HEADER = os.path.join(SRC, "chrome/browser/ui/atrium/atrium_surface_host.h")
ACTIONS_TABLE = os.path.join(
    SRC, "chrome/browser/ui/atrium/atrium_command_bar_actions.cc")


def js_message_names():
    names = {}
    if not os.path.isdir(RESOURCES):
        return None
    for entry in sorted(os.listdir(RESOURCES)):
        if not entry.endswith(".js"):
            continue
        path = os.path.join(RESOURCES, entry)
        with open(path, encoding="utf-8") as handle:
            source = handle.read()
            for pattern in SEND_PATTERNS:
                for name in re.findall(pattern, source):
                    names.setdefault(name, entry)
    return names


def js_string_literals():
    """Every single- or double-quoted string in the WebUI sources."""
    found = set()
    for entry in sorted(os.listdir(RESOURCES)):
        if not entry.endswith(".js"):
            continue
        with open(os.path.join(RESOURCES, entry), encoding="utf-8") as handle:
            found.update(re.findall(r"['\"]([A-Za-z][A-Za-z0-9_]*)['\"]",
                                    handle.read()))
    return found


def catalog_message_names():
    """The palette's verbs, which live in a C++ table rather than in the JS.

    The browser pushes the rows and the page sends whatever they name, so no
    scan of the JS can see these. An unparseable table yields nothing, which
    reports them as orphans rather than passing quietly.
    """
    try:
        with open(ACTIONS_TABLE, encoding="utf-8") as handle:
            body = handle.read().split(
                "std::to_array<CatalogRow>({")[1].split("\n});")[0]
    except (OSError, IndexError):
        return set()
    return set(re.findall(r'\{"([A-Za-z][A-Za-z0-9_]*)"', body))


def registered_names():
    with open(HOST_HEADER, encoding="utf-8") as handle:
        source = handle.read()
    try:
        body = source.split("kSurfaceMessages[] = {")[1].split("};")[0]
    except IndexError:
        sys.exit("verify-webui-messages: kSurfaceMessages not found in %s" % HOST_HEADER)
    return set(re.findall(r'"([^"]+)"', body))


def selftest():
    """Prove the scan sees a name sent through an injected sender."""
    def scan(line):
        return [n for p in SEND_PATTERNS for n in re.findall(p, line)]

    # Not indexed into SEND_PATTERNS: dropping a pattern has to fail here with
    # this message, which is the whole point of the selftest.
    for line, name, shape in (
            ("  send('sidebarWidthLive', [160]);", "sidebarWidthLive", "send("),
            ("  action: () => emit('sidebarTabMirrored', [url]);",
             "sidebarTabMirrored", "emit("),
            ("  chrome.send('sidebarReady', []);", "sidebarReady",
             "chrome.send(")):
        if scan(line) != [name]:
            print("verify-webui-messages: SELFTEST FAILED - a name sent through"
                  " %s is not scanned" % shape)
            return 1
    # And that it does not claim a name from an unrelated method call.
    if scan("queue.resend('nope');"):
        print("verify-webui-messages: SELFTEST FAILED - matched resend(")
        return 1
    print("selftest OK: an injected sender is scanned, resend( is not")
    return 0


def main():
    if "--selftest" in sys.argv:
        return selftest()

    sent = js_message_names()
    if sent is None:
        # No Chromium checkout here (CI lanes that only build Swift).
        print("verify-webui-messages: skipped, no Chromium tree at %s" % SRC)
        return 0

    registered = registered_names()
    missing = {n: f for n, f in sent.items() if n not in registered}
    if missing:
        print("verify-webui-messages: FAIL - unregistered chrome.send names")
        print("These crash the BROWSER process the moment their route loads.")
        for name, origin in sorted(missing.items()):
            print("  %-28s sent by %s" % (name, origin))
        print("Add them to kSurfaceMessages in %s" % HOST_HEADER)
        return 1

    # a registered name written nowhere in the JS. Distinct from
    # "registered but unsent" - most of those ARE sent, through a data field
    # (chrome.send(match.send, ...)) that no call-shape scan can see.
    literals = js_string_literals() | catalog_message_names()
    orphaned = sorted(n for n in registered
                      if n not in literals and n not in LANDED_BEFORE_SENDER)
    if orphaned:
        print("verify-webui-messages: FAIL - registered names no surface mentions")
        print("Each is a handler a user cannot reach. Wire it up, delete it, or")
        print("add it to LANDED_BEFORE_SENDER with the task that will send it.")
        for name in orphaned:
            print("  %s" % name)
        return 1

    stale = sorted(n for n in LANDED_BEFORE_SENDER if n in literals)
    if stale:
        print("verify-webui-messages: FAIL - allowlisted names that ARE now sent")
        print("Their sender landed; drop them from LANDED_BEFORE_SENDER.")
        for name in stale:
            print("  %s" % name)
        return 1

    print("verify OK: %d chrome.send name(s) all registered" % len(sent))
    return 0


if __name__ == "__main__":
    sys.exit(main())
