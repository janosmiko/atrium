// build/vm-tools/cgevent.swift
//
// Reusable CGEvent-based input probe tool for the atrium-probe Tart VM.
// Host-compiled INSIDE the VM (`swiftc cgevent.swift -o cgevent`), not on
// the host - the VM has no code-signing/AX-permission relationship with a
// host-built binary.
//
// Two hard-won fixes baked in here, both cost real debugging time upstream:
//
// 1. MODIFIER DELIVERY: stamping `.flags = .maskCommand` onto a
//    single synthetic key/mouse event delivers the modifier to THAT event,
//    but leaves Cmd "stuck" in the OS/app's own modifier-tracking state
//    afterward, causing accidental Cmd+T/Cmd+S/Cmd+N on later, unrelated
//    keystrokes. Fix: physically press the real modifier keycode (55 for
//    Cmd) with a keyDown event, post the target event with the flag still
//    set, then keyUp the modifier - modifier state is never left dangling.
//
// 2. RETINA COORDINATE SCALING: CGEvent mouse coordinates are in
//    POINTS, but `screencapture`'s PNG output and this VM's virtual display
//    are 2x Retina. A screenshot pixel coordinate must be divided by 2 (NOT
//    by any chat-preview scale factor) before passing it to this tool's
//    click/move commands - getting this wrong silently clicks the wrong
//    element instead of erroring, so verify with a control click first.
//
// 3. SETTLE DELAY: rapid-fire synthetic input with no settle time
//    between posts (e.g. omnibox typing without waiting for autocomplete)
//    can drive Chromium to 100% CPU and total input unresponsiveness for
//    minutes. Serialize probe actions - one input, one verification, then
//    the next - rather than batching many CGEvent posts back to back.
//
// Usage:
//   cgevent click <x> <y>                 plain left click
//   cgevent rightclick <x> <y>             right click (context menu)
//   cgevent cmdclick <x> <y>               cmd+left click
//   cgevent modclick <x> <y> <mods>        left click under e.g. cmd+opt
//   cgevent shiftclick <x> <y>             shift+left click
//   cgevent doubleclick <x> <y>            double left click
//   cgevent move <x> <y>                   move mouse (no click)
//   cgevent drag <x1> <y1> <x2> <y2> [steps]  mouse-down, N interpolated
//                                           drags, mouse-up (default 8 steps)
//   cgevent dragdwell <x1> <y1> <x2> <y2> <dwellms> [steps]
//                                           drag, HOLD at the end point for
//                                           dwellms, then mouse-up (dwell-armed
//                                           drop targets, todo 51 probes)
//   cgevent typetext <string>              types each character via unicode string events
//   cgevent keypress <keycode> [mod]       one keycode, optional modifier: cmd|shift|ctrl|opt
//   cgevent backspaces <n>                 n backspace keypresses
//   cgevent key <keycode>                  alias for `keypress <keycode>` (no modifier)
//   cgevent scroll <x> <y> <dx> [phaseMs] [momentumMs] [stepMs]
//                                           trackpad-style horizontal scroll
//                                           gesture with real .began/.changed/.ended phase
//                                           transitions (plus a momentum tail when
//                                           momentumMs > 0) - a plain scrollWheelEvent
//                                           carries no phase and is silently ignored by
//                                           any NSEvent.phase-driven recognizer (e.g.
//                                           SidebarSwipeCatcher). dx is the total
//                                           horizontal delta (points) spread across the
//                                           phase-active steps; phaseMs defaults to 300
//                                           (a slow scrub), momentumMs defaults to 0 (a
//                                           tracked drag with no release - pass e.g. 300
//                                           for a fast flick's momentum tail); stepMs
//                                           (default 20) is the interval between samples.
//   cgevent heldtaps <modifier> <keycode> <count> [tapDelayMs]
//                                           press the real modifier
//                                           ONCE, tap <keycode> <count> times
//                                           under it, release the modifier
//                                           ONCE. `keypress`'s withModifier
//                                           already presses/releases the real
//                                           modifier key, but does so around a
//                                           SINGLE event - a scenario chaining
//                                           several `keypress` calls releases
//                                           the modifier between them, which is
//                                           exactly the gap Ctrl-Tab recency
//                                           cycling depends on staying closed:
//                                           the walk is meant to freeze while
//                                           Control is held and commit only on
//                                           release. tapDelayMs (default 400)
//                                           is the pause between taps, not
//                                           inside one - the up/down pair of a
//                                           single tap keeps the existing
//                                           20ms `keypress` timing.
//   cgevent batch "<cmd args>;<cmd args>;wait <ms>;..."
//                                           runs a ';'-separated sequence of the above
//                                           commands (plus `wait <ms>`) in ONE process,
//                                           so no swiftc-binary-startup latency accrues
//                                           between steps - required for timing-sensitive
//                                           sequences like clicking a toast's Undo button
//                                           before its ~4s auto-dismiss fires. Chaining
//                                           separate `cgevent` invocations from the shell
//                                           (each paying process-startup cost) can easily
//                                           eat the whole window - this is what actually
//                                           caused the documented "raced the auto-
//                                           dismiss" miss.

