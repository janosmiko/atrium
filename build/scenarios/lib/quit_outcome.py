#!/usr/bin/env python3
"""Grade one shutdown: did Cmd+Q end the app, or did a fallback?

Backs both places this harness stops the app - assert_quit in lib/asserts.sh
and shutdown_app in vm-scenario.sh - so the two cannot answer the same question
differently. Same division of labour as pick_renderer.py and file_value.py: the
runner does the driving inside the VM, the grading happens on the host next to
the artifacts a dispute would be settled from.

WHY `pgrep` ALONE IS NOT AN ANSWER. shutdown_app sends Cmd+Q, waits fifteen
seconds and then sends SIGTERM. The process is gone either way, so a check that
only asks "is it still there" is answered by the fallback rather than by the
chord. Measured 2026-08-10: settings-confirm-quit-toggle scored 12 passed, 0
failed while its own log carried "Cmd+Q did not stop Atrium within 15s -
sending SIGTERM". Two separate bugs that week were surfaces swallowing Cmd+Q
and in both the scenario that caught it did so through
some other assertion, never the quit.

TWO INDEPENDENT SIGNALS, for the same reason pick_renderer.py insists on them:

  THE HARNESS'S OWN BOOKKEEPING - was the process there before the chord, after
  the chord window, after the fallback. This is what the runner watched happen.

  THE APP'S OWN ACCOUNT - Chromium's shutdown_signal_handlers_posix.cc prints
  "Handling shutdown for signal 15." when the browser exits on a POSIX signal,
  and prints nothing of the sort when it quits through its own quit path.
  Checked both ways against 2026-08-10 artifacts: present in the launch that
  needed SIGTERM (settings-confirm-quit-toggle), absent in every run whose
  Cmd+Q worked.

The second is what catches the case the first cannot see. A process that was
already gone when the chord window closed looks like a perfect quit to pgrep,
even when it went because something signalled it - a stray kill, a crash
handler, a previous phase's teardown. So a shutdown-signal line in the log
FAILS the quit no matter what the process table said.

The pids are carried through rather than reduced to a yes/no because the
evidence line has to name them: "still running" is an outcome, "still running
as pid 6960" is a measurement, and the fidelity rules in docs/BUILD.md ask for
the second one.

CLI:
  quit_outcome.py <proc> <chord-window-s> <fallback> \\
                  <pids-before> <pids-after-chord> <pids-after-fallback> \\
                  <log-file>

<fallback> is `none` when the caller sent no fallback at all (assert_quit does
not - a wedged app there is the finding), or the signal name it did send.
<log-file> holds the app's shutdown-signal lines, or is empty/absent when the
log could not be read.

Exit codes: 0 quit on the chord, 3 only the fallback ended it, 4 still running,
5 gone but on a signal rather than through the quit path, 1 nothing measured.
One evidence line on stdout either way.
"""
import re
import sys

# Chromium logs this from shutdown_signal_handlers_posix.cc for every POSIX
# signal it handles. SIGKILL cannot be caught, so it leaves no line - which is
# why the runner's fallback is SIGTERM and not a kill.
SIGNAL_LINE = re.compile(r"Handling shutdown for signal (\d+)")
SIGNAL_NAMES = {1: "SIGHUP", 2: "SIGINT", 3: "SIGQUIT", 15: "SIGTERM"}


def signals_in(log_text):
    """[(number, name, whole line)] for every shutdown-signal line, in order."""
    found = []
    for line in (log_text or "").splitlines():
        match = SIGNAL_LINE.search(line)
        if match:
            number = int(match.group(1))
            found.append((number, SIGNAL_NAMES.get(number, "signal %d" % number),
                          line.strip()))
    return found


def pids(text):
    """pgrep output -> a list of pid strings. Empty means no such process."""
    return [token for token in (text or "").replace("\r", " ").split() if token]


def _as_list(values):
    return ", ".join(values) if values else "<none>"


def grade(proc, window_s, fallback, before, after_chord, after_fallback, log_text):
    """(exit code, evidence line). See the module doc for what each code means."""
    started = pids(before)
    survived_chord = pids(after_chord)
    survived_fallback = pids(after_fallback)
    seen = signals_in(log_text)
    quiet = ("and its log carries no 'Handling shutdown for signal' line, so "
             "nothing signalled it")

    if not started:
        return 1, ("nothing was measured: no '%s' process was running when "
                   "Cmd+Q was sent, so this shutdown cannot say whether the "
                   "chord works. The app ended before the harness asked it to."
                   % proc)

    if survived_chord:
        if fallback == "none":
            return 4, ("Cmd+Q did not stop '%s' within %ss - still running as "
                       "pid %s. No fallback was sent, so this is where it "
                       "stands." % (proc, window_s, _as_list(survived_chord)))
        if survived_fallback:
            return 4, ("Cmd+Q did not stop '%s' within %ss, and neither did the "
                       "%s fallback - still running as pid %s."
                       % (proc, window_s, fallback, _as_list(survived_fallback)))
        note = (" The app agrees: %s" % seen[-1][2]) if seen else ""
        return 3, ("Cmd+Q was SWALLOWED: '%s' ran on as pid %s for the whole %ss "
                   "window and only ended because the harness sent %s "
                   "(fallback=%s).%s"
                   % (proc, _as_list(survived_chord), window_s, fallback,
                      fallback, note))

    if seen:
        number, name, line = seen[-1]
        return 5, ("'%s' was gone within %ss of Cmd+Q, but it did not quit - it "
                   "exited on %s (signal %d), which is not the app's own quit "
                   "path: %s" % (proc, window_s, name, number, line))

    return 0, ("'%s' exited on Cmd+Q within %ss (was pid %s), %s - no fallback "
               "fired" % (proc, window_s, _as_list(started), quiet))


def _read(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            return handle.read()
    except OSError:
        return ""


def main(argv):
    if len(argv) != 8:
        sys.stdout.write(__doc__)
        return 1
    proc, window_s, fallback, before, after_chord, after_fallback, log_path = argv[1:]
    code, evidence = grade(proc, window_s, fallback, before, after_chord,
                           after_fallback, _read(log_path))
    print(evidence)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
