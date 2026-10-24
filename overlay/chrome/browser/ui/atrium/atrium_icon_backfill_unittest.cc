// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_icon_backfill_unittest.cc

#include "chrome/browser/ui/atrium/atrium_icon_backfill.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "base/json/json_reader.h"
#include "net/base/net_errors.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace atrium {
namespace {

base::DictValue Model(const char* json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  EXPECT_TRUE(parsed && parsed->is_dict()) << json;
  return std::move(*parsed).TakeDict();
}

// Nothing is cached yet, which is the state after a cold launch.
bool NoneCached(const std::string& url) {
  return false;
}

std::vector<std::string> Select(const char* json,
                                const std::string& profile_id = "p",
                                const std::string& unowned_id = "p") {
  return SelectMissingIconURLs(Model(json), profile_id, unowned_id, NoneCached);
}

TEST(SelectMissingIconURLs, TakesTabsPinnedAndFavourites) {
  EXPECT_EQ(
      (std::vector<std::string>{"https://a/", "https://b/", "https://c/"}),
      Select(R"({"tabs":[{"url":"https://a/"}],)"
             R"("pinned":[{"url":"https://b/"}],)"
             R"("favorites":[{"id":"https://c/"}]})"));
}

// A restored tab has no live WebContents, so its row carries a url and no id.
// Selecting on `id` would leave exactly the rows this exists for.
TEST(SelectMissingIconURLs, TakesAColdRowWithNoTabId) {
  EXPECT_EQ((std::vector<std::string>{"https://cold/"}),
            Select(R"({"tabs":[{"url":"https://cold/","id":""}]})"));
}

// A pinned folder has no "url" of its own. Its members do, in its "tabs".
TEST(SelectMissingIconURLs, ReachesAPinnedFoldersMembers) {
  EXPECT_EQ(
      (std::vector<std::string>{"https://member/"}),
      Select(R"({"pinned":[{"kind":"folder","id":"G",)"
             R"("tabs":[{"url":"https://member/"}]}]})"));
}

TEST(SelectMissingIconURLs, ReachesFoldersAndEverySpace) {
  EXPECT_EQ(
      (std::vector<std::string>{"https://folder/", "https://space/",
                                "https://spacefolder/"}),
      Select(R"({"folders":[{"tabs":[{"url":"https://folder/"}]}],)"
             R"("spaces":[{"tabs":[{"url":"https://space/"}],)"
             R"("folders":[{"tabs":[{"url":"https://spacefolder/"}]}]}]})"));
}

TEST(SelectMissingIconURLs, SkipsWhatIsAlreadyCached) {
  const base::DictValue model =
      Model(R"({"tabs":[{"url":"https://have/"},{"url":"https://want/"}]})");
  const auto have = [](const std::string& url) { return url == "https://have/"; };
  EXPECT_EQ((std::vector<std::string>{"https://want/"}),
            SelectMissingIconURLs(model, "p", "p", have));
}

// One favourite and one open tab for the same page are one lookup, and a push
// runs this on every navigation.
TEST(SelectMissingIconURLs, NamesEachURLOnce) {
  EXPECT_EQ((std::vector<std::string>{"https://a/"}),
            Select(R"({"tabs":[{"url":"https://a/"}],)"
                   R"("favorites":[{"id":"https://a/"}]})"));
}

// Each profile's icons live in its own cache, so another profile's rows are
// that profile's lookups.
TEST(SelectMissingIconURLs, TakesOnlyTheAskedProfilesSpaces) {
  const char* json = R"({"favorites":[{"id":"https://p/"}],)"
                     R"("spaces":[{"profileId":"p",)"
                     R"("favorites":[{"id":"https://p/"}]},)"
                     R"({"profileId":"q","favorites":[{"id":"https://q/"}]}]})";
  EXPECT_EQ((std::vector<std::string>{"https://p/"}), Select(json, "p"));
  EXPECT_EQ((std::vector<std::string>{"https://q/"}), Select(json, "q"));
}

// A space with no profile id is the presenter's, so another profile's fetch
// leaves it alone.
TEST(SelectMissingIconURLs, AnUnownedSpaceIsOnlyThePresentersOwn) {
  const char* json = R"({"tabs":[{"url":"https://top/"}],)"
                     R"("spaces":[{"tabs":[{"url":"https://top/"}]},)"
                     R"({"profileId":"q","tabs":[{"url":"https://q/"}]}]})";
  EXPECT_EQ((std::vector<std::string>{"https://top/"}), Select(json, "p", "p"));
  EXPECT_EQ((std::vector<std::string>{"https://q/"}), Select(json, "q", "p"));
}

// The top-level rows are the active space's, whichever browser built the push.
TEST(SelectMissingIconURLs, TopLevelRowsBelongToTheActiveSpace) {
  const char* json = R"({"activeSpace":1,"tabs":[{"url":"https://top/"}],)"
                     R"("spaces":[{"profileId":"p"},{"profileId":"q"}]})";
  EXPECT_TRUE(Select(json, "p").empty());
  EXPECT_EQ((std::vector<std::string>{"https://top/"}), Select(json, "q"));
}

TEST(SelectMissingIconURLs, IgnoresEmptyAndMalformedRows) {
  EXPECT_TRUE(Select(R"({"tabs":[{"url":""},{},"notadict"],)"
                     R"("favorites":[{"title":"no id"}]})")
                  .empty());
}

TEST(DefaultFaviconURLFor, IsTheOriginsOwnIcoWithNoPathOrQuery) {
  EXPECT_EQ("https://example.test/favicon.ico",
            DefaultFaviconURLFor("https://example.test/deep/page.html?q=1#frag"));
  EXPECT_EQ("http://example.test/favicon.ico",
            DefaultFaviconURLFor("http://example.test/"));
}

// The fixture server the VM scenario uses is a bare host and port, and a
// scheme-only origin check would send the fetch to the wrong place.
TEST(DefaultFaviconURLFor, KeepsThePort) {
  EXPECT_EQ("http://127.0.0.1:8000/favicon.ico",
            DefaultFaviconURLFor("http://127.0.0.1:8000/favicon-page.html"));
}

// A row can hold any scheme the profile persisted. None of these has a site to
// ask, and issuing a request for one would be traffic the user never caused.
TEST(DefaultFaviconURLFor, RefusesEverySchemeButHttpAndHttps) {
  for (const char* url : {"chrome://settings", "file:///tmp/page.html",
                          "data:text/html,<b>x", "javascript:alert(1)",
                          "about:blank", "ftp://example.test/x", "",
                          "not a url at all"}) {
    EXPECT_EQ("", DefaultFaviconURLFor(url)) << url;
  }
}

// The fetch is anonymous. Resolving against the page URL would carry these
// straight to the site.
TEST(DefaultFaviconURLFor, DropsCredentialsFromTheURL) {
  EXPECT_EQ("https://example.test/favicon.ico",
            DefaultFaviconURLFor("https://user:pass@example.test/page"));
}

// 9 of 21 favourite origins in one real profile answered /favicon.ico with a
// 30x: same-host moves to /owa/favicon.ico, a CDN, or a renamed domain.
TEST(RedirectTargetAllowedForIcoFetch, FollowsAnHttpTarget) {
  for (const char* url :
       {"https://outlook.office365.com/owa/favicon.ico",
        "https://teams.public.onecdn.static.microsoft/favicon.ico",
        "http://icon.test:8000/icon.png", "https://93.184.216.34/favicon.ico"}) {
    EXPECT_TRUE(RedirectTargetAllowedForIcoFetch(GURL(url))) << url;
  }
}

// The fetch is anonymous and the user never named the redirect target, so a
// site cannot aim it at anything on this machine or its network.
TEST(RedirectTargetAllowedForIcoFetch, RefusesLocalAddressesAndOtherSchemes) {
  for (const char* url :
       {"http://127.0.0.1/favicon.ico", "http://[::1]/favicon.ico",
        "http://10.0.0.1/x", "http://192.168.1.1/x", "http://172.16.0.1/x",
        "http://169.254.169.254/latest/meta-data", "http://0.0.0.0/x",
        "http://[fe80::1]/x", "http://[fd00::1]/x", "http://localhost:8080/x",
        "http://LOCALHOST/x", "http://foo.localhost/x", "file:///etc/hosts",
        "data:image/png;base64,AAAA", "ftp://example.test/favicon.ico",
        "chrome://settings", ""}) {
    EXPECT_FALSE(RedirectTargetAllowedForIcoFetch(GURL(url))) << url;
  }
}

// A site that sends the anonymous GET to its identity provider answers with
// the provider's page, whose icon is the wrong one for the row.
TEST(PageMayNameIcons, SameRegistrableDomainAndHtmlOnly) {
  const GURL page("https://app.clockify.me/calendar");
  EXPECT_TRUE(PageMayNameIcons(page, GURL("https://app.clockify.me/login"),
                               "text/html"));
  EXPECT_TRUE(PageMayNameIcons(page, GURL("https://clockify.me/"),
                               "application/xhtml+xml"));
  EXPECT_FALSE(PageMayNameIcons(
      page, GURL("https://login.microsoftonline.com/x"), "text/html"));
  EXPECT_FALSE(PageMayNameIcons(page, GURL("https://app.clockify.me/api"),
                                "application/json"));
  EXPECT_FALSE(PageMayNameIcons(page, GURL(), "text/html"));
}

// An IP literal or a single-label intranet host has no registry, so the
// domain check falls back to comparing hosts directly.
TEST(PageMayNameIcons, FallsBackToHostWhenDomainIsEmpty) {
  EXPECT_TRUE(PageMayNameIcons(GURL("http://192.168.1.5/app"),
                               GURL("http://192.168.1.5/login"), "text/html"));
  EXPECT_FALSE(PageMayNameIcons(GURL("http://192.168.1.5/app"),
                                GURL("http://192.168.1.6/login"), "text/html"));
  EXPECT_TRUE(PageMayNameIcons(GURL("http://odin/"), GURL("http://odin/x"),
                               "text/html"));
  EXPECT_FALSE(PageMayNameIcons(GURL("http://odin/"), GURL("http://thor/x"),
                                "text/html"));
}

TEST(IconCandidatesFromLinks, ResolvesAgainstBaseThenPage) {
  const GURL page("https://app.clockify.me/calendar");
  EXPECT_EQ((std::vector<GURL>{GURL("https://app.clockify.me/assets/i.ico")}),
            IconCandidatesFromLinks(
                R"({"icons":[{"href":"assets/i.ico","rel":"icon"}]})", page));
  EXPECT_EQ((std::vector<GURL>{GURL("https://cdn.test/b/i.png")}),
            IconCandidatesFromLinks(
                R"({"base":"https://cdn.test/b/","icons":[{"href":"i.png","rel":"icon"}]})",
                page));
}

TEST(IconCandidatesFromLinks, KeepsHttpAndDataImagesOnlyAndCapsAtThree) {
  const GURL page("https://example.test/");
  const std::vector<GURL> got = IconCandidatesFromLinks(
      // A custom raw-string delimiter: "alert(1)" ends in ")" right before a
      // closing quote, which is the R"(...)" terminator.
      R"json({"icons":[
        {"href":"data:image/png;base64,iVBORw0KGgo=","rel":"icon"},
        {"href":"javascript:alert(1)","rel":"icon"},
        {"href":"http://127.0.0.1/i.png","rel":"icon"},
        {"href":"file:///etc/hosts","rel":"icon"},
        {"href":"data:text/html,<b>x","rel":"icon"},
        {"href":"/a.png","rel":"icon"},
        {"href":"/b.png","rel":"icon"},
        {"href":"/c.png","rel":"icon"}]})json",
      page);
  EXPECT_EQ((std::vector<GURL>{GURL("data:image/png;base64,iVBORw0KGgo="),
                               GURL("https://example.test/a.png"),
                               GURL("https://example.test/b.png")}),
            got);
}

TEST(IconCandidatesFromLinks, MalformedJsonGivesNothing) {
  EXPECT_TRUE(IconCandidatesFromLinks("", GURL("https://a.test/")).empty());
  EXPECT_TRUE(IconCandidatesFromLinks("[1]", GURL("https://a.test/")).empty());
  EXPECT_TRUE(
      IconCandidatesFromLinks(R"({"icons":"x"})", GURL("https://a.test/")).empty());
}

TEST(IconBackfillQueue, StartsNoMoreThanTheBound) {
  IconBackfillQueue queue(2);
  EXPECT_EQ((std::vector<std::string>{"a", "b"}),
            queue.Admit({"a", "b", "c", "d"}));
  EXPECT_EQ(2u, queue.in_flight());
  EXPECT_EQ(2u, queue.waiting());
}

TEST(IconBackfillQueue, AFinishedLookupLetsTheNextOneThrough) {
  IconBackfillQueue queue(1);
  EXPECT_EQ((std::vector<std::string>{"a"}), queue.Admit({"a", "b", "c"}));
  EXPECT_EQ((std::vector<std::string>{"b"}), queue.Finish("a"));
  EXPECT_EQ((std::vector<std::string>{"c"}), queue.Finish("b"));
  EXPECT_TRUE(queue.Finish("c").empty());
  EXPECT_EQ(0u, queue.in_flight());
}

// THE point of the memo. A push happens on every navigation, tab open, tab
// close and title change, so re-admitting would make one iconless site a
// lookup per push for the life of the session.
TEST(IconBackfillQueue, AFailedLookupIsNotRetried) {
  IconBackfillQueue queue(4);
  EXPECT_EQ((std::vector<std::string>{"a"}), queue.Admit({"a"}));
  // Found nothing - Finish is the same call either way.
  queue.Finish("a");
  for (int push = 0; push < 100; ++push) {
    ASSERT_TRUE(queue.Admit({"a"}).empty()) << "re-asked on push " << push;
  }
  EXPECT_EQ(0u, queue.in_flight());
}

TEST(IconBackfillQueue, AnEvictedIconIsAskedForAgain) {
  IconBackfillQueue queue(4);
  queue.Admit({"a"});
  queue.MarkFound("a");
  queue.Finish("a");
  // The caller only offers URLs the icon cache no longer holds.
  EXPECT_EQ((std::vector<std::string>{"a"}), queue.Admit({"a"}));
}

TEST(IconBackfillQueue, AnEvictedIconIsAskedForOncePerEviction) {
  IconBackfillQueue queue(4);
  queue.Admit({"a"});
  queue.MarkFound("a");
  queue.Finish("a");
  queue.Admit({"a"});
  queue.Finish("a");
  EXPECT_TRUE(queue.Admit({"a"}).empty());
}

TEST(IconBackfillQueue, WithoutRefetchAnEvictedIconIsNotAskedForAgain) {
  IconBackfillQueue queue(4);
  queue.Admit({"a"});
  queue.MarkFound("a");
  queue.Finish("a");
  // A short missing list says nothing about the row total, so the caller's
  // flag alone decides.
  EXPECT_TRUE(queue.Admit({"a"}, /*allow_refetch=*/false).empty());
}

TEST(IconBackfillQueue, WithoutRefetchANewURLIsStillAdmitted) {
  IconBackfillQueue queue(4);
  queue.Admit({"a"});
  queue.MarkFound("a");
  queue.Finish("a");
  EXPECT_EQ((std::vector<std::string>{"b"}),
            queue.Admit({"a", "b"}, /*allow_refetch=*/false));
}

TEST(IconBackfillQueue, AllowingRefetchAgainTakesTheEvictedIconOnce) {
  IconBackfillQueue queue(4);
  queue.Admit({"a"});
  queue.MarkFound("a");
  queue.Finish("a");
  EXPECT_TRUE(queue.Admit({"a"}, false).empty());
  EXPECT_EQ((std::vector<std::string>{"a"}), queue.Admit({"a"}, true));
  queue.Finish("a");
  EXPECT_TRUE(queue.Admit({"a"}, true).empty());
}

TEST(IconBackfillQueue, AQueuedOrRunningURLIsNotQueuedTwice) {
  IconBackfillQueue queue(1);
  ASSERT_EQ((std::vector<std::string>{"a"}), queue.Admit({"a", "b"}));
  queue.MarkFound("a");
  queue.MarkFound("b");
  EXPECT_TRUE(queue.Admit({"a", "b"}).empty());
  EXPECT_EQ(1u, queue.in_flight());
  EXPECT_EQ(1u, queue.waiting());
}

TEST(IconBackfillQueue, AnIconlessURLIsNotReAdmittedOnASecondPush) {
  IconBackfillQueue queue(4);
  queue.Admit({"a"});
  queue.Finish("a");
  EXPECT_TRUE(queue.Admit({"a"}).empty());
  EXPECT_TRUE(queue.Admit({"a"}).empty());
}

TEST(IconBackfillQueue, DoesNotReAdmitWhatIsStillInFlight) {
  IconBackfillQueue queue(4);
  EXPECT_EQ((std::vector<std::string>{"a"}), queue.Admit({"a"}));
  EXPECT_TRUE(queue.Admit({"a", "a"}).empty());
  EXPECT_EQ(1u, queue.in_flight());
}

TEST(IconBackfillQueue, DropsEmptyURLs) {
  IconBackfillQueue queue(4);
  EXPECT_TRUE(queue.Admit({""}).empty());
  EXPECT_EQ(0u, queue.in_flight());
}

// The first ask runs before the visit commits, when the favicon database has
// nothing, so a visit has to buy one more.
TEST(IconBackfillQueue, AVisitBuysOneMoreLookup) {
  IconBackfillQueue queue(4);
  queue.Admit({"a"});
  queue.Finish("a");
  queue.AllowRetry("a");
  EXPECT_EQ((std::vector<std::string>{"a"}), queue.Admit({"a"}));
}

// Otherwise a navigation loop that pushes while a lookup is outstanding issues
// the same one again on every turn.
TEST(IconBackfillQueue, ARetryWhileInFlightIsIgnored) {
  IconBackfillQueue queue(4);
  queue.Admit({"a"});
  queue.AllowRetry("a");
  EXPECT_TRUE(queue.Admit({"a"}).empty());
  EXPECT_EQ(1u, queue.in_flight());
}

// A tab strip going away cancels the lookups it started, so their Finish never
// arrives. Without this the bound is spent forever and no row ever gets an icon
// again for the life of the process.
TEST(IconBackfillQueue, ATeardownFreesTheSlotsItsCancelledLookupsHeld) {
  IconBackfillQueue queue(2);
  EXPECT_EQ((std::vector<std::string>{"a", "b"}), queue.Admit({"a", "b", "c"}));
  queue.Release({"a", "b"});
  EXPECT_EQ(0u, queue.in_flight());
  // The waiting URL goes on the next push, and the released ones stay asked.
  EXPECT_EQ((std::vector<std::string>{"c"}), queue.Admit({"a", "b"}));
}

// One window's teardown must not free another's slots: the queue is shared by
// every window of the profile, so clearing it wholesale would let the windows
// still running exceed the bound.
TEST(IconBackfillQueue, ReleaseTouchesOnlyTheURLsItIsGiven) {
  IconBackfillQueue queue(4);
  queue.Admit({"mine", "theirs"});
  queue.Release({"mine"});
  EXPECT_EQ(1u, queue.in_flight());
}

TEST(IconBackfillQueue, TheMemosStopGrowingAtTheCap) {
  IconBackfillQueue queue(kMaxRemembered + 10);
  for (size_t i = 0; i < kMaxRemembered + 5; ++i) {
    const std::string url = "https://site/" + std::to_string(i);
    queue.Admit({url});
    queue.MarkFound(url);
    queue.Finish(url);
  }
  EXPECT_EQ(kMaxRemembered, queue.admitted_size());
  EXPECT_EQ(kMaxRemembered, queue.found_size());
  const std::string newest = "https://site/" + std::to_string(kMaxRemembered + 4);
  EXPECT_TRUE(queue.Admit({newest}, false).empty())
      << "a recent URL was forgotten";
  EXPECT_EQ(1u, queue.Admit({"https://site/0"}, false).size())
      << "the oldest URL was kept past the cap";
}

TEST(IconBackfillQueueFor, ForgettingAProfileStartsItsQueueOver) {
  constexpr uintptr_t kGone = 0x728C;
  IconBackfillQueueFor(kGone).Admit({"https://gone/"});
  ForgetIconBackfillQueue(kGone);
  EXPECT_EQ((std::vector<std::string>{"https://gone/"}),
            IconBackfillQueueFor(kGone).Admit({"https://gone/"}));
}

TEST(IconBackfillQueueFor, FindNeverCreatesAQueue) {
  constexpr uintptr_t kDead = 0x728D;
  IconBackfillQueueFor(kDead);
  ForgetIconBackfillQueue(kDead);
  EXPECT_EQ(nullptr, FindIconBackfillQueue(kDead));
  EXPECT_EQ(nullptr, FindIconBackfillQueue(kDead))
      << "the lookup made an entry for a dead profile";
  EXPECT_NE(nullptr, &IconBackfillQueueFor(kDead));
  EXPECT_NE(nullptr, FindIconBackfillQueue(kDead));
  ForgetIconBackfillQueue(kDead);
}

// The registry outlives any one test, so these stand in for two profiles that
// nothing else in this binary keys on.
constexpr uintptr_t kProfileA = 0x728A;
constexpr uintptr_t kProfileB = 0x728B;

// THE point of sharing. Five windows on one profile each admitted the same cold
// row and fetched it five times, storing one answer.
TEST(IconBackfillQueueFor, WindowsOfOneProfileShareTheMemoAndTheBound) {
  IconBackfillQueue& from_first_window = IconBackfillQueueFor(kProfileA);
  IconBackfillQueue& from_second_window = IconBackfillQueueFor(kProfileA);
  EXPECT_EQ(&from_first_window, &from_second_window);

  EXPECT_EQ((std::vector<std::string>{"https://shared/"}),
            from_first_window.Admit({"https://shared/"}));
  EXPECT_TRUE(from_second_window.Admit({"https://shared/"}).empty())
      << "a second window on the same profile re-fetched it";
}

// An off-the-record profile is a different BrowserContext, so it partitions
// here without this file knowing what a profile is.
TEST(IconBackfillQueueFor, ADifferentProfileGetsItsOwnQueue) {
  IconBackfillQueueFor(kProfileA).Admit({"https://split/"});
  EXPECT_EQ((std::vector<std::string>{"https://split/"}),
            IconBackfillQueueFor(kProfileB).Admit({"https://split/"}));
}

// A URL waiting behind the bound has an unanswered lookup coming, so a visit
// while it waits buys nothing - and a second copy would run the same lookup
// twice once the slot freed up.
TEST(IconBackfillQueue, ARetryForAWaitingURLIsIgnored) {
  IconBackfillQueue queue(1);
  EXPECT_EQ((std::vector<std::string>{"a"}), queue.Admit({"a", "b"}));
  queue.AllowRetry("b");
  EXPECT_TRUE(queue.Admit({"b"}).empty());
  EXPECT_EQ(1u, queue.waiting());
  EXPECT_EQ((std::vector<std::string>{"b"}), queue.Finish("a"));
  EXPECT_TRUE(queue.Finish("b").empty());
  EXPECT_EQ(0u, queue.in_flight());
}

TEST(IcoFailureReachesLinkTagTier, NetworkErrorsEndTheRowOnlyBeforeTier4) {
  for (int net_error : {net::ERR_CONNECTION_REFUSED, net::ERR_NAME_NOT_RESOLVED,
                        net::ERR_TIMED_OUT}) {
    EXPECT_FALSE(IcoFailureReachesLinkTagTier(net_error,
                                              /*in_link_tag_tier=*/false))
        << net_error;
    EXPECT_TRUE(IcoFailureReachesLinkTagTier(net_error,
                                             /*in_link_tag_tier=*/true))
        << net_error;
  }
}

TEST(IcoFailureReachesLinkTagTier, AnHttpErrorAlwaysReachesTheLinkTagTier) {
  EXPECT_TRUE(IcoFailureReachesLinkTagTier(net::ERR_HTTP_RESPONSE_CODE_FAILURE,
                                           /*in_link_tag_tier=*/false));
  EXPECT_TRUE(IcoFailureReachesLinkTagTier(net::ERR_HTTP_RESPONSE_CODE_FAILURE,
                                           /*in_link_tag_tier=*/true));
}

TEST(IcoFailureReachesLinkTagTier, AnEmptyOkBodyAlwaysReachesTheLinkTagTier) {
  EXPECT_TRUE(
      IcoFailureReachesLinkTagTier(net::OK, /*in_link_tag_tier=*/false));
  EXPECT_TRUE(
      IcoFailureReachesLinkTagTier(net::OK, /*in_link_tag_tier=*/true));
}

}  // namespace
}  // namespace atrium
