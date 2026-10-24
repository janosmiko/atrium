#!/usr/bin/env bash
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
# Runs every ported sidebar interaction against ONE build, each with its own
# fixture.
#
#   make sidebar-regress
#
# These probes cover interlocking changes to the same drag, snapshot and
# selection code. Each was verified in isolation, on a different fixture.
# Running them together is what catches one breaking another - which it did on
# the first run: the drag-start affordance reveal reflows the sidebar, and the
# row drag probe was still measuring its drop coordinate before the drag began.
#
# Every check asserts the PERSISTED model where an action changes it. The DOM
# is what the surface drew, which is the thing under test.
#
# Needs the VM running and 2+ spaces. Rewrites the VM's spaces.json between
# runs, so do not point it at anything you care about.
set -uo pipefail

# || exit, because a cd that fails would run every command below in whatever
# directory the caller happened to be in and could still report success (SC2164).
cd "$(dirname "${BASH_SOURCE[0]}")/../.." || exit 1
# ConnectTimeout matters here: the address used to be hardcoded, and when the VM
# moved, ssh blocked on the TCP connect for twenty minutes instead of failing.
SSHK=(-i "${TART_HOME:-$HOME/.tart}/atrium-probe-key" -o StrictHostKeyChecking=no
      -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=10)
VM="${ATRIUM_VM_HOST:-}"
if [ -z "$VM" ]; then
  vm_ip="$(./build/scripts/vm.sh ip 2>/dev/null)" || vm_ip=""
  [ -n "$vm_ip" ] || { echo "error: no VM address - start it with make vm-start, or set ATRIUM_VM_HOST" >&2; exit 2; }
  VM="admin@${vm_ip}"
fi

# Only the Tart bridge subnet may be targeted - an allowlist, so a form we
# did not think of (IPv6, a zone id, a hostname, an empty host) fails closed.
vm_addr="${VM##*@}"
if [[ ! "$vm_addr" =~ ^192\.168\.64\.[0-9]{1,3}$ ]]; then
  echo "error: refusing to target '${vm_addr}' - not the Tart bridge subnet (192.168.64.x)" >&2
  exit 2
fi
echo "==> VM ${VM}"

# Writes the document every probe starts from, then applies that probe's own
# body to it.
#
# this used to READ the VM's spaces.json and patch it. A clean
# snapshot has no such file, json.load raised, and the whole thing was inside
# an ssh whose output went to /dev/null - so every probe ran against nothing
# and reported a missing fixture, while the suite reported success. A gate over
# nothing.
#
# Written fresh rather than patched, for a second reason too: Atrium rewrites
# spaces.json from memory on exit, so anything seeded once is gone a run later.
# Each probe now starts from the same known document.
#
# The base carries what the strictest probe needs - two spaces for the rail,
# four regular tabs, a pinned row, and three favourites - so a probe body only
# has to say how it differs.
fixture() {  # $1 = python body operating on `d`
  ssh "${SSHK[@]}" "$VM" "pkill -f 'MacOS/Atrium'; sleep 4; python3 -c \"
import json, os
if not os.path.isdir('/Users/admin'):
    raise SystemExit('refusing: no /Users/admin, this is not the Tart VM')
p = os.path.expanduser('~/Library/Application Support/Atrium/spaces.json')
os.makedirs(os.path.dirname(p), exist_ok=True)
pid = 'Default'
def space(sid, name, order):
    return {'id': sid, 'name': name, 'emoji': 'H', 'tint': '#4A6FA5',
            'profileID': pid, 'pinnedTabs': [], 'tabs': [], 'order': order,
            'groups': []}
d = {
  'spaces': [space('11111111-1111-1111-1111-111111111111', 'Probe', 0),
             space('22222222-2222-2222-2222-222222222222', 'Probe Two', 1)],
  'appState': {'sidebarWidth': 280, 'schemaVersion': 1},
  'favorites': [],
}
d['spaces'][0]['tabs'] = [
  {'url': 'https://b%d.test/' % i, 'title': 'BASE %d' % i} for i in range(4)]
d['spaces'][0]['pinnedTabs'] = [{'url': 'https://bp.test/', 'title': 'BASE PIN'}]
d['favorites'] = [
  {'url': 'https://bf%d.test/' % i, 'title': 'BASE F%d' % i, 'profileID': pid}
  for i in range(3)]
# shellcheck disable=SC2029  # deliberate: \$1 is spliced into the python
# program text on the CLIENT, which is what makes this a template.
$1
d['appState']['activeSpaceID'] = d['spaces'][0]['id']
d['appState']['windowSpaces'] = [d['spaces'][0]['id']]
json.dump(d, open(p,'w'))
print('staged %d spaces, %d tabs, %d pinned, %d favourites' % (
    len(d['spaces']), len(d['spaces'][0]['tabs']),
    len(d['spaces'][0]['pinnedTabs']), len(d['favorites'])))
\"" || { echo "error: could not stage the fixture" >&2; exit 2; }
}

