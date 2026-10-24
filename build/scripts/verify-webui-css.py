#!/usr/bin/env python3
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
"""Asserts that every WebUI stylesheet still parses as CSS.

A browser never reports a broken stylesheet. It discards everything up to and
through the next block and renders on, so a syntax error reads as a theming bug.
An edit that opened a comment inside an open comment cost the address bar its
whole rule, and the WebUI suite stayed green because those tests read the
stylesheet as text rather than parsing it.
"""

import pathlib
import re
import sys

# A selector may hold names, combinators, attribute and pseudo syntax, and a
# leading at-rule. It never holds a slash, so prose left at the top level by a
# mis-closed comment shows up as one - along with the orphaned close marker.
SELECTOR = re.compile(r'[-\w\s.,:#>~\[\]="\'()*%+@&]+')


def nested_comment_openers(text):
    """Line numbers where a comment opens inside an already-open comment."""
    hits = []
    for match in re.finditer(r'/\*', text):
        close = text.find('*/', match.start() + 2)
        body = text[match.start() + 2:close if close != -1 else len(text)]
        if '/*' in body:
            hits.append(text[:match.start()].count('\n') + 1)
    return hits


def unparsable_selectors(text):
    """Top-level preludes that no browser can read as a selector."""
    stripped = re.sub(r'/\*.*?\*/', '', text, flags=re.DOTALL)
    bad, depth, buf = [], 0, ''
    for char in stripped:
        if char == '{':
            if depth == 0:
                prelude = buf.strip()
                if prelude and not SELECTOR.fullmatch(prelude):
                    bad.append(' '.join(prelude.split())[:80])
                buf = ''
            depth += 1
        elif char == '}':
            depth -= 1
            buf = ''
        elif depth == 0:
            buf += char
    return bad


def check(path):
    text = path.read_text(encoding='utf-8')
    problems = []
    for line in nested_comment_openers(text):
        problems.append(f'{path}:{line}: a comment opens inside an open comment')
    for prelude in unparsable_selectors(text):
        problems.append(f'{path}: unparsable rule, the browser drops it: {prelude}')
    if text.count('/*') != text.count('*/'):
        problems.append(f'{path}: unbalanced comment markers')
    return problems


def main():
    repo = pathlib.Path(__file__).resolve().parents[2]
    roots = [repo / 'build' / 'generated']
    src = pathlib.Path(
        __import__('os').environ.get(
            'ATRIUM_CHROMIUM_SRC',
            pathlib.Path.home() / 'chromium-atrium' / 'src'))
    webui = src / 'chrome' / 'browser' / 'resources' / 'atrium'
    if webui.is_dir():
        roots.append(webui)

    sheets = sorted(p for root in roots if root.is_dir()
                    for p in root.rglob('*.css'))
    if not sheets:
        print('warning: no stylesheets found to check', file=sys.stderr)
        return 0

    problems = [p for sheet in sheets for p in check(sheet)]
    if problems:
        for problem in problems:
            print(f'FAIL: {problem}', file=sys.stderr)
        print(f'FAIL: {len(problems)} problem(s) in {len(sheets)} stylesheet(s)',
              file=sys.stderr)
        return 1
    print(f'verify OK: {len(sheets)} stylesheet(s) parse')
    return 0


def selftest():
    """Proves the guard fails, so a green run means something."""
    broken = '/* one /* two */\n.a { color: red }\n'
    if not nested_comment_openers(broken):
        print('selftest FAIL: the nested opener was not caught', file=sys.stderr)
        return 1
    orphan = '.b (a sentence left behind) minus the lab\'s */\n.c { color: red }\n'
    if not unparsable_selectors(orphan):
        print('selftest FAIL: the orphaned prose was not caught', file=sys.stderr)
        return 1
    if unparsable_selectors('.a, .b > .c:hover[data-x="1"] { color: red }\n'):
        print('selftest FAIL: a real selector was rejected', file=sys.stderr)
        return 1
    print('selftest OK: the guard catches a nested opener and orphaned prose')
    return 0


if __name__ == '__main__':
    sys.exit(selftest() if '--selftest' in sys.argv else main())
