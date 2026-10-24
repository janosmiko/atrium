#!/usr/bin/env python3
"""Tests for file_value.check. No VM, no build: `python3 <file>`.

The documents below are the real staged fixtures - build/scenarios/fixtures/
confirm-quit-off.settings.json and presenter-startup.spaces.json - trimmed only
where a key plays no part. Writing a smaller invented shape would test the
walker against a document the harness never stages.

The cases that matter are the ones where a WEAKER comparison would have passed:
a substring hit inside a longer number, a bool that came back as a string, and
a fixture that already held the answer.
"""
import json
import unittest

import file_value

SETTINGS = json.dumps({
    "schemaVersion": 1,
    "appIconVariant": "solarized-ink",
    "animationsEnabled": True,
    "startupBehavior": "restoreLastActive",
    "downloadsDirectory": None,
    "promptForDownload": False,
    "confirmToQuitEnabled": False,
    "glassOpacity": 0.55,
    "opaqueSurround": False,
})

FOREIGN = "11111111-1111-1111-1111-111111111111"
THEMED = "22222222-2222-2222-2222-222222222222"

SPACES = json.dumps({
    "spaces": [
        {"id": FOREIGN, "name": "Foreign", "order": 0, "profileID": "Profile 1",
         "tabs": [{"url": "https://example.org/", "title": "Example Domain"}]},
        {"id": THEMED, "name": "Themed", "order": 1, "profileID": "Default",
         "tabs": [{"url": "https://example.com/", "title": "Example Domain"}]},
    ],
    "appState": {"sidebarWidth": 280, "schemaVersion": 1,
                 "activeSpaceID": FOREIGN},
})


def spaces_with_active(space_id):
    doc = json.loads(SPACES)
    doc["appState"]["activeSpaceID"] = space_id
    return json.dumps(doc)


def settings_with(key, value):
    doc = json.loads(SETTINGS)
    doc[key] = value
    return json.dumps(doc)


class TheWriteHappened(unittest.TestCase):
    def test_a_changed_value_passes(self):
        code, why = file_value.check(spaces_with_active(THEMED), SPACES,
                                     "appState.activeSpaceID",
                                     json.dumps(THEMED))
        self.assertEqual(code, 0, why)
        self.assertIn(FOREIGN, why, "the evidence must name what was staged")

    def test_a_bool_that_flipped_passes(self):
        code, why = file_value.check(settings_with("confirmToQuitEnabled", True),
                                     SETTINGS, "confirmToQuitEnabled", "true")
        self.assertEqual(code, 0, why)

    def test_reformatting_alone_does_not_fail_it(self):
        # The writer is serde and the fixture is hand-written, so key order and
        # spacing differ on every real run. Neither is what persistence means.
        pretty = json.dumps(json.loads(spaces_with_active(THEMED)),
                            indent=4, sort_keys=True)
        code, why = file_value.check(pretty, SPACES, "appState.activeSpaceID",
                                     json.dumps(THEMED))
        self.assertEqual(code, 0, why)

    def test_a_list_index_reaches_into_the_spaces_array(self):
        moved = json.loads(SPACES)
        moved["spaces"][1]["tabs"][0]["url"] = "https://example.net/"
        code, why = file_value.check(json.dumps(moved), SPACES,
                                     "spaces.1.tabs.0.url",
                                     '"https://example.net/"')
        self.assertEqual(code, 0, why)


class TheWriteDidNot(unittest.TestCase):
    def test_an_unchanged_value_fails(self):
        # Nothing wrote, so the file still reads as it was staged. This is the
        # case the whole assertion exists for.
        code, why = file_value.check(SPACES, SPACES, "appState.activeSpaceID",
                                     json.dumps(THEMED))
        self.assertEqual(code, 3, why)
        self.assertIn(FOREIGN, why)

    def test_a_missing_path_fails_and_says_how_far_it_got(self):
        code, why = file_value.check(SPACES, SPACES,
                                     "appState.confirmToQuitEnabled", "true")
        self.assertEqual(code, 4, why)
        self.assertIn("appState", why)

    def test_an_index_past_the_end_is_absent_not_a_crash(self):
        code, why = file_value.check(SPACES, SPACES, "spaces.9.name", '"Third"')
        self.assertEqual(code, 4, why)