launch() {
  # ATRIUM_STARTUP_WINDOW=1: without a window there is no Browser, so browser_
  # is null and every message needing it is silently a no-op - and a real drag
  # has nothing to happen in. appState.windowSpaces must also name a space,
  # which fixture() sets.
  ATRIUM_STARTUP_WINDOW=1 ./build/scripts/vm.sh run-atrium \
      --remote-debugging-port=9334 >/dev/null 2>&1
  sleep 16
  timeout 60 ./build/scripts/vm-cdp.sh >/dev/null 2>&1
}

declare -a NAMES=() RESULTS=()
run() {  # $1 = label, $2 = probe script
  echo "=============== $1"
  launch
  if node "$2" 2>&1 | tail -20; then
    NAMES+=("$1"); RESULTS+=(PASS)
  else
    NAMES+=("$1"); RESULTS+=(FAIL)
  fi
}

TABS3="d['spaces'][0]['tabs'] = [
  {'url': 'https://alpha.test/', 'title': 'ALPHA'},
  {'url': 'https://bravo.test/', 'title': 'BRAVO'},
  {'url': 'https://cha.test/',   'title': 'CHARLIE'},
  {'url': 'https://delta.test/', 'title': 'DELTA'},
]"

# 0. REAL INPUT first, and deliberately first. Everything below synthesises
#    DragEvents, which bypasses the browser - all of it passed for weeks against
#    a drag that had never once worked for a real pointer. If this
#    step fails, the rest of the suite going green means nothing.
fixture "$TABS3
d['spaces'][0]['pinnedTabs'] = []
d['favorites'] = []"
run "real-input drag survives dragstart" build/scripts/drag-input-probe.mjs

# 1. Row reorder within the tab list.
fixture "$TABS3
d['spaces'][0]['pinnedTabs'] = []
d['favorites'] = []"
run "row drag reorder" build/scripts/row-drag-probe.mjs

# 2. Cross-section drops, with pins present.
fixture "$TABS3
d['spaces'][0]['pinnedTabs'] = [
  {'url': 'https://pin1.test/', 'title': 'PIN1'},
  {'url': 'https://pin2.test/', 'title': 'PIN2'},
]
d['spaces'][0]['pinnedCollapsed'] = False
d['favorites'] = []"
run "pin/unpin/favourite" build/scripts/row-drop-probe.mjs

# 3. The same probe's first-pin mode: the target does not exist at rest.
fixture "$TABS3
d['spaces'][0]['pinnedTabs'] = []
d['favorites'] = []"
run "first pin (empty section)" build/scripts/row-drop-probe.mjs

# 4. Edge paging and the cross-space drop.
fixture "d['spaces'][0]['tabs'] = [
  {'url': 'https://live.test/',  'title': 'LIVE'},
  {'url': 'https://mover.test/', 'title': 'MOVER'},
  {'url': 'https://stay.test/',  'title': 'STAY'},
]
d['spaces'][0]['pinnedTabs'] = []
d['spaces'][1]['tabs'] = [{'url': 'https://other.test/', 'title': 'OTHER'}]
d['spaces'][1]['pinnedTabs'] = []
d['favorites'] = []"
run "edge paging + cross-space" build/scripts/edge-paging-probe.mjs

# 5. Favourite grid reorder.
fixture "$TABS3
d['spaces'][0]['pinnedTabs'] = []
d['favorites'] = [
  {'url': 'https://one.test/',   'title': 'ONE',   'profileID': pid},
  {'url': 'https://two.test/',   'title': 'TWO',   'profileID': pid},
  {'url': 'https://three.test/', 'title': 'THREE', 'profileID': pid},
  {'url': 'https://four.test/',  'title': 'FOUR',  'profileID': pid},
]"
run "favourite reorder" build/scripts/fav-drag-probe.mjs

