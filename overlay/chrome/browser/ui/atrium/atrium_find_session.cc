// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_find_session.cc
#include "chrome/browser/ui/atrium/atrium_find_session.h"

#include "components/find_in_page/find_notification_details.h"
#include "components/find_in_page/find_types.h"
#include "content/public/browser/web_contents.h"

namespace atrium {

FindSession::FindSession(ResultCallback on_result)
    : on_result_(std::move(on_result)) {}

FindSession::~FindSession() = default;

void FindSession::Apply(content::WebContents* contents,
                        FindTrigger trigger,
                        const std::u16string& query) {
  if (!contents) {
    return;
  }
  find_in_page::FindTabHelper* helper =
      find_in_page::FindTabHelper::FromWebContents(contents);
  if (!helper) {
    return;
  }

  if (contents_.get() != contents) {
    observation_.Reset();
    contents_ = contents->GetWeakPtr();
    observation_.Observe(helper);
  }

  const FindRequest request = FindRequestFor(trigger, query.empty());
  if (request.start) {
    helper->set_find_ui_active(true);
    helper->StartFinding(query, request.forward, /*case_sensitive=*/false,
                         request.find_match);
  } else if (request.stop) {
    helper->set_find_ui_active(false);
    helper->set_find_ui_focused(false);
    helper->StopFinding(request.keep_selection
                            ? find_in_page::SelectionAction::kKeep
                            : find_in_page::SelectionAction::kClear);
  }
}

void FindSession::Stop() {
  content::WebContents* contents = contents_.get();
  observation_.Reset();
  contents_.reset();
  if (!contents) {
    return;
  }
  find_in_page::FindTabHelper* helper =
      find_in_page::FindTabHelper::FromWebContents(contents);
  if (!helper) {
    return;
  }
  helper->set_find_ui_active(false);
  helper->set_find_ui_focused(false);
  helper->StopFinding(find_in_page::SelectionAction::kKeep);
}

std::u16string FindSession::InitialText(
    content::WebContents* contents) const {
  if (!contents) {
    return std::u16string();
  }
  find_in_page::FindTabHelper* helper =
      find_in_page::FindTabHelper::FromWebContents(contents);
  return helper ? helper->GetInitialSearchText() : std::u16string();
}

void FindSession::OnFindResultAvailable(content::WebContents* web_contents) {
  find_in_page::FindTabHelper* helper =
      find_in_page::FindTabHelper::FromWebContents(web_contents);
  if (!helper) {
    return;
  }
  const find_in_page::FindNotificationDetails& result = helper->find_result();
  if (result.number_of_matches() == -1) {
    return;
  }
  on_result_.Run(result.active_match_ordinal(), result.number_of_matches(),
                result.final_update());
}

void FindSession::OnFindTabHelperDestroyed(find_in_page::FindTabHelper*) {
  observation_.Reset();
  contents_.reset();
}

}  // namespace atrium
