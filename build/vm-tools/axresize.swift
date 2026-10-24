// AX-based window resize/move for VM probes. Mirrors what tiling window
// managers (AeroSpace) do: change a window's frame WITHOUT any mouse travel,
// so hover-dependent UI (status bubble) can be probed across a resize.
//
// Usage:
//   axresize <app-process-name> size <w> <h>
//   axresize <app-process-name> pos  <x> <y>
//   axresize <app-process-name> get
//   axresize <app-process-name> count [minArea]   real-window count
//   axresize <app-process-name> list  [minArea]   one line per window: subrole area pos size title
//   axresize <app-process-name> focused           focused window: subrole area pos size title
//   axresize <app-process-name> kinds [minArea]   one line per window, with the
//                                                 URLs of every web area inside it
//
// `kinds` exists because `count` and `list` cannot tell one KIND of window from
// another. A little window and a space window are both
// AXStandardWindow, both around 1024x680, both titled "Untitled" before their
// page commits - so "two windows appeared" is true on the handover path and on
// the stock path alike, and proves only that a window opened. What differs is
// what CHROME the window is built around: a space window holds the sidebar
// WebUI, a little window holds the foyer strip instead and has no sidebar at
// all. Both are web areas in the window's own AX subtree, and each carries its
// chrome://atrium.top-chrome/<route> URL, so the window states its kind rather
// than the app stating its intention.
//
// The count/list/focused verbs are read-only AX queries added for the
// scenario-runner: the presenter/window-jump regression asserts
// "exactly ONE real Atrium window survives a cross-profile space switch", and
// a second ordered-on-screen NSWindow is the failure signature. Child widgets
// (status bubble, command palette) surface as AX windows too, so `count`
// filters by area >= minArea (default 200000 pt^2, ~450x450) to count only
// real browser windows. Emit machine-parseable lines the asserts.sh helpers grep.
//
// Compile on the host: swiftc -O axresize.swift -o axresize
import AppKit
import ApplicationServices

func fail(_ msg: String) -> Never {
    FileHandle.standardError.write((msg + "\n").data(using: .utf8)!)
    exit(1)
}

let args = CommandLine.arguments
guard args.count >= 3 else { fail("usage: axresize <app> size|pos|get [a b]") }
let appName = args[1]
let verb = args[2]

// Runs before the app lookup: the decision it checks is pure, so requiring a
// live Atrium would make the guard unrunnable exactly when it is most useful.
if verb == "phantom-selftest" {
    var failures = 0
    // The frame column is the one that decides it now, so every case carries it.
    // The pair that matters is the first two: same attributes, different answer,
    // purely because one sits on another window's frame and the other does not.
    let cases: [(String, String, Bool, Bool, String)] = [
        ("AXUnknown", "", true, true, "the measured twin, on the real window's frame"),
        ("AXUnknown", "", false, false, "a peek or overlay on its own frame is real"),
        ("AXUnknown", "Untitled", true, false, "titled, so not the twin whatever its frame"),
        ("AXStandardWindow", "", true, false, "a standard window with no title yet"),
        ("AXStandardWindow", "Untitled", true, false, "the real window"),
        ("AXDialog", "", true, false, "a dialog is not the twin"),
        ("", "", true, false, "no subrole reported is not a reason to drop a window"),
    ]
    for (subrole, title, shares, want, why) in cases {
        let got = isPhantomWindow(subrole: subrole, title: title, sharesFrame: shares)
        if got != want {
            print("FAIL subrole=\(subrole) title=\"\(title)\" shares=\(shares) want=\(want) got=\(got) - \(why)")
            failures += 1
        }
    }
    print(failures == 0 ? "phantom-selftest: ok (\(cases.count) cases)"
                        : "phantom-selftest: \(failures) failed")
    exit(failures == 0 ? 0 : 1)
}

guard let app = NSWorkspace.shared.runningApplications.first(where: {
    ($0.localizedName ?? "") == appName || ($0.bundleIdentifier ?? "").contains(appName)
}) else { fail("no running app matching '\(appName)'") }

let axApp = AXUIElementCreateApplication(app.processIdentifier)
var windowsRef: CFTypeRef?
guard AXUIElementCopyAttributeValue(axApp, kAXWindowsAttribute as CFString, &windowsRef) == .success,
      let windows = windowsRef as? [AXUIElement], !windows.isEmpty else {
    fail("no AX windows for pid \(app.processIdentifier) (accessibility not granted?)")
}

