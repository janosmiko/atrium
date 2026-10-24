// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_log_safe_url.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_LOG_SAFE_URL_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_LOG_SAFE_URL_H_

#include <string>

class GURL;

namespace atrium {

// Returns "scheme://" plus the eTLD+1 (or bare host for an IP or intranet
// name), never the path or query a log line should not carry.
std::string LogSafeUrl(const GURL& url);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_LOG_SAFE_URL_H_
