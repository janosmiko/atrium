#!/usr/bin/env python3
"""Grade one launch attempt: did Atrium actually start, or hand off to a
process already running?

A run that aborts can leave Atrium alive in the VM. The suite runs with
SKIP_RESTORE=1, so nothing resets that between scenarios. The next launch
does not fail - Chromium's ProcessSingleton notifies the leftover process and
the NEW launch exits immediately, while the OLD process keeps running. Every
assertion that follows then grades whatever the OLD process shows, which has
nothing to do with the scenario that just staged fresh fixtures for it.
Observed 2026-08-10: a profile picker left over from an aborted run scored a
scenario 1 of 4, and only a manual clean re-run showed the real result.

TWO INDEPENDENT SIGNALS, same division as quit_outcome.py:

  THE HARNESS'S OWN BOOKKEEPING - a pgrep taken BEFORE the launch is even
  attempted. If Atrium is already there, the scenario should refuse to start
  rather than stage fixtures for a launch that cannot possibly use them.

  THE APP'S OWN ACCOUNT - the launch log line ProcessSingleton writes when it
  notifies an existing process instead of starting fresh (observed as
  "NotifyResult=1"; also documented in run-atrium.sh as printing "Opening in
  existing browser session"). This is what catches the race the preflight
  pgrep cannot: something starts in the gap between that check and the launch
  itself.

The preflight signal is checked first and wins when both fire - it is the
earlier, plainer fact, and reporting the handoff on top of it would just be
racing a check against itself.

CLI:
  launch_outcome.py <pids-before> <log-file>
  launch_outcome.py dead <log-file>

<pids-before> is raw pgrep output (possibly multi-line/CRLF) taken before the
launch was attempted. <log-file> holds the just-launched app's run log, or is
empty/absent when it could not be read.

Exit codes: 0 clean start, 2 a prior instance was already running, 3 this
launch handed off through ProcessSingleton. One evidence line on stdout
either way.

`dead` mode is a separate question from all of the above: not
"which process is this", but "did any process ever come up at all". Call it
only once the harness already knows the launch produced no running process
(pgrep found nothing a couple of seconds after the launch command returned) -
it always exits 1, and its only job is to name the real cause from the run
log rather than leave a reader to infer it from assertions that measured
nothing.
"""
import re
import sys

from quit_outcome import pids

# Chromium's process_singleton_posix.cc logs this when NotifyOtherProcess
# succeeds in reaching an already-running instance (NotifyResult::
# PROCESS_NOTIFIED == 1). run-atrium.sh documents the same handoff's
# human-visible half as "Opening in existing browser session.".
SINGLETON_HANDOFF = re.compile(
    r"NotifyResult=1|Opening in existing browser session", re.IGNORECASE)


def already_running(before_pids_text):
    """(bool, evidence) - was Atrium already running before this launch."""
    running = pids(before_pids_text)
    if running:
        return True, ("Atrium was already running (pid %s) before this "
                       "launch was even attempted - refusing to start over "
                       "it; the next launch would hand off to it instead of "
                       "starting fresh" % ", ".join(running))
    return False, "no Atrium process was running before this launch"


def singleton_handoff_line(log_text):
    """The launch log's ProcessSingleton handoff line, or None."""
    for line in (log_text or "").splitlines():
        if SINGLETON_HANDOFF.search(line):
            return line.strip()
    return None


def grade(before_pids_text, log_text):
    """(exit code, evidence line). See the module doc for what each code means."""
    running, evidence = already_running(before_pids_text)
    if running:
        return 2, evidence

    handoff = singleton_handoff_line(log_text)
    if handoff:
        return 3, ("this launch handed off through Chromium's ProcessSingleton "
                    "instead of starting Atrium fresh: %s - pgrep saw no prior "
                    "instance beforehand, so this is a race the preflight "
                    "check could not see; every assertion after this measures "
                    "the OLD process, not the one this scenario just launched"
                    % handoff)

    return 0, ("no prior instance, and the launch log carries no "
                "ProcessSingleton handoff line")


def dead_launch_evidence(log_text):
    """Evidence line naming why a launch never produced a running process
   . Read only from what the launch itself wrote - the app's own
    account, since there is no process left to ask the harness's bookkeeping
    about.

    Observed 2026-08-10, twice in one hour: the binary missing (`env` writes
    "No such file or directory" to the log before Atrium ever gets to run),
    and a deploy that never landed one (SKIP_DEPLOY=1 combined with a restore
    that wiped the app - same empty log, no distinguishing line). Either way
    the run then carried on grading untouched fixture values as if they were
    product failures - the whole reason this check exists.
    """
    text = log_text or ""
    for line in text.splitlines():
        if "No such file or directory" in line:
            return "the Atrium binary is missing: %s" % line.strip()
    if not text.strip():
        return ("Atrium never wrote a single log line - the launch never "
                 "produced a running process (binary missing, or a deploy "
                 "that never landed one)")
    return ("Atrium's process ended before staying up - last log lines: %s"
             % " | ".join(text.strip().splitlines()[-3:]))


def _read(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as handle:
            return handle.read()
    except OSError:
        return ""


def main(argv):
    if len(argv) != 3:
        sys.stdout.write(__doc__ or "")
        return 1
    if argv[1] == "dead":
        print(dead_launch_evidence(_read(argv[2])))
        return 1
    before_pids_text, log_path = argv[1:]
    code, evidence = grade(before_pids_text, _read(log_path))
    print(evidence)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