// Child widgets (status bubble, palette) surface as AX windows too - pick
// the largest one, which is always the real browser window.
func windowArea(_ w: AXUIElement) -> Double {
    var sizeRef: CFTypeRef?
    guard AXUIElementCopyAttributeValue(w, kAXSizeAttribute as CFString, &sizeRef) == .success else { return 0 }
    var s = CGSize.zero
    AXValueGetValue(sizeRef as! AXValue, .cgSize, &s)
    return Double(s.width * s.height)
}
let win = windows.max(by: { windowArea($0) < windowArea($1) })!

// One machine-parseable line describing a window: subrole, area, pos, size,
// title. Used by the count/list/focused read verbs of the scenario runner.
func subroleOf(_ w: AXUIElement) -> String {
    var ref: CFTypeRef?
    AXUIElementCopyAttributeValue(w, kAXSubroleAttribute as CFString, &ref)
    return (ref as? String) ?? ""
}

func titleOf(_ w: AXUIElement) -> String {
    var ref: CFTypeRef?
    AXUIElementCopyAttributeValue(w, kAXTitleAttribute as CFString, &ref)
    return (ref as? String) ?? ""
}

// an AXUnknown window with no title sits at the main window's exact
// geometry in some runs, and counting it failed three unrelated features'
// acceptance. Measured: the twin is role=AXWindow subrole=AXUnknown title=""
// main=false with one child and no traffic-light buttons, while a genuine second
// window - including a little window, measured in little-window-hotkey - is
// AXStandardWindow with a title. Both halves are required, so a titled window is
// never skipped whatever its subrole.
func isPhantomWindow(subrole: String, title: String, sharesFrame: Bool) -> Bool {
    return subrole == "AXUnknown" && title.isEmpty && sharesFrame
}

func frameOf(_ w: AXUIElement) -> CGRect {
    var posRef: CFTypeRef?, sizeRef: CFTypeRef?
    AXUIElementCopyAttributeValue(w, kAXPositionAttribute as CFString, &posRef)
    AXUIElementCopyAttributeValue(w, kAXSizeAttribute as CFString, &sizeRef)
    var p = CGPoint.zero, sz = CGSize.zero
    if let posRef { AXValueGetValue(posRef as! AXValue, .cgPoint, &p) }
    if let sizeRef { AXValueGetValue(sizeRef as! AXValue, .cgSize, &sz) }
    return CGRect(origin: p, size: sz)
}

func describeWindow(_ w: AXUIElement) -> String {
    var subroleRef: CFTypeRef?, posRef: CFTypeRef?, sizeRef: CFTypeRef?, titleRef: CFTypeRef?
    AXUIElementCopyAttributeValue(w, kAXSubroleAttribute as CFString, &subroleRef)
    AXUIElementCopyAttributeValue(w, kAXPositionAttribute as CFString, &posRef)
    AXUIElementCopyAttributeValue(w, kAXSizeAttribute as CFString, &sizeRef)
    AXUIElementCopyAttributeValue(w, kAXTitleAttribute as CFString, &titleRef)
    var p = CGPoint.zero, s = CGSize.zero
    if let posRef { AXValueGetValue(posRef as! AXValue, .cgPoint, &p) }
    if let sizeRef { AXValueGetValue(sizeRef as! AXValue, .cgSize, &s) }
    let subrole = (subroleRef as? String) ?? "-"
    let title = (titleRef as? String) ?? "-"
    let area = Int(s.width * s.height)
    return "subrole=\(subrole) area=\(area) pos=\(Int(p.x)),\(Int(p.y)) size=\(Int(s.width))x\(Int(s.height)) title=\"\(title)\""
}

// THE APP MUST BE LAUNCHED WITH --force-renderer-accessibility for `kinds` and
// `tree` to see anything web. Chromium builds a renderer accessibility tree
// only for an assistive technology; without that flag a window's AX subtree
// stops at the native views and every web area - sidebar, foyer, page - is
// simply absent, so `kinds` reports urls=[] and reads exactly like "no chrome
// found" while nothing was ever built to find. Measured 2026-08-11: three
// windows, all urls=[], and chrome://atrium.top-chrome/sidebar on the very
// next launch with the flag. Setting the AXManualAccessibility attribute from
// here was tried first and did NOT turn the trees on, so it is not used.

