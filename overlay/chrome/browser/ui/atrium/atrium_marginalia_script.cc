// Copyright 2026 The Atrium Authors

#include "chrome/browser/ui/atrium/atrium_marginalia_script.h"

#include "base/json/string_escape.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"

namespace atrium {

namespace {

constexpr char kMarginaliaLibrary[] = R"js(
window.__atriumMarginalia = window.__atriumMarginalia || (function() {
  var STYLE_ID = 'atrium-marginalia-style';
  function ensureStyle() {
    if (document.getElementById(STYLE_ID)) return;
    var style = document.createElement('style');
    style.id = STYLE_ID;
    style.textContent =
        'mark.atrium-marginalia-mark{background:#ffe28a;color:inherit;}' +
        'mark.atrium-marginalia-mark.atrium-marginalia-flash{' +
        'outline:2px solid #f5a623;}';
    (document.head || document.documentElement).appendChild(style);
  }
  function wrapRange(range, id) {
    var root = range.commonAncestorContainer;
    if (root.nodeType === 3) root = root.parentNode;
    if (!root) return false;
    var walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT, {
      acceptNode: function(node) {
        var p = node.parentNode;
        if (p && (p.nodeName === 'SCRIPT' || p.nodeName === 'STYLE')) {
          return NodeFilter.FILTER_REJECT;
        }
        return range.intersectsNode(node) ?
            NodeFilter.FILTER_ACCEPT : NodeFilter.FILTER_REJECT;
      }
    });
    var nodes = [];
    var n;
    while ((n = walker.nextNode())) nodes.push(n);
    if (!nodes.length) return false;
    var wrapped = false;
    for (var i = nodes.length - 1; i >= 0; i--) {
      var node = nodes[i];
      var start = (node === range.startContainer) ? range.startOffset : 0;
      var end = (node === range.endContainer) ? range.endOffset : node.length;
      if (start >= end) continue;
      var middle = node.splitText(start);
      middle.splitText(end - start);
      var mark = document.createElement('mark');
      mark.className = 'atrium-marginalia-mark';
      mark.setAttribute('data-atrium-mark-id', id);
      middle.parentNode.replaceChild(mark, middle);
      mark.appendChild(middle);
      wrapped = true;
    }
    return wrapped;
  }
  function wrapSelection(id) {
    ensureStyle();
    var sel = window.getSelection();
    if (!sel || sel.rangeCount === 0 || sel.isCollapsed) return false;
    return wrapRange(sel.getRangeAt(0), id);
  }
  function findQuoteRange(quote) {
    if (!quote) return null;
    var walker = document.createTreeWalker(
        document.body, NodeFilter.SHOW_TEXT, {
      acceptNode: function(node) {
        var p = node.parentNode;
        if (!p) return NodeFilter.FILTER_REJECT;
        if (p.nodeName === 'SCRIPT' || p.nodeName === 'STYLE' ||
            p.nodeName === 'NOSCRIPT') {
          return NodeFilter.FILTER_REJECT;
        }
        return NodeFilter.FILTER_ACCEPT;
      }
    });
    var nodes = [];
    var texts = [];
    var n;
    while ((n = walker.nextNode())) { nodes.push(n); texts.push(n.nodeValue); }
    var full = texts.join('');
    var idx = full.indexOf(quote);
    if (idx < 0) return null;
    var endIdx = idx + quote.length;
    var pos = 0, startNode = null, startOffset = 0, endNode = null,
        endOffset = 0;
    for (var i = 0; i < nodes.length; i++) {
      var len = texts[i].length;
      if (startNode === null && idx < pos + len) {
        startNode = nodes[i];
        startOffset = idx - pos;
      }
      if (endNode === null && endIdx <= pos + len) {
        endNode = nodes[i];
        endOffset = endIdx - pos;
        break;
      }
      pos += len;
    }
    if (!startNode || !endNode) return null;
    var range = document.createRange();
    range.setStart(startNode, startOffset);
    range.setEnd(endNode, endOffset);
    return range;
  }
  function reanchor(list) {
    ensureStyle();
    var applied = window.__atriumMarginaliaApplied ||
        (window.__atriumMarginaliaApplied = {});
    var out = [];
    for (var i = 0; i < list.length; i++) {
      var id = list[i].id, quote = list[i].quote;
      if (applied[id]) { out.push(true); continue; }
      if (document.querySelector(
              'mark[data-atrium-mark-id="' + CSS.escape(id) + '"]')) {
        applied[id] = true;
        out.push(true);
        continue;
      }
      var range = findQuoteRange(quote);
      if (!range) { out.push(false); continue; }
      var ok = wrapRange(range, id);
      if (ok) applied[id] = true;
      out.push(ok);
    }
    return out;
  }
  function installClickDelegate() {
    if (window.__atriumMarginaliaClickInstalled) return;
    window.__atriumMarginaliaClickInstalled = true;
    window.__atriumMarginaliaPendingClicks = [];
    document.addEventListener('click', function(e) {
      var mark = e.target && e.target.closest &&
          e.target.closest('mark[data-atrium-mark-id]');
      if (!mark) return;
      window.__atriumMarginaliaPendingClicks.push(
          mark.getAttribute('data-atrium-mark-id'));
    }, true);
  }
  function drainClicks() {
    var q = window.__atriumMarginaliaPendingClicks || [];
    window.__atriumMarginaliaPendingClicks = [];
    return q;
  }
  function scrollToMark(id) {
    var mark = document.querySelector(
        'mark[data-atrium-mark-id="' + CSS.escape(id) + '"]');
    if (!mark) return false;
    mark.scrollIntoView({block: 'center'});
    mark.classList.add('atrium-marginalia-flash');
    setTimeout(function() {
      mark.classList.remove('atrium-marginalia-flash');
    }, 1200);
    return true;
  }
  installClickDelegate();
  return {
    wrapSelection: wrapSelection,
    reanchor: reanchor,
    drainClicks: drainClicks,
    scrollToMark: scrollToMark,
  };
})();
)js";

std::u16string WithLibrary(const std::string& call) {
  return base::UTF8ToUTF16(base::StrCat({kMarginaliaLibrary, call}));
}

}  // namespace

std::u16string BuildWrapSelectionScript(const std::string& mark_id) {
  return WithLibrary(base::StrCat(
      {"window.__atriumMarginalia.wrapSelection(",
       base::GetQuotedJSONString(mark_id), ");"}));
}

std::u16string BuildReanchorScript(const std::string& marginalia_json) {
  return WithLibrary(base::StrCat(
      {"window.__atriumMarginalia.reanchor(JSON.parse(",
       base::GetQuotedJSONString(marginalia_json), "));"}));
}

std::u16string BuildDrainClicksScript() {
  return WithLibrary("window.__atriumMarginalia.drainClicks();");
}

std::u16string BuildScrollToMarkScript(const std::string& mark_id) {
  return WithLibrary(base::StrCat(
      {"window.__atriumMarginalia.scrollToMark(",
       base::GetQuotedJSONString(mark_id), ");"}));
}

}  // namespace atrium
