// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_find_session.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_FIND_SESSION_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_FIND_SESSION_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/atrium/atrium_find_action.h"
#include "components/find_in_page/find_result_observer.h"
#include "components/find_in_page/find_tab_helper.h"

namespace content {
class WebContents;
}  // namespace content

namespace atrium {

// The live half of atrium_find_action.h's decision: turns a FindTrigger into
// one StartFinding or StopFinding call on the visible tab's FindTabHelper, and
// forwards each result back through the callback given at construction.
class FindSession : public find_in_page::FindResultObserver {
 public:
  using ResultCallback =
      base::RepeatingCallback<void(int active, int total, bool final_update)>;

  explicit FindSession(ResultCallback on_result);
  FindSession(const FindSession&) = delete;
  FindSession& operator=(const FindSession&) = delete;
  ~FindSession() override;

  // `contents` may be null. Starts observing it and remembers it, so Stop()
  // reaches the tab that holds the highlights even after the window has
  // moved on to another one.
  void Apply(content::WebContents* contents,
             FindTrigger trigger,
             const std::u16string& query);

  // Ends the session on the tab that owns it. Idempotent, and safe with a
  // destroyed tab.
  void Stop();

  // What the bar opens on: the tab's own last search, else the profile's.
  std::u16string InitialText(content::WebContents* contents) const;

  // find_in_page::FindResultObserver:
  void OnFindResultAvailable(content::WebContents* web_contents) override;
  void OnFindTabHelperDestroyed(find_in_page::FindTabHelper* helper) override;

 private:
  ResultCallback on_result_;
  base::WeakPtr<content::WebContents> contents_;
  base::ScopedObservation<find_in_page::FindTabHelper,
                          find_in_page::FindResultObserver>
      observation_{this};
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_FIND_SESSION_H_