import CoreGraphics
import Foundation

let kVK_Command: CGKeyCode = 55
let kVK_Shift: CGKeyCode = 56
let kVK_Control: CGKeyCode = 59
let kVK_Option: CGKeyCode = 58
let kVK_Delete: CGKeyCode = 51
let kVK_Return: CGKeyCode = 36
let kVK_Escape: CGKeyCode = 53

// Raw CGEvent scroll/momentum-phase field VALUES. CoreGraphics
// publishes the FIELD ids (.scrollWheelEventScrollPhase /
// .scrollWheelEventMomentumPhase) but not these Swift-side enum constants, so
// they are declared here the same way the kVK_* keycodes above are. A scroll
// event with none of these set is a legacy line-scroll event - it carries no
// NSEvent.phase/momentumPhase at all and is invisible to a phase-driven
// recognizer like SidebarSwipeCatcher, not merely imprecise to it.
let kCGScrollPhaseBegan: Int64 = 1
let kCGScrollPhaseChanged: Int64 = 2
let kCGScrollPhaseEnded: Int64 = 4
let kCGMomentumScrollPhaseBegin: Int64 = 1
let kCGMomentumScrollPhaseContinue: Int64 = 2
let kCGMomentumScrollPhaseEnd: Int64 = 3

func modifierKeycode(_ name: String) -> CGKeyCode? {
    switch name.lowercased() {
    case "cmd", "command": return kVK_Command
    case "shift": return kVK_Shift
    case "ctrl", "control": return kVK_Control
    case "opt", "option", "alt": return kVK_Option
    default: return nil
    }
}

func modifierFlag(_ name: String) -> CGEventFlags? {
    switch name.lowercased() {
    case "cmd", "command": return .maskCommand
    case "shift": return .maskShift
    case "ctrl", "control": return .maskControl
    case "opt", "option", "alt": return .maskAlternate
    default: return nil
    }
}

let source = CGEventSource(stateID: .hidSystemState)

func post(_ event: CGEvent?) {
    event?.post(tap: .cghidEventTap)
}

func sleepMs(_ ms: UInt32) {
    usleep(ms * 1000)
}

/// Cumulative flags `withModifier` stamps onto each keyDown (added, in press
/// order) then each keyUp (removed, in release order) - one entry per event,
/// press events first. Pure so `flags-selftest` can check it without posting.
func modifierFlagSequence(_ modFlags: [CGEventFlags]) -> [CGEventFlags] {
    var sequence: [CGEventFlags] = []
    var held: CGEventFlags = []
    for flag in modFlags {
        held.insert(flag)
        sequence.append(held)
    }
    for flag in modFlags.reversed() {
        held.remove(flag)
        sequence.append(held)
    }
    return sequence
}

