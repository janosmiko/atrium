// rapidcap.swift - single-process burst screen capture for catching short
// CoreAnimation transitions that a forked `screencapture` per frame cannot
// (each fork costs ~150-190ms, exceeding a 160-200ms slide animation).
// macOS 26 removed CGDisplayCreateImage (SCREEN_CAPTURE_OBSOLETE), so this
// uses ScreenCaptureKit's SCStream at a fixed high frame rate, buffering
// CGImages in memory, then writes them all to disk after the burst.
//
// Usage: rapidcap <outdir> <durationMs> <fps> [moveX moveY moveDelayMs]
//   If moveX/moveY given, posts a CGEvent mouseMoved to that point
//   moveDelayMs (default 0) after the stream starts, in-process (no ssh
//   round-trip jitter).
import AppKit
import CoreGraphics
import Foundation
import ImageIO
import ScreenCaptureKit
import UniformTypeIdentifiers

let args = CommandLine.arguments
guard args.count >= 4 else {
    FileHandle.standardError.write("usage: rapidcap <outdir> <durationMs> <fps> [x y moveDelayMs]\n".data(using: .utf8)!)
    exit(1)
}
let outdir = args[1]
let durationMs = Double(args[2])!
let fps = Double(args[3])!
let moveX = args.count >= 6 ? Double(args[4]) : nil
let moveY = args.count >= 6 ? Double(args[5]) : nil
let moveDelayMs = args.count >= 7 ? Double(args[6])! : 0

final class FrameCollector: NSObject, SCStreamOutput {
    var frames: [(CGImage, Double)] = []
    let start = Date()
    let lock = NSLock()

    func stream(_ stream: SCStream, didOutputSampleBuffer sampleBuffer: CMSampleBuffer, of type: SCStreamOutputType) {
        guard type == .screen, let imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer) else { return }
        let ciImage = CIImage(cvPixelBuffer: imageBuffer)
        let ctx = CIContext()
        guard let cgImage = ctx.createCGImage(ciImage, from: ciImage.extent) else { return }
        let t = Date().timeIntervalSince(start)
        lock.lock()
        frames.append((cgImage, t))
        lock.unlock()
    }
}

let semaphore = DispatchSemaphore(value: 0)
var collector: FrameCollector!

// the FIRST SCStream started after keyboard input is swallowed - it
// delivers no frames at all and the process still exits 0, so the capture that
// follows a `type` action silently loses its evidence. rapidcap starts a fresh
// stream per invocation and replayd evaluates an alert check on every start,
// the same mechanism vm-scenario.sh's launch warm-up absorbs but which nothing
// covers after input. One retry is enough: the second stream has always
// delivered, and retrying here fixes every caller rather than the scenarios
// whose author remembered to stage a throwaway capture.
func captureOnce() async throws -> FrameCollector {
    let content = try await SCShareableContent.excludingDesktopWindows(false, onScreenWindowsOnly: true)
    guard let display = content.displays.first else {
        FileHandle.standardError.write("no display found\n".data(using: .utf8)!)
        exit(1)
    }
    let filter = SCContentFilter(display: display, excludingWindows: [])
    let config = SCStreamConfiguration()
    config.width = display.width
    config.height = display.height
    config.minimumFrameInterval = CMTime(value: 1, timescale: Int32(fps))
    config.showsCursor = true
    config.pixelFormat = kCVPixelFormatType_32BGRA

    let attempt = FrameCollector()
    let live = SCStream(filter: filter, configuration: config, delegate: nil)
    try live.addStreamOutput(attempt, type: .screen, sampleHandlerQueue: DispatchQueue(label: "rapidcap.capture"))
    try await live.startCapture()

    if let x = moveX, let y = moveY {
        if moveDelayMs > 0 {
            try? await Task.sleep(nanoseconds: UInt64(moveDelayMs * 1_000_000))
        }
        let source = CGEventSource(stateID: .hidSystemState)
        let point = CGPoint(x: x, y: y)
        let move = CGEvent(mouseEventSource: source, mouseType: .mouseMoved, mouseCursorPosition: point, mouseButton: .left)
        move?.post(tap: .cghidEventTap)
    }

    try? await Task.sleep(nanoseconds: UInt64(durationMs * 1_000_000))
    try await live.stopCapture()
    return attempt
}

Task {
    do {
        collector = try await captureOnce()
        if collector.frames.isEmpty {
            FileHandle.standardError.write(
                "first stream delivered no frames, retrying once\n".data(using: .utf8)!)
            collector = try await captureOnce()
        }
        semaphore.signal()
    } catch {
        FileHandle.standardError.write("capture error: \(error)\n".data(using: .utf8)!)
        exit(1)
    }
}

semaphore.wait()

try? FileManager.default.createDirectory(atPath: outdir, withIntermediateDirectories: true)
for (i, entry) in collector.frames.enumerated() {
    let (img, t) = entry
    let path = "\(outdir)/frame-\(String(format: "%03d", i))-t\(String(format: "%05.1f", t * 1000)).png"
    let url = URL(fileURLWithPath: path)
    guard let dest = CGImageDestinationCreateWithURL(url as CFURL, "public.png" as CFString, 1, nil) else { continue }
    CGImageDestinationAddImage(dest, img, nil)
    CGImageDestinationFinalize(dest)
}
print("wrote \(collector.frames.count) frames to \(outdir)")
