// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_visit_thumbnail_store_unittest.cc

#include "chrome/browser/ui/atrium/atrium_visit_thumbnail_store.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

std::string Thumb(int marker) {
  return "data:image/jpeg;base64,thumb-" + std::to_string(marker);
}

// Records `count` stops on `tab_id`, each carrying its own recognisable
// thumbnail, so a later read says WHICH stop it landed on rather than only
// that something is there.
void RecordStops(VisitThumbnailStore& store, uint64_t tab_id, int count) {
  for (int i = 0; i < count; ++i) {
    store.AddStop(tab_id);
    ASSERT_TRUE(store.SetNewestThumbnail(tab_id, store.StopSerial(tab_id),
                                         Thumb(i)));
  }
}

TEST(VisitThumbnailStore, StampsOntoTheNewestStop) {
  VisitThumbnailStore store(4);
  store.AddStop(7);
  store.AddStop(7);
  EXPECT_TRUE(store.SetNewestThumbnail(7, store.StopSerial(7), Thumb(1)));
  EXPECT_EQ("", store.ThumbnailAt(7, 0));
  EXPECT_EQ(Thumb(1), store.ThumbnailAt(7, 1));
}

TEST(VisitThumbnailStore, AStopWithNoCaptureReadsEmptyRatherThanMissing) {
  VisitThumbnailStore store(4);
  store.AddStop(7);
  EXPECT_EQ(1u, store.StopCount(7));
  EXPECT_EQ("", store.ThumbnailAt(7, 0));
  EXPECT_EQ("", store.ThumbnailAt(7, 1));
  EXPECT_EQ("", store.ThumbnailAt(99, 0));
}

TEST(VisitThumbnailStore, ACaptureNeedsAStopToLandOn) {
  VisitThumbnailStore store(4);
  EXPECT_FALSE(store.SetNewestThumbnail(7, 0, Thumb(1)));
  EXPECT_EQ(0u, store.TabCount());
}

// THE bound. Tab ids are minted forever and a busy tab navigates all day, so
// an unbounded deque here grows for the life of the process.
TEST(VisitThumbnailStore, NeverExceedsTheCap) {
  VisitThumbnailStore store(kMaxVisitThumbnails);
  for (int i = 0; i < 500; ++i) {
    store.AddStop(7);
    ASSERT_LE(store.StopCount(7), store.capacity()) << "overflowed at stop "
                                                    << i;
  }
  EXPECT_EQ(kMaxVisitThumbnails, store.StopCount(7));
}

// Dropping the newest would make the gesture stop working on exactly the tabs
// it gets used on most, which is why visits.rs drops the oldest and why this
// has to agree.
TEST(VisitThumbnailStore, TheCapEvictsTheOldestStop) {
  VisitThumbnailStore store(2);
  RecordStops(store, 7, 3);
  EXPECT_EQ(2u, store.StopCount(7));
  EXPECT_EQ(Thumb(1), store.ThumbnailAt(7, 0));
  EXPECT_EQ(Thumb(2), store.ThumbnailAt(7, 1));
}

// The reason the cap has to be the core's cap. Both logs shift down by one on
// the same stop, so index i keeps meaning the same page on both sides.
TEST(VisitThumbnailStore, AnEvictionShiftsEveryThumbnailDownOneIndex) {
  VisitThumbnailStore store(kMaxVisitThumbnails);
  RecordStops(store, 7, static_cast<int>(kMaxVisitThumbnails));
  EXPECT_EQ(Thumb(0), store.ThumbnailAt(7, 0));
  EXPECT_EQ(Thumb(1), store.ThumbnailAt(7, 1));

  store.AddStop(7);
  ASSERT_TRUE(store.SetNewestThumbnail(7, store.StopSerial(7), Thumb(999)));
  EXPECT_EQ(kMaxVisitThumbnails, store.StopCount(7));
  EXPECT_EQ(Thumb(1), store.ThumbnailAt(7, 0)) << "stop 0 did not shift out";
  EXPECT_EQ(Thumb(2), store.ThumbnailAt(7, 1));
  EXPECT_EQ(Thumb(999), store.ThumbnailAt(7, kMaxVisitThumbnails - 1))
      << "the newest stop carries the capture taken for it";
}

// A closed tab's captures go with it.
TEST(VisitThumbnailStore, ForgettingATabDropsItsThumbnails) {
  VisitThumbnailStore store(4);
  RecordStops(store, 7, 2);
  RecordStops(store, 8, 2);

  store.Forget(7);
  EXPECT_EQ(0u, store.StopCount(7));
  EXPECT_EQ("", store.ThumbnailAt(7, 0));
  EXPECT_EQ(1u, store.TabCount()) << "the other tab's timeline is not its own";
  EXPECT_EQ(Thumb(0), store.ThumbnailAt(8, 0));
}

// A capture is asynchronous, so a fast second navigation lands its stop before
// the first page's picture arrives. Stamping the newest slot blindly would put
// the OLD page on the NEW stop.
TEST(VisitThumbnailStore, ACaptureThatLandsAfterTheTabMovedOnIsRefused) {
  VisitThumbnailStore store(4);
  store.AddStop(7);
  const uint64_t serial_in_flight = store.StopSerial(7);
  store.AddStop(7);

  EXPECT_FALSE(store.SetNewestThumbnail(7, serial_in_flight, Thumb(1)));
  EXPECT_EQ("", store.ThumbnailAt(7, 1));
  EXPECT_TRUE(store.SetNewestThumbnail(7, store.StopSerial(7), Thumb(2)));
  EXPECT_EQ(Thumb(2), store.ThumbnailAt(7, 1));
}

// The serial keeps counting past the cap, so a tab already at sixty stops can
// still tell a stale capture from a current one.
TEST(VisitThumbnailStore, TheSerialKeepsCountingPastTheCap) {
  VisitThumbnailStore store(2);
  store.AddStop(7);
  const uint64_t serial_in_flight = store.StopSerial(7);
  store.AddStop(7);
  store.AddStop(7);
  EXPECT_EQ(2u, store.StopCount(7));
  EXPECT_EQ(3u, store.StopSerial(7));
  EXPECT_FALSE(store.SetNewestThumbnail(7, serial_in_flight, Thumb(1)));
}

TEST(VisitThumbnailStore, TabsDoNotShareSlots) {
  VisitThumbnailStore store(4);
  RecordStops(store, 7, 1);
  RecordStops(store, 8, 1);
  EXPECT_EQ(Thumb(0), store.ThumbnailAt(7, 0));
  EXPECT_EQ(Thumb(0), store.ThumbnailAt(8, 0));
  EXPECT_EQ(2u, store.TabCount());
}

// The process-wide store is the one the browser actually reads, so its cap is
// the one that has to match the core.
TEST(VisitThumbnailStore, TheProcessStoreCarriesTheCoresCap) {
  EXPECT_EQ(kMaxVisitThumbnails, VisitThumbnails().capacity());
  EXPECT_EQ(60u, kMaxVisitThumbnails) << "visits.rs MAX_VISITS is 60";
}

}  // namespace
}  // namespace atrium