// Every URL reachable inside a window's AX subtree, in visit order. A node
// carrying kAXURL is the root of a rendered document (Chromium exposes it on
// the web area), so it is recorded and NOT descended into - the page's own DOM
// is thousands of nodes and says nothing about which chrome the window has.
// Bounded on both depth and node count: an unbounded walk of a live browser
// window is a probe that can hang the scenario it was meant to measure.
func webAreaURLs(_ window: AXUIElement, maxDepth: Int = 12, maxNodes: Int = 4000) -> [String] {
    var urls: [String] = []
    var visited = 0
    var frontier: [(AXUIElement, Int)] = [(window, 0)]
    while !frontier.isEmpty, visited < maxNodes {
        let (node, depth) = frontier.removeFirst()
        visited += 1
        var urlRef: CFTypeRef?
        if AXUIElementCopyAttributeValue(node, kAXURLAttribute as CFString, &urlRef) == .success,
           let url = urlRef as? NSURL, let str = url.absoluteString, !str.isEmpty {
            urls.append(str)
            continue  // a document root: its contents are not chrome
        }
        guard depth < maxDepth else { continue }
        var childrenRef: CFTypeRef?
        if AXUIElementCopyAttributeValue(node, kAXChildrenAttribute as CFString, &childrenRef) == .success,
           let children = childrenRef as? [AXUIElement] {
            for child in children { frontier.append((child, depth + 1)) }
        }
    }
    return urls
}

