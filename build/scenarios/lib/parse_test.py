#!/usr/bin/env python3
"""Tests for parse.invalid_regex_patterns. No VM, no build: `python3 <file>`.

An early assertion used a PCRE backreference the harness's grep -aE cannot
parse - it burned a full VM run before anyone noticed. These tests pin the
hard-error check that now catches that class of pattern at validate() time,
before any VM run.
"""
import unittest

import parse


def _spec(**assertions_kw):
    return {
        "name": "t",
        "fixture": "f.json",
        "actions": [{"type": "settle", "ms": 100}],
        "assertions": [assertions_kw],
    }


class InvalidRegexPatterns(unittest.TestCase):
    def test_lookahead_backreference_pattern_is_rejected(self):
        # The actual pattern the original assertion shipped: a PCRE
        # lookahead combined with a backreference to it. grep -aE reports
        # this as "Invalid back reference" - a plain lone \1 is accepted by
        # this grep as a POSIX-extension backreference, so the combination
        # is what proves the syntax error, not \1 alone.
        spec = _spec(
            type="vlog",
            pattern=r"line one\n(?!.*line one \1)",
        )
        errors = parse.invalid_regex_patterns(spec)
        self.assertEqual(len(errors), 1)
        self.assertIn("not a valid grep -aE", errors[0])

    def test_unbalanced_paren_pattern_is_rejected(self):
        spec = _spec(type="vlog", pattern="a(b")
        errors = parse.invalid_regex_patterns(spec)
        self.assertEqual(len(errors), 1)

    def test_ordinary_pattern_with_digits_is_accepted(self):
        spec = _spec(type="vlog", pattern="Atrium: tab [0-9]+ closed")
        self.assertEqual(parse.invalid_regex_patterns(spec), [])

    def test_pattern_that_finds_no_match_is_not_a_syntax_error(self):
        # An empty /dev/null target means every pattern "finds nothing" -
        # that must never be confused with "does not compile".
        spec = _spec(type="vlog", pattern="this string appears nowhere")
        self.assertEqual(parse.invalid_regex_patterns(spec), [])

    def test_checks_after_pattern_on_vlog_after_too(self):
        spec = _spec(type="vlog_after", pattern="ok", after_pattern="a(b")
        errors = parse.invalid_regex_patterns(spec)
        self.assertEqual(len(errors), 1)
        self.assertIn("after_pattern", errors[0])

    def test_non_vlog_assertion_types_are_not_checked(self):
        spec = _spec(type="quit")
        self.assertEqual(parse.invalid_regex_patterns(spec), [])

    def test_shell_metacharacters_in_pattern_are_not_interpreted(self):
        # Passed as an argv element to grep, never through a shell - this
        # must be rejected as "no match", not executed or crash the check.
        spec = _spec(type="vlog", pattern="foo; rm -rf / #")
        self.assertEqual(parse.invalid_regex_patterns(spec), [])


class AxWindowKindValidation(unittest.TestCase):
    """a kind gate that cannot fail is worse than none at all.

    An absent pattern matches every window and an absent expected compares
    against the empty string, so both shapes produce a green line describing a
    measurement that never happened. validate() refuses them before the six
    minutes a VM run costs.
    """

    @staticmethod
    def _errors(**kw):
        spec = _spec(**kw)
        spec["startup_space"] = "s"
        return [e for e in parse.validate(spec) if "ax_window_kind" in e]

    def test_pattern_and_expected_together_are_accepted(self):
        self.assertEqual(self._errors(type="ax_window_kind", pattern="/foyer", expected=1), [])

    def test_zero_is_a_real_expectation(self):
        # The control asserts NO little window exists; zero must not be read
        # as "unset".
        self.assertEqual(self._errors(type="ax_window_kind", pattern="/foyer", expected=0), [])

    def test_missing_pattern_is_rejected(self):
        self.assertTrue(self._errors(type="ax_window_kind", expected=1))

    def test_empty_pattern_is_rejected(self):
        self.assertTrue(self._errors(type="ax_window_kind", pattern="", expected=1))

    def test_missing_expected_is_rejected(self):
        self.assertTrue(self._errors(type="ax_window_kind", pattern="/foyer"))

    def test_boolean_expected_is_rejected(self):
        # True is an int in Python; it is not a window count.
        self.assertTrue(self._errors(type="ax_window_kind", pattern="/foyer", expected=True))


class EvalUntilAssertionShape(unittest.TestCase):
    """An eval_until answer is stored raw, an eval answer JSON-quoted.

    42 scenarios asserted an eval_until value in the quoted form, so
    those assertions could never pass. A guaranteed failure reads as a product
    defect, and everything behind the failed gate runs against unestablished
    state, so the whole run is wasted. Caught at validate() time instead.
    """

    @staticmethod
    def _errors(until_expected, asserted):
        spec = {
            "name": "t",
            "fixture": "f.json",
            "startup_space": "s",
            "actions": [{
                "type": "eval_until",
                "id": "page",
                "target": "/sidebar",
                "expression": "location.href",
                "expected": until_expected,
            }],
            "assertions": [{
                "type": "eval_equals",
                "id": "page",
                "expected": asserted,
            }],
        }
        return [e for e in parse.validate(spec) if "eval_until" in e]

    def test_matching_shapes_are_accepted(self):
        self.assertEqual(self._errors("http://x/y.html", "http://x/y.html"), [])

    def test_the_quoted_form_is_rejected(self):
        errors = self._errors("http://x/y.html", '"http://x/y.html"')
        self.assertTrue(errors)
        self.assertIn("can never pass", errors[0])

    def test_a_bare_eval_is_left_alone(self):
        # Only eval_until stores raw, so an eval-backed assertion keeping its
        # quotes must not be flagged.
        spec = {
            "name": "t",
            "fixture": "f.json",
            "startup_space": "s",
            "actions": [{
                "type": "eval",
                "id": "page",
                "target": "/sidebar",
                "expression": "String(1)",
            }],
            "assertions": [{
                "type": "eval_equals",
                "id": "page",
                "expected": '"1"',
            }],
        }
        self.assertEqual([e for e in parse.validate(spec) if "eval_until" in e], [])


