#!/usr/bin/env python3
"""Tests for pick_renderer.choose. No VM, no build: `python3 <file>`.

The process lines below are real `ps -axww -o pid=,command=` output from the
probe VM, trimmed to the switches that matter. The commit lines are real run-log
lines. Inventing either would test the parser against a shape the machine does
not produce.

The refusal to signal the browser process is what this file exists for. It
cannot be proven in the VM without risking the thing it is protecting.
"""
import unittest

import pick_renderer

HELPER = ("/Users/admin/atrium-probe/Atrium.app/Contents/Frameworks/"
          "Atrium Framework.framework/Versions/151.0.7922.47/Helpers/"
          "Atrium Helper (Renderer).app/Contents/MacOS/Atrium Helper (Renderer)")
BROWSER_EXE = "/Users/admin/atrium-probe/Atrium.app/Contents/MacOS/Atrium"

BROWSER = ("2958 " + BROWSER_EXE + " --user-data-dir=/Users/admin/Library/"
           "Application Support/Atrium/Chromium --no-first-run "
           "--no-default-browser-check --enable-logging=stderr --v=1")
GPU = ("2967 /Users/admin/atrium-probe/Atrium.app/Contents/Frameworks/"
       "Atrium Framework.framework/Versions/151.0.7922.47/Helpers/"
       "Atrium Helper.app/Contents/MacOS/Atrium Helper --type=gpu-process "
       "--user-data-dir=/Users/admin/Library/Application Support/Atrium/Chromium")
NETWORK = ("2968 /Users/admin/atrium-probe/Atrium.app/Contents/Frameworks/"
           "Atrium Framework.framework/Versions/151.0.7922.47/Helpers/"
           "Atrium Helper.app/Contents/MacOS/Atrium Helper --type=utility "
           "--utility-sub-type=network.mojom.NetworkService --lang=en-US")


def renderer(pid, top_chrome=False, extension=False):
    switches = " --type=renderer"
    if top_chrome:
        switches += " --top-chrome-webui"
    if extension:
        switches += " --extension-process"
    switches += (" --gpu-client-id=6 --user-data-dir=/Users/admin/Library/"
                 "Application Support/Atrium/Chromium --lang=en-US "
                 "--renderer-client-id=%d --enable-logging=stderr --v=1" % pid)
    return "%d %s%s" % (pid, HELPER, switches)


def commit(pid, url):
    return ("[%d:34062:0809/235215.924014:VERBOSE1:"
            "content/renderer/render_frame_impl.cc:4013] "
            "Committed provisional load: %s" % (pid, url))


def choose(target, ps_lines, commit_lines=(), app="Atrium"):
    return pick_renderer.choose(
        target,
        pick_renderer.parse_ps("\n".join(ps_lines)),
        pick_renderer.parse_commits("\n".join(commit_lines)),
        app)


TOP_CHROME = "chrome://atrium.top-chrome/sidebar"
LIVE = [BROWSER, GPU, NETWORK,
        renderer(2971, top_chrome=True), renderer(2974), renderer(2975)]
LIVE_COMMITS = [commit(2971, TOP_CHROME),
                commit(2971, "chrome://atrium.top-chrome/pill"),
                commit(2974, "http://127.0.0.1:8000/favicon-page.html")]


class ChooseChrome(unittest.TestCase):
    def test_picks_the_top_chrome_renderer(self):
        code, pid, why = choose("chrome", LIVE, LIVE_COMMITS)
        self.assertEqual((code, pid), (0, 2971), why)

    def test_skips_a_spare_that_committed_nothing(self):
        # A spare top-chrome renderer carries the switch but hosts no surface.
        procs = LIVE + [renderer(3001, top_chrome=True)]
        code, pid, why = choose("chrome", procs, LIVE_COMMITS)
        self.assertEqual((code, pid), (0, 2971), why)

    def test_refuses_to_guess_between_two_live_ones(self):
        procs = LIVE + [renderer(3001, top_chrome=True)]
        commits = LIVE_COMMITS + [commit(3001, "chrome://atrium.top-chrome/foyer")]
        code, _, why = choose("chrome", procs, commits)
        self.assertEqual(code, 3, why)
        self.assertIn("refusing to guess", why)

    def test_not_found_when_no_chrome_layer_renderer_exists(self):
        code, _, why = choose("chrome", [BROWSER, GPU, renderer(2974)],
                              [commit(2974, "http://example.com/")])
        self.assertEqual(code, 4, why)