/// Presses the real modifier key(s), runs `body` with the combined flags
/// applied to every posted event, then releases the real modifier key(s) in
/// reverse order. This is fix #1 above - never stamp a flag onto an isolated
/// event without the paired physical press/release. `name` may combine
/// modifiers with '+' (e.g. "cmd+opt" for Cmd+Option+N) - needed once split-
/// tab's Cmd+Option+N shortcut required two modifiers held simultaneously,
/// which a single-modifier press/release could not represent.
func withModifier(_ name: String?, _ body: (CGEventFlags) -> Void) {
    guard let name else {
        body([])
        return
    }
    let names = name.split(separator: "+").map(String.init)
    let keycodes = names.compactMap { modifierKeycode($0) }
    let modFlags = names.compactMap { modifierFlag($0) }
    guard keycodes.count == names.count, modFlags.count == names.count else {
        body([])
        return
    }
    guard !keycodes.isEmpty else {
        body([])
        return
    }
    // A release posted with no `.flags` leaves the OS reading the modifier as
    // still held. Stamp the cumulative flags onto each press/release so the
    // release actually clears it.
    let sequence = modifierFlagSequence(modFlags)
    for (i, keycode) in keycodes.enumerated() {
        let down = CGEvent(keyboardEventSource: source, virtualKey: keycode, keyDown: true)
        down?.flags = sequence[i]
        post(down)
        sleepMs(30)
    }
    body(sequence[keycodes.count - 1])
    sleepMs(30)
    for (i, keycode) in keycodes.reversed().enumerated() {
        let up = CGEvent(keyboardEventSource: source, virtualKey: keycode, keyDown: false)
        up?.flags = sequence[keycodes.count + i]
        post(up)
    }
}

func click(x: Double, y: Double, button: CGMouseButton, flags: CGEventFlags, double: Bool = false) {
    let point = CGPoint(x: x, y: y)
    let (downType, upType): (CGEventType, CGEventType)
    switch button {
    case .left: (downType, upType) = (.leftMouseDown, .leftMouseUp)
    case .right: (downType, upType) = (.rightMouseDown, .rightMouseUp)
    default: (downType, upType) = (.otherMouseDown, .otherMouseUp)
    }
    let clickCount = double ? 2 : 1
    for i in 1...clickCount {
        let down = CGEvent(mouseEventSource: source, mouseType: downType, mouseCursorPosition: point, mouseButton: button)
        down?.flags = flags
        down?.setIntegerValueField(.mouseEventClickState, value: Int64(i))
        post(down)
        sleepMs(20)
        let up = CGEvent(mouseEventSource: source, mouseType: upType, mouseCursorPosition: point, mouseButton: button)
        up?.flags = flags
        up?.setIntegerValueField(.mouseEventClickState, value: Int64(i))
        post(up)
        if double { sleepMs(20) }
    }
}

func move(x: Double, y: Double) {
    let point = CGPoint(x: x, y: y)
    post(CGEvent(mouseEventSource: source, mouseType: .mouseMoved, mouseCursorPosition: point, mouseButton: .left))
}

/// Split-view VM probe: left-mouse-down at (x1,y1), a handful of
/// interpolated leftMouseDragged steps to (x2,y2) (a real resize-area drag
/// delegate reports incremental deltas, not a single teleport - see
/// AtriumSplitResizeArea's OnMouseDragged), then mouse-up at the final
/// point. `steps` controls how many intermediate drag events are posted.
func drag(x1: Double, y1: Double, x2: Double, y2: Double, steps: Int = 8) {
    let down = CGEvent(mouseEventSource: source, mouseType: .leftMouseDown,
                       mouseCursorPosition: CGPoint(x: x1, y: y1), mouseButton: .left)
    post(down)
    sleepMs(30)
    for i in 1...steps {
        let t = Double(i) / Double(steps)
        let point = CGPoint(x: x1 + (x2 - x1) * t, y: y1 + (y2 - y1) * t)
        let dragged = CGEvent(mouseEventSource: source, mouseType: .leftMouseDragged,
                              mouseCursorPosition: point, mouseButton: .left)
        post(dragged)
        sleepMs(20)
    }
    let up = CGEvent(mouseEventSource: source, mouseType: .leftMouseUp,
                     mouseCursorPosition: CGPoint(x: x2, y: y2), mouseButton: .left)
    post(up)
}

