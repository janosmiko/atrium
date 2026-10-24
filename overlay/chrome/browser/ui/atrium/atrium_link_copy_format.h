// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_link_copy_format.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_LINK_COPY_FORMAT_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_LINK_COPY_FORMAT_H_

namespace atrium {

// the three "Copy URL" chord variants (Cmd+Shift+C / Opt+Shift+
// Cmd+C / Ctrl+Shift+Cmd+C) - see CopyCurrentPageLink().
enum class LinkCopyFormat {
  kPlain,     // The bare URL spec, the original behavior.
  kMarkdown,  // "[title](url)".
  kQuote,     // "> [title](url)" - a blockquoted Markdown link.
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_LINK_COPY_FORMAT_H_