class ChooseWeb(unittest.TestCase):
    def test_picks_the_committed_web_renderer(self):
        code, pid, why = choose("web", LIVE, LIVE_COMMITS)
        self.assertEqual((code, pid), (0, 2974), why)

    def test_never_picks_the_chrome_layer_renderer(self):
        code, pid, why = choose("web", LIVE, LIVE_COMMITS)
        self.assertNotEqual(pid, 2971, why)
        self.assertEqual(code, 0, why)

    def test_ignores_an_extension_renderer(self):
        procs = [BROWSER, renderer(2980, extension=True)]
        code, _, why = choose("web", procs,
                              [commit(2980, "chrome-extension://abc/page.html")])
        self.assertEqual(code, 4, why)


class RefusesTheBrowser(unittest.TestCase):
    def test_the_browser_target_is_refused_by_name(self):
        code, pid, why = choose("browser", LIVE, LIVE_COMMITS)
        self.assertEqual(code, 3)
        self.assertIsNone(pid)
        self.assertIn("quit", why)

    def test_a_process_running_the_browser_binary_is_never_signalled(self):
        # The invariant, not the happy path: identity comes from the binary, so
        # a line claiming --type=renderer while running the browser executable
        # is still refused. Nothing in Chromium writes such a line - that is the
        # point of asserting it here rather than hoping.
        doctored = ("2958 " + BROWSER_EXE + " --type=renderer "
                    "--top-chrome-webui --user-data-dir=/tmp")
        code, pid, why = choose("chrome", [doctored], [commit(2958, TOP_CHROME)])
        self.assertEqual(code, 3, why)
        self.assertIsNone(pid)
        self.assertIn("browser binary", why)

    def test_no_live_pick_is_ever_a_browser_pid(self):
        procs = pick_renderer.parse_ps("\n".join(LIVE))
        browsers = pick_renderer.browser_pids(procs, "Atrium")
        self.assertEqual(browsers, [2958])
        for target in pick_renderer.TARGETS:
            _, pid, why = choose(target, LIVE, LIVE_COMMITS)
            self.assertNotIn(pid, browsers, "%s -> %s" % (target, why))

    def test_unknown_target_is_a_usage_error_not_a_pick(self):
        code, pid, _ = choose("everything", LIVE, LIVE_COMMITS)
        self.assertEqual(code, 2)
        self.assertIsNone(pid)


class Parsing(unittest.TestCase):
    def test_bundle_paths_with_spaces_survive(self):
        procs = pick_renderer.parse_ps(renderer(2971, top_chrome=True))
        self.assertEqual(procs[0].exe, HELPER)
        self.assertEqual(procs[0].ptype, "renderer")
        self.assertTrue(procs[0].top_chrome)

    def test_a_switch_prefix_is_not_the_switch(self):
        procs = pick_renderer.parse_ps(
            "1 /x/Atrium Helper --type=renderer --top-chrome-webui-spare")
        self.assertFalse(procs[0].top_chrome)

    def test_commit_lines_map_routes_to_the_pid_that_loaded_them(self):
        commits = pick_renderer.parse_commits("\n".join(LIVE_COMMITS))
        self.assertEqual(commits[2971],
                         [TOP_CHROME, "chrome://atrium.top-chrome/pill"])
        self.assertEqual(commits[2974],
                         ["http://127.0.0.1:8000/favicon-page.html"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