/// Tab-detach VM probe: like `drag`, but HOLDS at (x2,y2) for `dwellMs`
/// (posting periodic same-point leftMouseDragged events, so dwell-armed
/// drop targets - the space switcher's dwell-switch cards - keep seeing an
/// active drag) before releasing. A plain `drag` releases immediately,
/// too fast for any dwell affordance to arm.
func dragDwell(x1: Double, y1: Double, x2: Double, y2: Double, dwellMs: Int, steps: Int = 8) {
    let down = CGEvent(mouseEventSource: source, mouseType: .leftMouseDown,
                       mouseCursorPosition: CGPoint(x: x1, y: y1), mouseButton: .left)
    post(down)
    sleepMs(30)
    for i in 1...steps {
        let t = Double(i) / Double(steps)
        let point = CGPoint(x: x1 + (x2 - x1) * t, y: y1 + (y2 - y1) * t)
        let dragged = CGEvent(mouseEventSource: source, mouseType: .leftMouseDragged,
                              mouseCursorPosition: point, mouseButton: .left)
        post(dragged)
        sleepMs(20)
    }
    var held = 0
    while held < dwellMs {
        let dragged = CGEvent(mouseEventSource: source, mouseType: .leftMouseDragged,
                              mouseCursorPosition: CGPoint(x: x2, y: y2), mouseButton: .left)
        post(dragged)
        sleepMs(50)
        held += 50
    }
    let up = CGEvent(mouseEventSource: source, mouseType: .leftMouseUp,
                     mouseCursorPosition: CGPoint(x: x2, y: y2), mouseButton: .left)
    post(up)
}

/// One phase- or momentum-phase-stamped scroll wheel event at `point`.
/// `.location` is set explicitly (scroll events, unlike the mouse
/// constructors above, take no position parameter) - belt-and-suspenders
/// alongside the `move` call in `scroll` below, so window hit-testing lands
/// on the intended view even if the two ever race. `units: .pixel` keeps
/// `NSEvent.hasPreciseScrollingDeltas` true, matching a real trackpad rather
/// than a notched mouse wheel.
/// `deltaY` rides wheel1, the VERTICAL axis, which was hard-coded to 0 until
/// Nothing needed it while the only gesture under test was the
/// horizontal swipe, and its absence made every vertical behaviour - the tab
/// list's rubber band first - impossible to drive from a scenario at all.
func scrollWheelEvent(deltaX: Int32, deltaY: Int32 = 0, axes: UInt32 = 2, scrollPhase: Int64, momentumPhase: Int64, at point: CGPoint) -> CGEvent? {
    // wheelCount is the event's SHAPE and belongs to the caller, never to the
    // data. A vertical-only gesture is wheelCount 1 with wheel1 alone;
    // declaring 2 axes with the horizontal at zero posts events that move
    // nothing at all.
    //
    // Deriving it from `deltaX != 0` looked tidier and re-tuned every
    // horizontal swipe in the suite: a phase gesture ENDS with a zero-delta
    // terminator, so that final event silently dropped to one axis and the
    // accumulated scrub distance fell from 64-66 to 44-46.
    guard let event = CGEvent(scrollWheelEvent2Source: source, units: .pixel, wheelCount: axes,
                               wheel1: deltaY, wheel2: deltaX, wheel3: 0) else { return nil }
    event.location = point
    if scrollPhase != 0 {
        event.setIntegerValueField(.scrollWheelEventScrollPhase, value: scrollPhase)
    }
    if momentumPhase != 0 {
        event.setIntegerValueField(.scrollWheelEventMomentumPhase, value: momentumPhase)
    }
    return event
}