switch verb {
case "get":
    var posRef: CFTypeRef?, sizeRef: CFTypeRef?
    AXUIElementCopyAttributeValue(win, kAXPositionAttribute as CFString, &posRef)
    AXUIElementCopyAttributeValue(win, kAXSizeAttribute as CFString, &sizeRef)
    var p = CGPoint.zero, s = CGSize.zero
    if let posRef { AXValueGetValue(posRef as! AXValue, .cgPoint, &p) }
    if let sizeRef { AXValueGetValue(sizeRef as! AXValue, .cgSize, &s) }
    print("pos \(Int(p.x)) \(Int(p.y)) size \(Int(s.width)) \(Int(s.height))")
case "size":
    guard args.count == 5, let w = Double(args[3]), let h = Double(args[4]) else { fail("size needs <w> <h>") }
    var s = CGSize(width: w, height: h)
    guard let v = AXValueCreate(.cgSize, &s) else { fail("AXValueCreate failed") }
    let r = AXUIElementSetAttributeValue(win, kAXSizeAttribute as CFString, v)
    if r != .success { fail("set size failed: \(r.rawValue)") }
    print("resized to \(Int(w))x\(Int(h))")
case "pos":
    guard args.count == 5, let x = Double(args[3]), let y = Double(args[4]) else { fail("pos needs <x> <y>") }
    var p = CGPoint(x: x, y: y)
    guard let v = AXValueCreate(.cgPoint, &p) else { fail("AXValueCreate failed") }
    let r = AXUIElementSetAttributeValue(win, kAXPositionAttribute as CFString, v)
    if r != .success { fail("set pos failed: \(r.rawValue)") }
    print("moved to \(Int(x)),\(Int(y))")
case "count":
    let minArea = args.count >= 4 ? (Double(args[3]) ?? 200_000) : 200_000
    let big = windows.filter { windowArea($0) >= minArea }
    // The twin is measured to sit at the main window's EXACT geometry - that is
    // what `identify` exists for, because `list` cannot tell them apart on
    // position and size. Keying on subrole and title alone also dropped real
    // second windows: a peek card and an overlay are chromeless by design, so
    // untitled and AXUnknown is what they look like WORKING.
    let frames = big.map { frameOf($0) }
    let real = big.enumerated().filter { index, w in
        let shares = frames.enumerated().contains { other, f in
            other != index && f == frames[index]
        }
        return !isPhantomWindow(subrole: subroleOf(w), title: titleOf(w),
                                sharesFrame: shares)
    }.map { $0.element }
    // ignored is printed, not just subtracted: a phantom that turns out to be a
    // real window has to be visible in the output rather than silently dropped.
    print("count=\(real.count) minArea=\(Int(minArea)) total=\(windows.count) " +
          "ignored=\(big.count - real.count)")
case "identify":
    // `list` prints subrole, geometry and title, and for the AXUnknown
    // twin those are identical to the real window's - so it cannot say what the
    // twin IS. This prints the attributes that differ between a real window and
    // whatever is standing beside it.
    for w in windows {
        var roleRef: CFTypeRef?, subroleRef: CFTypeRef?, idRef: CFTypeRef?
        var mainRef: CFTypeRef?, minimizedRef: CFTypeRef?, modalRef: CFTypeRef?
        var childrenRef: CFTypeRef?, parentRef: CFTypeRef?, descRef: CFTypeRef?
        AXUIElementCopyAttributeValue(w, kAXRoleAttribute as CFString, &roleRef)
        AXUIElementCopyAttributeValue(w, kAXSubroleAttribute as CFString, &subroleRef)
        AXUIElementCopyAttributeValue(w, kAXIdentifierAttribute as CFString, &idRef)
        AXUIElementCopyAttributeValue(w, kAXMainAttribute as CFString, &mainRef)
        AXUIElementCopyAttributeValue(w, kAXMinimizedAttribute as CFString, &minimizedRef)
        AXUIElementCopyAttributeValue(w, kAXModalAttribute as CFString, &modalRef)
        AXUIElementCopyAttributeValue(w, kAXChildrenAttribute as CFString, &childrenRef)
        AXUIElementCopyAttributeValue(w, kAXParentAttribute as CFString, &parentRef)
        AXUIElementCopyAttributeValue(w, kAXDescriptionAttribute as CFString, &descRef)
        var names: CFArray?
        AXUIElementCopyAttributeNames(w, &names)
        let attrs = ((names as? [String]) ?? []).sorted().joined(separator: ",")
        let kids = (childrenRef as? [AXUIElement])?.count ?? -1
        var pid: pid_t = 0
        AXUIElementGetPid(w, &pid)
        print("role=\((roleRef as? String) ?? "-") subrole=\((subroleRef as? String) ?? "-") " +
              "id=\((idRef as? String) ?? "-") desc=\"\((descRef as? String) ?? "")\" " +
              "main=\(String(describing: mainRef as? Bool)) " +
              "minimized=\(String(describing: minimizedRef as? Bool)) " +
              "modal=\(String(describing: modalRef as? Bool)) " +
              "children=\(kids) hasParent=\(parentRef != nil) pid=\(pid)")
        print("  attrs=[\(attrs)]")
    }
case "list":
    let minArea = args.count >= 4 ? (Double(args[3]) ?? 0) : 0
    for w in windows where windowArea(w) >= minArea {
        print(describeWindow(w))
    }
case "tree":
    // Debug companion to `kinds`: what the walk actually sees. Not used by any
    // assertion - it is how "the tree is empty" gets told apart from "the walk
    // is wrong", which is the difference `kinds` alone cannot report.
    let maxDepth = args.count >= 4 ? (Int(args[3]) ?? 6) : 6
    var frontier: [(AXUIElement, Int)] = windows.map { ($0, 0) }
    var seen = 0
    while !frontier.isEmpty, seen < 400 {
        let (node, depth) = frontier.removeFirst()
        seen += 1
        var roleRef: CFTypeRef?, urlRef: CFTypeRef?, titleRef: CFTypeRef?
        AXUIElementCopyAttributeValue(node, kAXRoleAttribute as CFString, &roleRef)
        AXUIElementCopyAttributeValue(node, kAXURLAttribute as CFString, &urlRef)
        AXUIElementCopyAttributeValue(node, kAXTitleAttribute as CFString, &titleRef)
        let url = (urlRef as? NSURL)?.absoluteString ?? "-"
        print("\(String(repeating: "  ", count: depth))role=\((roleRef as? String) ?? "-") title=\"\((titleRef as? String) ?? "")\" url=\(url)")
        guard depth < maxDepth else { continue }
        var childrenRef: CFTypeRef?
        if AXUIElementCopyAttributeValue(node, kAXChildrenAttribute as CFString, &childrenRef) == .success,
           let children = childrenRef as? [AXUIElement] {
            frontier.insert(contentsOf: children.map { ($0, depth + 1) }, at: 0)
        }
    }
case "kinds":
    let minArea = args.count >= 4 ? (Double(args[3]) ?? 200_000) : 200_000
    let real = windows.filter { windowArea($0) >= minArea }
    // Printed even when a window has no URLs at all, so "the walk found
    // nothing" and "the window is not there" stay distinguishable.
    for (i, w) in real.enumerated() {
        print("window index=\(i) " + describeWindow(w) + " urls=[" + webAreaURLs(w).joined(separator: " ") + "]")
    }
    print("kinds=\(real.count) minArea=\(Int(minArea)) total=\(windows.count)")
case "focused":
    var focusedRef: CFTypeRef?
    if AXUIElementCopyAttributeValue(axApp, kAXFocusedWindowAttribute as CFString, &focusedRef) == .success,
       let fw = focusedRef, CFGetTypeID(fw) == AXUIElementGetTypeID() {
        // swiftlint:disable:next force_cast
        print("focused " + describeWindow(fw as! AXUIElement))
    } else {
        print("focused none")
    }
default:
    fail("unknown verb '\(verb)'")
}