class EmbeddedNewlines(unittest.TestCase):
    """encode() emits field values raw into one tab-delimited line per record.

    a two-paragraph desc on a region_mean assertion crossed to bash
    as two lines, and the runner read the second paragraph as a record of an
    unknown type mid-run. validate() refuses the newline before a VM run
    spends six minutes discovering it.
    """

    def test_a_desc_with_a_newline_is_rejected(self):
        spec = _spec(type="quit", desc="first paragraph\n\nsecond paragraph")
        errors = [e for e in parse.validate(spec) if "newline" in e]
        self.assertTrue(errors)

    def test_an_action_note_with_a_newline_is_rejected(self):
        spec = _spec(type="quit")
        spec["actions"] = [{"type": "settle", "ms": 100, "note": "a\nb"}]
        errors = [e for e in parse.validate(spec) if "newline" in e]
        self.assertTrue(errors)

    def test_a_single_line_desc_is_accepted(self):
        spec = _spec(type="quit", desc="one line, however long it runs")
        self.assertEqual([e for e in parse.validate(spec) if "newline" in e], [])

    def test_file_contains_value_may_hold_a_newline(self):
        # encode() ships this one field as json.dumps output, so its newline
        # crosses escaped and never splits the record.
        spec = _spec(type="file_contains", path="f", pointer="/a", value="x\ny")
        self.assertEqual([e for e in parse.validate(spec) if "newline" in e], [])


class MoveDuringCapture(unittest.TestCase):
    def _spec_with(self, action):
        return {
            "name": "t",
            "fixture": "f.json",
            "actions": [action],
            "assertions": [{"type": "quit"}],
        }

    def test_both_probe_placements_at_once_are_rejected(self):
        spec = self._spec_with({
            "type": "move_during_capture", "x": 12, "y": 400,
            "capture_id": "c", "mid_at_ms": 300, "mid_after_move_ms": 90,
        })
        self.assertTrue([e for e in parse.validate(spec) if "not both" in e])

    def test_a_mid_id_it_registers_is_assertable(self):
        # The _mid id exists only because this action makes it. An assertion
        # naming it must validate, or the action is unusable for its purpose.
        spec = self._spec_with({
            "type": "move_during_capture", "x": 12, "y": 400,
            "capture_id": "c", "mid_after_move_ms": 90,
        })
        spec["assertions"] = [{
            "type": "region_mean", "capture": "c_mid",
            "x": 1, "y": 1, "w": 2, "h": 2, "max_mean": 200,
        }]
        self.assertEqual(parse.validate(spec), [])


class RegionSteady(unittest.TestCase):
    def _errors(self, **kw):
        spec = _spec(type="region_steady", **kw)
        spec["actions"] = [{"type": "capture", "id": "c", "duration_ms": 100, "fps": 10}]
        return parse.validate(spec)

    def test_a_full_region_steady_is_accepted(self):
        self.assertEqual(self._errors(capture="c", region="1,2,3,4", max_step=8), [])

    def test_a_region_steady_with_no_step_is_rejected(self):
        self.assertEqual(len(self._errors(capture="c", region="1,2,3,4")), 1)

    def test_a_region_steady_on_an_unknown_capture_is_rejected(self):
        self.assertEqual(len(self._errors(capture="x", region="1,2,3,4", max_step=8)), 1)


class RegionEveryFrame(unittest.TestCase):
    def _errors(self, **kw):
        spec = _spec(type="region_every_frame", **kw)
        spec["actions"] = [{"type": "capture", "id": "c", "duration_ms": 100, "fps": 10}]
        return parse.validate(spec)

    def test_a_full_region_every_frame_is_accepted(self):
        self.assertEqual(self._errors(capture="c", x=1, y=2, w=3, h=4, max_mean=120), [])

    def test_a_region_every_frame_with_no_bound_is_rejected(self):
        self.assertEqual(len(self._errors(capture="c", x=1, y=2, w=3, h=4)), 1)

    def test_a_region_every_frame_on_an_unknown_capture_is_rejected(self):
        self.assertEqual(len(self._errors(capture="x", x=1, y=2, w=3, h=4, max_mean=120)), 1)


class ClickPoint(unittest.TestCase):
    def _errors(self, click):
        spec = _spec(type="eval_equals", id="p", expected="1")
        spec["actions"] = [click]
        return [e for e in parse.validate(spec) if "click" in e]

    def test_a_click_from_an_eval_needs_no_x_and_y(self):
        self.assertEqual(self._errors({"type": "click", "from_eval": "p"}), [])

    def test_a_click_with_no_point_is_rejected(self):
        self.assertEqual(len(self._errors({"type": "click"})), 1)

    def test_a_rightclick_from_an_eval_needs_no_x_and_y(self):
        self.assertEqual(self._errors({"type": "rightclick", "from_eval": "p"}), [])

    def test_a_rightclick_with_no_point_is_rejected(self):
        self.assertEqual(len(self._errors({"type": "rightclick"})), 1)


class LoadExtensionDir(unittest.TestCase):
    def test_load_extension_dir_is_a_meta_key(self):
        self.assertIn("load_extension_dir", parse.META_KEYS)


if __name__ == "__main__":
    unittest.main()