/// trackpad-style horizontal scroll gesture with real phase
/// transitions, the capability the VM harness was entirely missing (clicks/
/// drags/keys only). `totalDeltaX` is split evenly across `phaseMs`/`stepMs`
/// .began+.changed steps, then closed with a zero-delta .ended - matching
/// real hardware, whose terminal event carries no delta. `momentumMs > 0`
/// appends a decaying momentum tail (begin/continue/end) simulating a
/// released-finger flick continuing under inertia; omit it for a slow scrub
/// the user tracks all the way to a stop (no release, no momentum). `stepMs`
/// (default 20, matching a real trackpad's ~50Hz reporting rate) is exposed
/// because real hardware/OS combinations vary their sampling rate - a probe
/// chasing a rate-sensitive receiver bug needs to be able to dial it.
func scroll(x: Double, y: Double, totalDeltaX: Int32, totalDeltaY: Int32 = 0, axes: UInt32 = 2, phaseMs: Int, momentumMs: Int, stepMs: UInt32 = 20) {
    let point = CGPoint(x: x, y: y)
    move(x: x, y: y)
    sleepMs(20)

    let steps = max(1, phaseMs / Int(stepMs))
    let perStep = Int32((Double(totalDeltaX) / Double(steps)).rounded())
    let perStepY = Int32((Double(totalDeltaY) / Double(steps)).rounded())

    post(scrollWheelEvent(deltaX: perStep, deltaY: perStepY, axes: axes, scrollPhase: kCGScrollPhaseBegan, momentumPhase: 0, at: point))
    sleepMs(stepMs)
    for _ in 1..<steps {
        post(scrollWheelEvent(deltaX: perStep, deltaY: perStepY, axes: axes, scrollPhase: kCGScrollPhaseChanged, momentumPhase: 0, at: point))
        sleepMs(stepMs)
    }
    post(scrollWheelEvent(deltaX: 0, deltaY: 0, axes: axes, scrollPhase: kCGScrollPhaseEnded, momentumPhase: 0, at: point))
    guard momentumMs > 0 else { return }

    sleepMs(stepMs)
    let momentumSteps = max(1, momentumMs / Int(stepMs))
    var momentumDelta = perStep * 2  // a release hands off faster than the tracked drag that led to it
    var momentumDeltaY = perStepY * 2
    post(scrollWheelEvent(deltaX: momentumDelta, deltaY: momentumDeltaY, axes: axes, scrollPhase: 0, momentumPhase: kCGMomentumScrollPhaseBegin, at: point))
    sleepMs(stepMs)
    for _ in 1..<momentumSteps {
        momentumDelta = Int32((Double(momentumDelta) * 0.85).rounded())  // decay, matches real trackpad deceleration
        momentumDeltaY = Int32((Double(momentumDeltaY) * 0.85).rounded())
        post(scrollWheelEvent(deltaX: momentumDelta, deltaY: momentumDeltaY, axes: axes, scrollPhase: 0, momentumPhase: kCGMomentumScrollPhaseContinue, at: point))
        sleepMs(stepMs)
    }
    post(scrollWheelEvent(deltaX: 0, scrollPhase: 0, momentumPhase: kCGMomentumScrollPhaseEnd, at: point))
}

func typeText(_ text: String) {
    for scalar in text.unicodeScalars {
        var chars: [UniChar] = [UniChar(scalar.value)]
        let down = CGEvent(keyboardEventSource: source, virtualKey: 0, keyDown: true)
        down?.keyboardSetUnicodeString(stringLength: chars.count, unicodeString: &chars)
        post(down)
        sleepMs(15)
        let up = CGEvent(keyboardEventSource: source, virtualKey: 0, keyDown: false)
        up?.keyboardSetUnicodeString(stringLength: chars.count, unicodeString: &chars)
        post(up)
        sleepMs(15)
    }
    // Posting is asynchronous, and an exit right after the last key up dropped
    // that key in the VM ("/not" for "/note").
    sleepMs(150)
}

func keypress(keycode: CGKeyCode, modifier: String?) {
    withModifier(modifier) { flags in
        let down = CGEvent(keyboardEventSource: source, virtualKey: keycode, keyDown: true)
        down?.flags = flags
        post(down)
        sleepMs(20)
        let up = CGEvent(keyboardEventSource: source, virtualKey: keycode, keyDown: false)
        up?.flags = flags
        post(up)
    }
}

