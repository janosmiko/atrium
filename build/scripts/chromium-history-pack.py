#!/usr/bin/env python3
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
"""Builds a Chromium History file from its history.json twin.

The twin gives each visit as days before now, so the file is built when a
scenario stages it. A committed file would age out of the 90-day window that
the importer reads. The script runs the importer's query on its output and
exits with an error when the row count differs from the twin.

  python3 build/scripts/chromium-history-pack.py <history.json> <History>
"""

import json
import os
import sqlite3
import sys
import time

# The query in atrium_import_history.cc, word for word.
QUERY = ("SELECT u.url, u.title, u.visit_count, u.typed_count, "
         "v.visit_time / 1000 FROM visits v JOIN urls u ON u.id = v.url "
         "WHERE u.hidden = 0 AND (v.transition & 536870912) != 0 "
         "AND (v.transition & 255) NOT IN (3, 4, 10) "
         "AND v.visit_time > "
         "(strftime('%s', 'now') + 11644473600 - 7776000) * 1000000 "
         "ORDER BY v.visit_time DESC")
# Chromium counts microseconds from 1601, not from 1970.
WINDOWS_EPOCH_OFFSET_S = 11644473600
CHAIN_START_AND_END = 0x10000000 | 0x20000000
SKIPPED_CORE_TYPES = (3, 4, 10)
DAY_US = 86400 * 1000000


def main(source, target):
    with open(source, encoding="utf-8") as f:
        pages = json.load(f)
    for suffix in ("", "-journal", "-wal"):
        if os.path.exists(target + suffix):
            os.remove(target + suffix)
    now_us = int((time.time() + WINDOWS_EPOCH_OFFSET_S) * 1000000)
    db = sqlite3.connect(target)
    db.execute("CREATE TABLE urls (id INTEGER PRIMARY KEY, url LONGVARCHAR, "
               "title LONGVARCHAR, visit_count INTEGER DEFAULT 0 NOT NULL, "
               "typed_count INTEGER DEFAULT 0 NOT NULL, "
               "last_visit_time INTEGER NOT NULL, "
               "hidden INTEGER DEFAULT 0 NOT NULL)")
    db.execute("CREATE TABLE visits (id INTEGER PRIMARY KEY, "
               "url INTEGER NOT NULL, visit_time INTEGER NOT NULL, "
               "from_visit INTEGER, transition INTEGER DEFAULT 0 NOT NULL)")
    expected = 0
    for url_id, page in enumerate(pages, start=1):
        visits = page["visits"]
        times = [now_us - v["days_ago"] * DAY_US for v in visits]
        db.execute("INSERT INTO urls VALUES (?, ?, ?, ?, ?, ?, ?)",
                   (url_id, page["url"], page["title"], len(visits),
                    page["typed_count"], max(times), page["hidden"]))
        for visit, at in zip(visits, times):
            db.execute("INSERT INTO visits (url, visit_time, transition) "
                       "VALUES (?, ?, ?)",
                       (url_id, at, visit["transition"] | CHAIN_START_AND_END))
            if (not page["hidden"] and visit["days_ago"] < 90
                    and (visit["transition"] & 0xFF) not in SKIPPED_CORE_TYPES):
                expected += 1
    db.commit()
    got = len(db.execute(QUERY).fetchall())
    db.close()
    if got != expected:
        sys.exit(f"{target} reads {got} visits, {source} expects {expected}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
