// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_settings_json_cache.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/run_loop.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/threading/thread_restrictions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

std::string ReadFile(const base::FilePath& path) {
  std::string contents;
  base::ReadFileToString(path, &contents);
  return contents;
}

// Stands in for the window: an answer that must not arrive after it is gone.
class Waiter {
 public:
  explicit Waiter(bool* answered) : answered_(answered) {}

  void OnJson(const std::string&) { *answered_ = true; }

  base::WeakPtr<Waiter> GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

 private:
  raw_ptr<bool> answered_;
  base::WeakPtrFactory<Waiter> weak_factory_{this};
};

// The browser's own shape: every window past the first is built after
// BrowserMainLoop disallows blocking, so a read on the asking sequence aborts.
TEST(AtriumSettingsJsonCacheTest, ReadsWithBlockingDisallowedOnTheAskingThread) {
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath path = dir.GetPath().AppendASCII("settings.json");
  ASSERT_TRUE(base::WriteFile(path, "{\"searchEngines\":[]}"));

  std::string got;
  {
    base::ScopedDisallowBlocking no_blocking;
    SettingsJsonCache cache;
    base::RunLoop loop;
    cache.Get(base::ThreadPool::CreateTaskRunner(
                  {base::MayBlock(), base::TaskPriority::BEST_EFFORT}),
              base::BindOnce(&ReadFile, path),
              base::BindLambdaForTesting([&](const std::string& json) {
                got = json;
                loop.Quit();
              }));
    loop.Run();
  }

  EXPECT_EQ(got, "{\"searchEngines\":[]}");
}

// One read for the process, however many windows ask.
TEST(AtriumSettingsJsonCacheTest, ReadsOnceAndAnswersEveryCaller) {
  base::test::TaskEnvironment task_environment;
  SettingsJsonCache cache;
  scoped_refptr<base::TaskRunner> runner =
      base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()});

  std::atomic<int> reads{0};
  auto loader = [&] {
    return base::BindLambdaForTesting([&] {
      ++reads;
      return std::string("{\"a\":1}");
    });
  };
  std::vector<std::string> answers;
  auto answer = [&] {
    return base::BindLambdaForTesting(
        [&](const std::string& json) { answers.push_back(json); });
  };

  cache.Get(runner, loader(), answer());
  cache.Get(runner, loader(), answer());
  task_environment.RunUntilIdle();
  EXPECT_EQ(reads.load(), 1);
  EXPECT_EQ(answers.size(), 2u);

  // Held now, so this one is answered without a file touch at all.
  cache.Get(runner, loader(), answer());
  EXPECT_EQ(answers.size(), 3u);
  task_environment.RunUntilIdle();
  EXPECT_EQ(reads.load(), 1);
  EXPECT_EQ(answers, std::vector<std::string>(
                         {"{\"a\":1}", "{\"a\":1}", "{\"a\":1}"}));
}

// A window closed mid-read drops its own answer and leaves the others theirs.
TEST(AtriumSettingsJsonCacheTest, SkipsAWaiterThatWentAwayAndKeepsTheRest) {
  base::test::TaskEnvironment task_environment;
  SettingsJsonCache cache;
  scoped_refptr<base::TaskRunner> runner =
      base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()});

  bool closed_answered = false;
  bool live_answered = false;
  auto closed = std::make_unique<Waiter>(&closed_answered);
  Waiter live(&live_answered);

  cache.Get(runner,
            base::BindLambdaForTesting([] { return std::string("{}"); }),
            base::BindOnce(&Waiter::OnJson, closed->GetWeakPtr()));
  cache.Get(runner,
            base::BindLambdaForTesting([] { return std::string("{}"); }),
            base::BindOnce(&Waiter::OnJson, live.GetWeakPtr()));
  closed.reset();
  task_environment.RunUntilIdle();

  EXPECT_FALSE(closed_answered);
  EXPECT_TRUE(live_answered);
}

}  // namespace
}  // namespace atrium
