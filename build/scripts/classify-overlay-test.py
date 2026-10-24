#!/usr/bin/env python3
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
"""Unit tests for classify_path in classify-overlay.py."""

import importlib.util
import os
import unittest

# The sibling module's filename has a hyphen, so plain `import` can't reach
# it - load it from its path instead.
_SPEC = importlib.util.spec_from_file_location(
    "classify_overlay",
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "classify-overlay.py"),
)
classify_overlay = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(classify_overlay)
ROOTS = classify_overlay.ROOTS
classify_path = classify_overlay.classify_path


class ClassifyPathTest(unittest.TestCase):
    def test_new_file_under_owned_root_is_overlay(self):
        self.assertEqual(
            classify_path("chrome/browser/ui/atrium/x.cc", set(), ROOTS), "overlay"
        )

    def test_upstream_file_under_owned_root_is_patch(self):
        base = {"chrome/browser/ui/atrium/x.cc"}
        self.assertEqual(
            classify_path("chrome/browser/ui/atrium/x.cc", base, ROOTS), "patch"
        )

    def test_new_file_outside_owned_root_is_foreign(self):
        self.assertEqual(
            classify_path("chrome/browser/ui/views/y.cc", set(), ROOTS), "foreign"
        )

    def test_upstream_file_outside_owned_root_is_patch(self):
        base = {"chrome/browser/ui/views/y.cc"}
        self.assertEqual(
            classify_path("chrome/browser/ui/views/y.cc", base, ROOTS), "patch"
        )

    def test_chromium_adds_file_at_owned_path_is_patch_not_overlay(self):
        # A future Chromium bump can land a file at a path this project owns.
        # It must classify as patch, the same as any other upstream file
        # Atrium later touches - never silently as overlay.
        base = {"chrome/browser/resources/atrium/newly_added_upstream.ts"}
        self.assertEqual(
            classify_path(
                "chrome/browser/resources/atrium/newly_added_upstream.ts",
                base,
                ROOTS,
            ),
            "patch",
        )

    def test_prefix_collision_does_not_count_as_owned_root(self):
        # "third_party/atrium-fonts" shares a string prefix with the owned
        # root "third_party/atrium" but is a sibling directory, not a path
        # under it.
        self.assertEqual(
            classify_path("third_party/atrium-fonts/font.ttf", set(), ROOTS),
            "foreign",
        )

    def test_new_file_under_webui_root_is_overlay(self):
        self.assertEqual(
            classify_path(
                "chrome/browser/ui/webui/atrium/x.cc", set(), ROOTS
            ),
            "overlay",
        )


if __name__ == "__main__":
    unittest.main()