/// one held modifier, several taps, one release - see the usage
/// doc above for why `keypress` cannot express this. `withModifier` already
/// does the real press/release around the whole closure; this only adds the
/// tap loop inside it, with `tapDelayMs` between taps (not within one - a
/// single tap keeps `keypress`'s own 20ms up/down timing).
func heldTaps(modifier: String, keycode: CGKeyCode, count: Int, tapDelayMs: UInt32) {
    withModifier(modifier) { flags in
        for i in 0..<count {
            let down = CGEvent(keyboardEventSource: source, virtualKey: keycode, keyDown: true)
            down?.flags = flags
            post(down)
            sleepMs(20)
            let up = CGEvent(keyboardEventSource: source, virtualKey: keycode, keyDown: false)
            up?.flags = flags
            post(up)
            if i < count - 1 { sleepMs(tapDelayMs) }
        }
    }
}

let args = CommandLine.arguments

// Runs before any dispatch that would post a real event to the desktop -
// Regression guard for the flag bookkeeping, checked on argv alone.
if args.count >= 2 && args[1] == "flags-selftest" {
    var failures = 0
    let cases: [(String, [CGEventFlags], String)] = [
        ("shift", [.maskShift, []], "single modifier: pressed then cleared"),
        ("cmd+opt", [.maskCommand, [.maskCommand, .maskAlternate], .maskCommand, []],
         "two modifiers: press adds in order, release removes in reverse"),
        ("", [], "empty name: no press/release events, withModifier must not index into this"),
    ]
    for (name, want, why) in cases {
        let names = name.split(separator: "+").map(String.init)
        let modFlags = names.compactMap { modifierFlag($0) }
        let got = modifierFlagSequence(modFlags)
        if got.map({ $0.rawValue }) != want.map({ $0.rawValue }) {
            print("FAIL \(name): want \(want.map { $0.rawValue }) got \(got.map { $0.rawValue }) - \(why)")
            failures += 1
        }
    }
    print(failures == 0 ? "flags-selftest: ok (\(cases.count) cases)" : "flags-selftest: \(failures) failed")
    exit(failures == 0 ? 0 : 1)
}

