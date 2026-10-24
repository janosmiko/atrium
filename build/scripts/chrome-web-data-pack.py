#!/usr/bin/env python3
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
"""Builds a Chrome Web Data file from its keywords.json twin.

The Rust tests read keywords.json. The VM scenario needs the real SQLite file
that C++ reads, so both come from one source.

  python3 build/scripts/chrome-web-data-pack.py <keywords.json> <Web Data>
"""

import json
import os
import sqlite3
import sys

COLUMNS = ("short_name", "keyword", "url", "safe_for_autoreplace",
           "prepopulate_id", "created_by_policy", "is_active",
           "starter_pack_id", "sync_guid")


def main(source, target):
    with open(source, encoding="utf-8") as f:
        rows = json.load(f)
    if os.path.exists(target):
        os.remove(target)
    db = sqlite3.connect(target)
    db.execute("CREATE TABLE keywords (id INTEGER PRIMARY KEY, %s)" %
               ", ".join(COLUMNS))
    db.executemany(
        "INSERT INTO keywords (%s) VALUES (%s)" %
        (", ".join(COLUMNS), ", ".join("?" * len(COLUMNS))),
        [tuple(row.get(c) for c in COLUMNS) for row in rows])
    db.commit()
    db.close()


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
