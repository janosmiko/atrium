#!/usr/bin/env python3
"""Choose the renderer process a `crash_renderer` action may signal.

The runner is bash and the process list arrives over ssh, but the CHOICE of
which process to signal is made here, on the host, so it can be tested against
recorded `ps` output without a VM (see pick_renderer_test.py). Picking wrong is
not a failed assertion - it is a crash report about a process nobody meant to
touch, or about the browser.

A renderer qualifies only when TWO independent signals agree:

  1. the switch Chromium gave the process. Only a render process host created
     for a top-chrome WebUI gets --top-chrome-webui
     (content/browser/renderer_host/render_process_host_impl.cc, in
     AppendRendererCommandLine).
  2. a route that process actually committed, read out of the run log.

The switch alone also matches a SPARE renderer, which has committed nothing -
crashing that one crashes no surface, and the run would report a recovery that
was never tested.

Usage:
  pick_renderer.py <target> <ps-file> <commit-file> [<app-process>]

  <target>       chrome | web   (see TARGETS)
  <ps-file>      output of `ps -axww -o pid=,command=` inside the VM
  <commit-file>  the run log's "Committed provisional load:" lines
  <app-process>  the browser process name, default Atrium

Prints the chosen pid on stdout, and the reasoning on stderr for the runner to
record as evidence.

Exit codes:
  0  a pid was chosen
  2  usage
  3  REFUSED - the target names the browser process, resolves to a process
     running the browser binary, or is ambiguous. Refusing to guess is the
     point: an action that kills an arbitrary renderer is worse than no action,
     because every result it produces has to be thrown away.
  4  nothing matched
"""
import re
import sys

# The renderers a scenario may name.
TARGETS = {
    "chrome": "the shared chrome-layer renderer - sidebar, command bar, pill, "
              "foyer and settings all live in this one process",
    "web": "the web-content renderer showing an http(s) page",
}

# Names that exist ONLY so the refusal is a branch with a message rather than
# an accident of which strings happen to be in TARGETS. parse.py rejects the
# same name at validation time, so a spec can never reach this.
REFUSED_TARGETS = {
    "browser": "killing the browser process is the `quit` action's job",
}

_PS_LINE = re.compile(r"^\s*(\d+)\s+(\S.*)$")
_TYPE = re.compile(r"(?:^|\s)--type=([a-z-]+)")
_COMMIT = re.compile(r"\[(\d+):\d+:[^\]]*\]\s*Committed provisional load:\s*(\S+)")
_TOP_CHROME_URL = re.compile(r"^chrome://[^/]*\.top-chrome/")


def _has_switch(args, name):
    """True when `args` carries --<name> as a whole switch, not as a prefix."""
    return re.search(r"(?:^|\s)--" + re.escape(name) + r"(?:\s|=|$)", args) is not None


class Proc:
    """One line of `ps -axww -o pid=,command=`."""

    def __init__(self, pid, exe, args):
        self.pid = pid
        self.exe = exe
        self.args = args
        found = _TYPE.search(args)
        self.ptype = found.group(1) if found else ""
        self.top_chrome = _has_switch(args, "top-chrome-webui")
        self.extension = _has_switch(args, "extension-process")


def parse_ps(text):
    """`ps -axww -o pid=,command=` output -> [Proc].

    The executable path is everything before the first " --", because macOS
    bundle paths are full of spaces ("Atrium Helper (Renderer).app") and
    splitting on whitespace mangles them.
    """
    procs = []
    for line in text.splitlines():
        match = _PS_LINE.match(line)
        if not match:
            continue
        rest = match.group(2)
        cut = rest.find(" --")
        exe, args = (rest[:cut], rest[cut:]) if cut >= 0 else (rest, "")
        procs.append(Proc(int(match.group(1)), exe, args))
    return procs


def parse_commits(text):
    """Run-log lines -> {pid: [committed url, ...]}.

    The pid is the first field of Chromium's own log prefix, so this maps a
    route to the process that loaded it without asking the app for anything.
    """
    commits = {}
    for line in text.splitlines():
        match = _COMMIT.search(line)
        if match:
            commits.setdefault(int(match.group(1)), []).append(match.group(2))
    return commits


def browser_pids(procs, app):
    """pids running the BROWSER binary itself, helpers excluded.

    Identity comes from the executable name, never from the switches on the
    line: a process claiming --type=renderer while running the browser binary
    must still be refused, and that is the invariant the caller checks.
    """
    return [p.pid for p in procs if p.exe.rsplit("/", 1)[-1] == app]


def choose(target, procs, commits, app="Atrium"):
    """-> (exit code, pid or None, reasoning)."""
    if target in REFUSED_TARGETS:
        return 3, None, "refusing target '%s': %s" % (target, REFUSED_TARGETS[target])
    if target not in TARGETS:
        return 2, None, "unknown target '%s' (want one of %s)" % (
            target, ", ".join(sorted(TARGETS)))

    browsers = browser_pids(procs, app)
    renderers = [p for p in procs if p.ptype == "renderer"]

    if target == "chrome":
        flagged = [p for p in renderers if p.top_chrome]
        def wanted(url):
            return _TOP_CHROME_URL.match(url) is not None
        what = "--top-chrome-webui plus a committed chrome://*.top-chrome/ route"
    else:
        flagged = [p for p in renderers if not p.top_chrome and not p.extension]
        def wanted(url):
            return url.startswith(("http://", "https://"))
        what = "no --top-chrome-webui plus a committed http(s) page"

    candidates = [p for p in flagged
                  if any(wanted(url) for url in commits.get(p.pid, ()))]

    seen = "renderers seen: %s; %s browser: %s" % (
        ", ".join("%d(top_chrome=%d,committed=%d)"
                  % (p.pid, p.top_chrome, len(commits.get(p.pid, ())))
                  for p in renderers) or "<none>",
        app,
        ", ".join(str(pid) for pid in browsers) or "<none>")

    if not candidates:
        return 4, None, "no renderer with %s. %s" % (what, seen)
    if len(candidates) > 1:
        return 3, None, "refusing to guess: %d renderers match %s (pids %s). %s" % (
            len(candidates), what,
            ", ".join(str(p.pid) for p in candidates), seen)

    pick = candidates[0]
    if pick.pid in browsers:
        return 3, None, "refusing to signal pid %d: it runs the %s browser binary" % (
            pick.pid, app)
    if pick.ptype != "renderer":
        return 3, None, "refusing to signal pid %d: --type=%s is not a renderer" % (
            pick.pid, pick.ptype or "<none>")
    return 0, pick.pid, "pid %d matches %s. %s" % (pick.pid, what, seen)


def _read(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            return handle.read()
    except OSError:
        return ""


def main(argv):
    if len(argv) < 4:
        sys.stderr.write(__doc__)
        return 2
    target, ps_path, commit_path = argv[1], argv[2], argv[3]
    app = argv[4] if len(argv) > 4 else "Atrium"
    code, pid, message = choose(target, parse_ps(_read(ps_path)),
                                parse_commits(_read(commit_path)), app)
    sys.stderr.write(message + "\n")
    if code == 0:
        print(pid)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