# 6. Each pane renders its own space - the swipe-lag check.
fixture "$TABS3
d['spaces'][0]['pinnedTabs'] = []
d['spaces'][1]['tabs'] = [
  {'url': 'https://other1.test/', 'title': 'OTHER1'},
  {'url': 'https://other2.test/', 'title': 'OTHER2'},
]
d['favorites'] = []"
run "panes render their own space" build/scripts/per-space-panes-probe.mjs

# 7. A favourite-bound active tab marks its tile.
fixture "d['spaces'][0]['tabs'] = [{'url': 'https://fav-one.test/', 'title': 'FAVTAB'}]
d['spaces'][0]['pinnedTabs'] = []
d['favorites'] = [
  {'url': 'https://fav-one.test/', 'title': 'FAV ONE', 'profileID': pid},
  {'url': 'https://fav-two.test/', 'title': 'FAV TWO', 'profileID': pid},
]"
run "favourite-bound tab marks its tile" build/scripts/favourite-selection-probe.mjs

# 8. Folder membership - the thing no surface could set until folder drops existed.
fixture "import uuid
gid = str(uuid.uuid4()).upper()
d['spaces'][0]['groups'] = [{'id': gid, 'name': 'Folder A', 'collapsed': False}]
d['spaces'][0]['tabs'] = [
  {'url': 'https://join.test/', 'title': 'JOINER'},
  {'url': 'https://stay.test/', 'title': 'STAYER'},
]
d['spaces'][0]['pinnedTabs'] = []
d['favorites'] = []"
run "folder drop sets membership" build/scripts/group-drop-probe.mjs

# 9. A cold row reopens its tab when clicked.
fixture "$TABS3
d['spaces'][0]['pinnedTabs'] = []
d['favorites'] = []"
run "cold row reopens on click" build/scripts/cold-tab-probe.mjs

# 10. The context menu, driven by a real right-click.
fixture "d['spaces'][0]['tabs'] = [
  {'url': 'https://m1.test/', 'title': 'MENU1'},
  {'url': 'https://m2.test/', 'title': 'MENU2'},
]
d['spaces'][0]['pinnedTabs'] = [{'url': 'https://p1.test/', 'title': 'PINNED1'}]
d['spaces'][0]['pinnedCollapsed'] = False
d['favorites'] = []"
run "context menu pin/unpin" build/scripts/menu-probe.mjs

# 11. A cancelled favourite drag, and one that strays into a neighbouring
#     space's grid. What this turns on is node IDENTITY - which grid element
#     the drag started in - so the two panes only need to be separate grids, not
#     to hold different tiles. Putting the second space on the SAME profile and
#     leaving the favourites profile-wide gives both panes a populated grid,
#     which is the cheapest way to pose it.
fixture "d['spaces'][1]['profileID'] = pid
d['spaces'][0]['tabs'] = []
d['spaces'][0]['pinnedTabs'] = []
d['favorites'] = [
  {'url': 'https://c1.test/', 'title': 'C ONE',   'profileID': pid},
  {'url': 'https://c2.test/', 'title': 'C TWO',   'profileID': pid},
  {'url': 'https://c3.test/', 'title': 'C THREE', 'profileID': pid},
  {'url': 'https://c4.test/', 'title': 'C FOUR',  'profileID': pid},
]"
run "cancelled and cross-grid favourite drag" build/scripts/fav-cancel-probe.mjs

# 12. Only the tab sections scroll, and a swipe locks them while it runs.
#     Needs enough tabs to OVERFLOW the pane or the scroll assertions pass for
#     the wrong reason - there would be nothing to scroll.
fixture "d['spaces'][0]['tabs'] = [
  {'url': 'https://t%d.test/' % i, 'title': 'TAB %d' % i} for i in range(14)
]
d['spaces'][0]['pinnedTabs'] = [{'url': 'https://p1.test/', 'title': 'PIN'}]
d['favorites'] = [{'url': 'https://f1.test/', 'title': 'F1', 'profileID': pid}]"
run "scroll zone and swipe lock" build/scripts/scrollzone-probe.mjs

echo
echo "=============== SUMMARY"
fail=0
for i in "${!NAMES[@]}"; do
  printf '%-6s %s\n' "${RESULTS[$i]}" "${NAMES[$i]}"
  [ "${RESULTS[$i]}" = FAIL ] && fail=1
done
exit $fail