class WhatAWeakerComparisonWouldHaveMissed(unittest.TestCase):
    def test_a_substring_of_a_longer_number_is_not_the_value(self):
        # grep for '"schemaVersion": 1' matches this file. The value is 13.
        code, why = file_value.check(settings_with("schemaVersion", 13),
                                     settings_with("schemaVersion", 2),
                                     "schemaVersion", "1")
        self.assertEqual(code, 3, why)

    def test_a_bool_written_as_a_string_is_a_failed_round_trip(self):
        code, why = file_value.check(settings_with("confirmToQuitEnabled", "true"),
                                     SETTINGS, "confirmToQuitEnabled", "true")
        self.assertEqual(code, 3, why)

    def test_true_is_not_one(self):
        # json.loads gives True == 1 in Python. A settings write that turned a
        # bool into a number is exactly the round-trip defect worth catching.
        code, why = file_value.check(settings_with("confirmToQuitEnabled", 1),
                                     SETTINGS, "confirmToQuitEnabled", "true")
        self.assertEqual(code, 3, why)

    def test_a_number_that_only_looks_equal_is_still_compared(self):
        code, why = file_value.check(settings_with("glassOpacity", 0.55),
                                     settings_with("glassOpacity", 0.9),
                                     "glassOpacity", "0.55")
        self.assertEqual(code, 0, why)


class TheFixtureCannotAnswerForTheApp(unittest.TestCase):
    def test_a_baseline_that_already_matches_is_a_failure(self):
        code, why = file_value.check(spaces_with_active(THEMED),
                                     spaces_with_active(THEMED),
                                     "appState.activeSpaceID",
                                     json.dumps(THEMED))
        self.assertEqual(code, 5, why)
        self.assertIn("never wrote anything", why)

    def test_no_baseline_at_all_is_a_harness_fault(self):
        code, why = file_value.check(spaces_with_active(THEMED), None,
                                     "appState.activeSpaceID",
                                     json.dumps(THEMED))
        self.assertEqual(code, 1, why)

    def test_a_baseline_missing_the_path_still_lets_the_check_run(self):
        # Nothing staged at that path, so a value there can only have been
        # written. That is the opposite of the guard case, not a variant of it.
        code, why = file_value.check(settings_with("surroundColor", "#ff0000"),
                                     SETTINGS, "surroundColor", '"#ff0000"')
        self.assertEqual(code, 0, why)
        self.assertIn("<absent>", why)


class BadInput(unittest.TestCase):
    def test_a_file_that_is_not_json_is_a_harness_fault_not_a_mismatch(self):
        code, why = file_value.check("<html>404</html>", SETTINGS,
                                     "schemaVersion", "1")
        self.assertEqual(code, 1, why)
        self.assertIn("read back from the VM", why)

    def test_an_expected_value_that_is_not_json_is_refused(self):
        code, why = file_value.check(SETTINGS, SETTINGS, "schemaVersion", "one")
        self.assertEqual(code, 1, why)


class Selecting(unittest.TestCase):
    def test_walking_stops_at_the_first_segment_that_is_not_there(self):
        found, reached = file_value.select(json.loads(SPACES),
                                           "appState.nope.deeper")
        self.assertIs(found, file_value.MISSING)
        self.assertEqual(reached, "appState")

    def test_a_digit_segment_only_indexes_a_list(self):
        # "1" under a dict is a key lookup, not an index, and this document has
        # no such key - so it is absent rather than the second space.
        found, _ = file_value.select(json.loads(SPACES), "appState.1")
        self.assertIs(found, file_value.MISSING)

    def test_a_null_in_the_document_is_a_value_not_an_absence(self):
        found, _ = file_value.select(json.loads(SETTINGS), "downloadsDirectory")
        self.assertIsNone(found)
        self.assertIsNot(found, file_value.MISSING)


if __name__ == "__main__":
    unittest.main(verbosity=2)