/// Dispatches one already-tokenized command (e.g. ["click", "10", "20"]).
/// Shared by direct argv dispatch and `batch` mode.
func dispatch(_ tokens: [String]) {
    guard let cmd = tokens.first else { return }
    let args = tokens
    switch cmd {
    case "click":
        click(x: Double(args[1])!, y: Double(args[2])!, button: .left, flags: [])
    case "rightclick":
        click(x: Double(args[1])!, y: Double(args[2])!, button: .right, flags: [])
    case "middleclick":
        click(x: Double(args[1])!, y: Double(args[2])!, button: .center, flags: [])
    case "cmdclick":
        withModifier("cmd") { flags in
            click(x: Double(args[1])!, y: Double(args[2])!, button: .left, flags: flags)
        }
    case "modclick":
        withModifier(args.count > 3 ? args[3] : nil) { flags in
            click(x: Double(args[1])!, y: Double(args[2])!, button: .left, flags: flags)
        }
    case "shiftclick":
        withModifier("shift") { flags in
            click(x: Double(args[1])!, y: Double(args[2])!, button: .left, flags: flags)
        }
    case "doubleclick":
        click(x: Double(args[1])!, y: Double(args[2])!, button: .left, flags: [], double: true)
    case "move":
        move(x: Double(args[1])!, y: Double(args[2])!)
    case "drag":
        let steps = args.count > 5 ? Int(args[5])! : 8
        drag(x1: Double(args[1])!, y1: Double(args[2])!, x2: Double(args[3])!, y2: Double(args[4])!, steps: steps)
    case "dragdwell":
        let steps = args.count > 6 ? Int(args[6])! : 8
        dragDwell(x1: Double(args[1])!, y1: Double(args[2])!, x2: Double(args[3])!, y2: Double(args[4])!,
                  dwellMs: Int(args[5])!, steps: steps)
    // Item 61/63 frame-sequence probes: raw down/dragged/up as SEPARATE
    // invocations (each a fresh process) so a caller can interleave
    // screencapture between individual drag-motion steps - `drag`/
    // `dragdwell` run their whole gesture in one blocking call, too fast to
    // screenshot mid-motion. The mouse-button-down state lives in the OS,
    // not this process, so posting leftMouseDragged/Up from a later,
    // separate process invocation still reads as one continuous gesture to
    // the receiving app.
    case "rawdown":
        post(CGEvent(mouseEventSource: source, mouseType: .leftMouseDown,
                     mouseCursorPosition: CGPoint(x: Double(args[1])!, y: Double(args[2])!), mouseButton: .left))
    case "rawdragged":
        post(CGEvent(mouseEventSource: source, mouseType: .leftMouseDragged,
                     mouseCursorPosition: CGPoint(x: Double(args[1])!, y: Double(args[2])!), mouseButton: .left))
    case "rawup":
        post(CGEvent(mouseEventSource: source, mouseType: .leftMouseUp,
                     mouseCursorPosition: CGPoint(x: Double(args[1])!, y: Double(args[2])!), mouseButton: .left))
    case "typetext":
        typeText(args[1])
    case "keypress", "key":
        let keycode = CGKeyCode(args[1])!
        let modifier = args.count > 2 ? args[2] : nil
        keypress(keycode: keycode, modifier: modifier)
    case "backspaces":
        let n = Int(args[1])!
        for _ in 0..<n {
            keypress(keycode: kVK_Delete, modifier: nil)
        }
    case "heldtaps":
        let modifier = args[1]
        let keycode = CGKeyCode(args[2])!
        let count = Int(args[3])!
        let tapDelayMs = args.count > 4 ? UInt32(args[4])! : 400
        heldTaps(modifier: modifier, keycode: keycode, count: count, tapDelayMs: tapDelayMs)
    case "scroll":
        let phaseMs = args.count > 4 ? Int(args[4])! : 300
        let momentumMs = args.count > 5 ? Int(args[5])! : 0
        let stepMs = args.count > 6 ? UInt32(args[6])! : 20
        scroll(x: Double(args[1])!, y: Double(args[2])!, totalDeltaX: Int32(args[3])!,
               phaseMs: phaseMs, momentumMs: momentumMs, stepMs: stepMs)
    case "vscroll":
        // A separate subcommand rather than a fourth positional on `scroll`:
        // every swipe scenario passes `scroll x y dx ...` and a new positional
        // in the middle would silently reinterpret their arguments. Same
        // shape, vertical axis.
        let vPhaseMs = args.count > 4 ? Int(args[4])! : 300
        let vMomentumMs = args.count > 5 ? Int(args[5])! : 0
        let vStepMs = args.count > 6 ? UInt32(args[6])! : 20
        scroll(x: Double(args[1])!, y: Double(args[2])!, totalDeltaX: 0,
               totalDeltaY: Int32(args[3])!, axes: 1,
               phaseMs: vPhaseMs, momentumMs: vMomentumMs, stepMs: vStepMs)
    case "wait":
        sleepMs(UInt32(args[1])!)
    default:
        FileHandle.standardError.write("unknown command: \(cmd)\n".data(using: .utf8)!)
        exit(1)
    }
}

guard args.count >= 2 else {
    FileHandle.standardError.write("usage: cgevent <click|rightclick|middleclick|cmdclick|shiftclick|doubleclick|move|drag|scroll|vscroll|typetext|keypress|backspaces|key|heldtaps|batch> ...\n".data(using: .utf8)!)
    exit(1)
}

if args[1] == "batch" {
    guard args.count >= 3 else {
        FileHandle.standardError.write("usage: cgevent batch \"<cmd args>;<cmd args>;...\"\n".data(using: .utf8)!)
        exit(1)
    }
    let steps = args[2].split(separator: ";").map { $0.trimmingCharacters(in: .whitespaces) }
    for step in steps where !step.isEmpty {
        dispatch(step.split(separator: " ").map(String.init))
    }
} else {
    dispatch(Array(args.dropFirst()))
}
