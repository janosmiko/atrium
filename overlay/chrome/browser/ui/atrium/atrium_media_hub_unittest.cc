// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_media_hub_unittest.cc

#include "chrome/browser/ui/atrium/atrium_media_hub.h"

#include <optional>
#include <string>

#include "base/json/json_reader.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

MediaRecord MakeRecord(uint64_t tab_id, const std::string& profile_id) {
  MediaRecord record;
  record.tab_id = tab_id;
  record.profile_id = profile_id;
  record.title = "Big Buck Bunny";
  record.favicon_url = "https://example.test/favicon.ico";
  record.host = "example.test";
  record.playing = true;
  record.muted = false;
  record.can_prev = true;
  record.can_next = true;
  record.can_seek = false;
  record.picture_in_picture = true;
  return record;
}

const base::DictValue& FirstEntry(const base::Value& parsed) {
  return parsed.GetList().front().GetDict();
}

TEST(AtriumMediaHubTest, RecordCarriesEveryField) {
  MediaRecordList list("work");
  ASSERT_TRUE(list.Upsert(MakeRecord(41, "work")));

  std::optional<base::Value> parsed =
      base::JSONReader::Read(list.ToJson(), base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed && parsed->is_list());
  ASSERT_EQ(1u, parsed->GetList().size());

  const base::DictValue& entry = FirstEntry(*parsed);
  EXPECT_EQ("41", *entry.FindString("id"));
  EXPECT_EQ("Big Buck Bunny", *entry.FindString("title"));
  EXPECT_EQ("https://example.test/favicon.ico", *entry.FindString("faviconURL"));
  EXPECT_EQ("example.test", *entry.FindString("host"));
  EXPECT_EQ(true, entry.FindBool("playing"));
  EXPECT_EQ(false, entry.FindBool("muted"));
  EXPECT_EQ(true, entry.FindBool("canPrev"));
  EXPECT_EQ(true, entry.FindBool("canNext"));
  EXPECT_EQ(false, entry.FindBool("canSeek"));
  EXPECT_EQ(true, entry.FindBool("pictureInPicture"));
}

TEST(AtriumMediaHubTest, UpsertReplacesTheSameTab) {
  MediaRecordList list("work");
  ASSERT_TRUE(list.Upsert(MakeRecord(41, "work")));

  MediaRecord paused = MakeRecord(41, "work");
  paused.playing = false;
  ASSERT_TRUE(list.Upsert(paused));

  ASSERT_EQ(1u, list.records().size());
  EXPECT_FALSE(list.records().front().playing);
}

TEST(AtriumMediaHubTest, ForeignProfileRecordIsRefused) {
  MediaRecordList list("work");
  ASSERT_TRUE(list.Upsert(MakeRecord(41, "work")));

  EXPECT_FALSE(list.Upsert(MakeRecord(42, "work/otr")));
  EXPECT_FALSE(list.Owns(42));
  EXPECT_EQ(1u, list.records().size());
  EXPECT_EQ("[]", MediaRecordList("work/otr").ToJson());
}

TEST(AtriumMediaHubTest, RemoveDropsOnlyThatTab) {
  MediaRecordList list("work");
  ASSERT_TRUE(list.Upsert(MakeRecord(41, "work")));
  ASSERT_TRUE(list.Upsert(MakeRecord(42, "work")));

  EXPECT_TRUE(list.Remove(41));
  EXPECT_FALSE(list.Remove(41));
  EXPECT_FALSE(list.Owns(41));
  EXPECT_TRUE(list.Owns(42));
  EXPECT_EQ(1u, list.records().size());
}

}  // namespace
}  // namespace atrium
