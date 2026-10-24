#!/usr/bin/env python3
# Copyright 2026 The Atrium Authors
# Use of this source code is governed by the Apache License, Version 2.0
# found in the LICENSE file.
"""Builds a places.sqlite file from its bookmarks.json twin.

bookmarks.json holds the rows of the importer's bookmarks query, which the
Rust tests read. The VM scenario needs the SQLite file that C++ reads, so both
come from one source. The script runs the query on its output and exits with
an error when the rows differ from the twin.

An optional visits.json twin adds history rows. Its visits are days before
now, so a scenario packs them when it stages the fixture: a committed
visit_date would age out of the importer's 90-day window.

  python3 build/scripts/places-pack.py <bookmarks.json> <places.sqlite> \
      [visits.json]
"""

import json
import os
import sqlite3
import sys
import time

# The query in atrium_settings_source.cc, word for word.
BOOKMARKS_QUERY = (
    "SELECT b.id, b.parent, b.type, b.position, b.title, b.guid, p.url, "
    "p.last_visit_date FROM moz_bookmarks b LEFT JOIN moz_places p "
    "ON p.id = b.fk ORDER BY b.parent, b.position")
BOOKMARKS_COLUMNS = ("id", "parent", "type", "position", "title", "guid",
                     "url", "last_visit_date")
# The query in atrium_import_history.cc, word for word.
HISTORY_QUERY = (
    "SELECT p.url, p.title, p.visit_count, "
    "(SELECT count(*) FROM moz_historyvisits t "
    "WHERE t.place_id = p.id AND t.visit_type = 2), "
    "v.visit_date / 1000 FROM moz_historyvisits v "
    "JOIN moz_places p ON p.id = v.place_id "
    "WHERE p.hidden = 0 AND v.visit_type IN (1, 2, 3) "
    "AND v.visit_date > (strftime('%s', 'now') - 7776000) * 1000000 "
    "ORDER BY v.visit_date DESC")
DAY_S = 86400


def pack_bookmarks(db, rows):
    places = {}
    for row in rows:
        url = row.get("url")
        if url is not None and url not in places:
            places[url] = len(places) + 1
            db.execute(
                "INSERT INTO moz_places (id, url, last_visit_date) "
                "VALUES (?, ?, ?)",
                (places[url], url, row.get("last_visit_date")))
        db.execute("INSERT INTO moz_bookmarks VALUES (?, ?, ?, ?, ?, ?, ?)",
                   (row["id"], row["type"], places.get(url), row["parent"],
                    row["position"], row.get("title"), row["guid"]))
    got = [dict(zip(BOOKMARKS_COLUMNS, r)) for r in db.execute(BOOKMARKS_QUERY)]
    if got != rows:
        sys.exit("places.sqlite does not read back as bookmarks.json")
    return places


def pack_visits(db, pages, places):
    now_s = int(time.time())
    expected = 0
    for page in pages:
        place_id = places.get(page["url"])
        if place_id is None:
            place_id = len(places) + 1
            places[page["url"]] = place_id
            db.execute(
                "INSERT INTO moz_places (id, url, last_visit_date) "
                "VALUES (?, ?, NULL)", (place_id, page["url"]))
        db.execute(
            "UPDATE moz_places SET title = ?, visit_count = ?, hidden = ? "
            "WHERE id = ?",
            (page["title"], len(page["visits"]), page["hidden"], place_id))
        for visit in page["visits"]:
            at = (now_s - visit["days_ago"] * DAY_S) * 1000000
            db.execute(
                "INSERT INTO moz_historyvisits "
                "(place_id, visit_date, visit_type) VALUES (?, ?, ?)",
                (place_id, at, visit["visit_type"]))
            if (not page["hidden"] and visit["days_ago"] < 90
                    and visit["visit_type"] in (1, 2, 3)):
                expected += 1
    got = len(db.execute(HISTORY_QUERY).fetchall())
    if got != expected:
        sys.exit(f"places.sqlite reads {got} visits, visits.json expects "
                 f"{expected}")


def main(source, target, visits_source=None):
    with open(source, encoding="utf-8") as f:
        rows = json.load(f)
    for suffix in ("", "-wal", "-shm"):
        if os.path.exists(target + suffix):
            os.remove(target + suffix)
    db = sqlite3.connect(target)
    # Firefox keeps places.sqlite in WAL mode, and the reader must open that.
    db.execute("PRAGMA journal_mode=WAL")
    db.execute("CREATE TABLE moz_places (id INTEGER PRIMARY KEY, "
               "url TEXT UNIQUE, last_visit_date INTEGER, title TEXT, "
               "visit_count INTEGER DEFAULT 0, hidden INTEGER DEFAULT 0)")
    db.execute("CREATE TABLE moz_bookmarks (id INTEGER PRIMARY KEY, "
               "type INTEGER, fk INTEGER, parent INTEGER, position INTEGER, "
               "title TEXT, guid TEXT)")
    db.execute("CREATE TABLE moz_historyvisits (id INTEGER PRIMARY KEY, "
               "place_id INTEGER, visit_date INTEGER, visit_type INTEGER)")
    places = pack_bookmarks(db, rows)
    if visits_source:
        with open(visits_source, encoding="utf-8") as f:
            pack_visits(db, json.load(f), places)
    db.commit()
    db.close()


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    main(*sys.argv[1:])
