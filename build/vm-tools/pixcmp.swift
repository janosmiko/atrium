// pixcmp.swift - region colour compare for the scenario runner.
//
// Why: the theme-per-space regression needs a probe that proves a themed
// backdrop ACTUALLY changed across a space switch, not just that a VLOG fired.
// BUILD.md's probe-fidelity rules forbid outcome-only claims and PNG file-hash
// comparisons (encoder noise makes hashes differ on identical pixels; a
// byte-for-byte copy once shipped as a fake before/after). So this reads raw
// decoded pixels via ImageIO, averages a backdrop region, and reports the
// mean-colour delta plus the region's own variance (a flat backdrop has low
// variance; noise/gradient has higher - variance is the "is this really a
// backdrop" context the fidelity rules ask for, not a single-frame claim).
//
// Runs on the HOST (pure image math, no display/AX/TCC needed) after frames
// are pulled back from the VM, or inside the VM - either works.
//
// WHAT `diff` CANNOT SEE, and why `moved` exists.
// `diff`'s delta is a euclidean distance between the two region MEANS. Averaging
// the region first throws away where inside it anything happened, and that loses
// real movement two different ways:
//
//   CANCELLATION. A region spanning both ends of a movement scores near zero,
//   because what leaves one end and arrives at the other cancels in the shared
//   average. Measured: a two-row region read 0.75 for a highlight that plainly
//   moved, and 32-36 once narrowed to a single row.
//
//   DESATURATION. The delta also carries the magnitude of the colour change, so
//   it drops when a thing still moves but is painted paler. sidebar-vertical-band
//   read 9.88 against a bar of 10.0 for weeks while its gesture worked perfectly -
//   the pill had lost its tint, not its motion.
//
// `moved` compares CORRESPONDING PIXELS and counts how many changed. Nothing is
// averaged before the comparison, so nothing cancels, and the count is scale-free:
// a pale thing moving scores the same as a saturated one. It answers "did this
// region's content move", where `diff` answers "and is it the right colour".
//
// Usage:
//   pixcmp stats <png> <x> <y> <w> <h>
//       -> "mean=r,g,b var=v n=<px>"  (region average + variance)
//   pixcmp diff <before.png> <after.png> <x> <y> <w> <h> <minDelta>
//       -> prints both means, the euclidean RGB delta, and both variances.
//          exit 0: changed  (delta >= minDelta)          -> assertion PASS
//          exit 2: unchanged (delta <  minDelta)          -> assertion FAIL
//          exit 3: inputs are byte-identical              -> fake/duplicate frame
//   pixcmp moved <before.png> <after.png> <x> <y> <w> <h> <minChangedPct> [tol]
//       -> prints the share of the region's pixels whose colour moved further
//          than <tol> (default 12), plus the mean/max per-pixel distance and the
//          mean-based delta for context.
//          exit 0: moved   (changed_pct >= minChangedPct) -> assertion PASS
//          exit 2: still   (changed_pct <  minChangedPct) -> assertion FAIL
//          exit 3: inputs are byte-identical              -> fake/duplicate frame
//
// `step` walks a row and reports where a band ends. A mean says nothing about
// a band's width, and two bands of different widths can average alike.
//
// Compile on the host: swiftc -O pixcmp.swift -o pixcmp
import CoreGraphics
import Foundation
import ImageIO

func fail(_ msg: String) -> Never {
    FileHandle.standardError.write((msg + "\n").data(using: .utf8)!)
    exit(1)
}

struct RegionStat {
    let mean: (Double, Double, Double)
    let variance: Double
    let count: Int
}

func loadImage(_ path: String) -> CGImage {
    let url = URL(fileURLWithPath: path)
    guard let src = CGImageSourceCreateWithURL(url as CFURL, nil),
          let img = CGImageSourceCreateImageAtIndex(src, 0, nil) else {
        fail("cannot decode image: \(path)")
    }
    return img
}

// Decode into a known-order RGBA8 buffer so pixel offsets are deterministic
// regardless of the source PNG's native layout.
func rgbaBytes(_ img: CGImage) -> (bytes: [UInt8], width: Int, height: Int) {
    let width = img.width
    let height = img.height
    let bytesPerRow = width * 4
    var buf = [UInt8](repeating: 0, count: bytesPerRow * height)
    let cs = CGColorSpaceCreateDeviceRGB()
    let bitmap = CGImageAlphaInfo.premultipliedLast.rawValue
    guard let ctx = buf.withUnsafeMutableBytes({ ptr in
        CGContext(data: ptr.baseAddress, width: width, height: height,
                  bitsPerComponent: 8, bytesPerRow: bytesPerRow,
                  space: cs, bitmapInfo: bitmap)
    }) else { fail("cannot create bitmap context") }
    ctx.draw(img, in: CGRect(x: 0, y: 0, width: width, height: height))
    return (buf, width, height)
}

func regionStat(_ path: String, _ rx: Int, _ ry: Int, _ rw: Int, _ rh: Int) -> RegionStat {
    let (bytes, width, height) = rgbaBytes(loadImage(path))
    let x0 = max(0, rx), y0 = max(0, ry)
    let x1 = min(width, rx + rw), y1 = min(height, ry + rh)
    guard x1 > x0, y1 > y0 else { fail("region \(rx),\(ry) \(rw)x\(rh) outside image \(width)x\(height)") }
    var sr = 0.0, sg = 0.0, sb = 0.0
    var n = 0
    // First pass: means.
    for y in y0..<y1 {
        for x in x0..<x1 {
            let o = (y * width + x) * 4
            sr += Double(bytes[o]); sg += Double(bytes[o + 1]); sb += Double(bytes[o + 2])
            n += 1
        }
    }
    let mr = sr / Double(n), mg = sg / Double(n), mb = sb / Double(n)
    // Second pass: variance of per-pixel luminance around the region mean.
    var sv = 0.0
    for y in y0..<y1 {
        for x in x0..<x1 {
            let o = (y * width + x) * 4
            let dr = Double(bytes[o]) - mr, dg = Double(bytes[o + 1]) - mg, db = Double(bytes[o + 2]) - mb
            sv += (dr * dr + dg * dg + db * db) / 3.0
        }
    }
    return RegionStat(mean: (mr, mg, mb), variance: sv / Double(n), count: n)
}

struct MoveStat {
    let changedPct: Double
    let changed: Int
    let count: Int
    let meanDistance: Double
    let maxDistance: Double
}

// Per-pixel comparison of the same region in two frames. Both frames must have
// the same dimensions - a per-pixel compare across differently sized captures is
// comparing two different places, and reporting a number for it would be worse
// than refusing.
func moveStat(_ beforePath: String, _ afterPath: String,
              _ rx: Int, _ ry: Int, _ rw: Int, _ rh: Int, _ tol: Double) -> MoveStat {
    let (ab, aw, ah) = rgbaBytes(loadImage(beforePath))
    let (bb, bw, bh) = rgbaBytes(loadImage(afterPath))
    guard aw == bw, ah == bh else {
        fail("frames differ in size (\(aw)x\(ah) vs \(bw)x\(bh)) - the same region is not the same place in both")
    }
    let x0 = max(0, rx), y0 = max(0, ry)
    let x1 = min(aw, rx + rw), y1 = min(ah, ry + rh)
    guard x1 > x0, y1 > y0 else { fail("region \(rx),\(ry) \(rw)x\(rh) outside image \(aw)x\(ah)") }
    var changed = 0, n = 0
    var sum = 0.0, maxD = 0.0
    for y in y0..<y1 {
        for x in x0..<x1 {
            let o = (y * aw + x) * 4
            let dr = Double(ab[o]) - Double(bb[o])
            let dg = Double(ab[o + 1]) - Double(bb[o + 1])
            let db = Double(ab[o + 2]) - Double(bb[o + 2])
            let d = (dr * dr + dg * dg + db * db).squareRoot()
            sum += d
            if d > maxD { maxD = d }
            if d > tol { changed += 1 }
            n += 1
        }
    }
    return MoveStat(changedPct: 100.0 * Double(changed) / Double(n), changed: changed,
                    count: n, meanDistance: sum / Double(n), maxDistance: maxD)
}

// The mean colour of a <thick>-deep strip across the scan, so one noisy pixel
// cannot look like the end of a band.
func stripMean(_ bytes: [UInt8], _ width: Int, _ height: Int,
               _ sx: Int, _ sy: Int, _ thick: Int,
               _ vertical: Bool) -> (Double, Double, Double)? {
    var sr = 0.0, sg = 0.0, sb = 0.0, n = 0
    for k in 0..<thick {
        let x = vertical ? sx + k : sx
        let y = vertical ? sy : sy + k
        guard x >= 0, x < width, y >= 0, y < height else { continue }
        let o = (y * width + x) * 4
        sr += Double(bytes[o]); sg += Double(bytes[o + 1]); sb += Double(bytes[o + 2])
        n += 1
    }
    guard n > 0 else { return nil }
    return (sr / Double(n), sg / Double(n), sb / Double(n))
}

func fileBytesEqual(_ a: String, _ b: String) -> Bool {
    guard let da = FileManager.default.contents(atPath: a),
          let db = FileManager.default.contents(atPath: b) else { return false }
    return da == db
}

let args = CommandLine.arguments
guard args.count >= 2 else { fail("usage: pixcmp <stats|diff> ...") }

switch args[1] {
case "stats":
    guard args.count == 7,
          let x = Int(args[3]), let y = Int(args[4]),
          let w = Int(args[5]), let h = Int(args[6]) else { fail("usage: pixcmp stats <png> <x> <y> <w> <h>") }
    let s = regionStat(args[2], x, y, w, h)
    print(String(format: "mean=%.1f,%.1f,%.1f var=%.1f n=%d", s.mean.0, s.mean.1, s.mean.2, s.variance, s.count))
case "diff":
    guard args.count == 9,
          let x = Int(args[4]), let y = Int(args[5]),
          let w = Int(args[6]), let h = Int(args[7]),
          let minDelta = Double(args[8]) else {
        fail("usage: pixcmp diff <before.png> <after.png> <x> <y> <w> <h> <minDelta>")
    }
    let before = args[2], after = args[3]
    if fileBytesEqual(before, after) {
        print("IDENTICAL byte-identical inputs \(before) == \(after) (fake/duplicate frame)")
        exit(3)
    }
    let a = regionStat(before, x, y, w, h)
    let b = regionStat(after, x, y, w, h)
    let dr = a.mean.0 - b.mean.0, dg = a.mean.1 - b.mean.1, db = a.mean.2 - b.mean.2
    let delta = (dr * dr + dg * dg + db * db).squareRoot()
    let verdict = delta >= minDelta ? "CHANGED" : "UNCHANGED"
    print(String(format: "%@ delta=%.2f minDelta=%.2f before_mean=%.1f,%.1f,%.1f after_mean=%.1f,%.1f,%.1f before_var=%.1f after_var=%.1f",
                 verdict, delta, minDelta,
                 a.mean.0, a.mean.1, a.mean.2, b.mean.0, b.mean.1, b.mean.2, a.variance, b.variance))
    exit(delta >= minDelta ? 0 : 2)
case "moved":
    guard args.count == 9 || args.count == 10,
          let x = Int(args[4]), let y = Int(args[5]),
          let w = Int(args[6]), let h = Int(args[7]),
          let minChangedPct = Double(args[8]) else {
        fail("usage: pixcmp moved <before.png> <after.png> <x> <y> <w> <h> <minChangedPct> [tol]")
    }
    // 12 is a euclidean RGB distance - roughly 7 per channel. Below that a
    // difference is capture and encoder noise: two at-rest frames from the same
    // burst score 0.0% changed at this tolerance (see the scenario's own note).
    let tol = args.count == 10 ? (Double(args[9]) ?? 12.0) : 12.0
    let before = args[2], after = args[3]
    if fileBytesEqual(before, after) {
        print("IDENTICAL byte-identical inputs \(before) == \(after) (fake/duplicate frame)")
        exit(3)
    }
    let m = moveStat(before, after, x, y, w, h, tol)
    // The mean-based numbers ride along as context, never as the verdict: an
    // evidence line that carries both is what let the 9.88 miss be diagnosed.
    let a = regionStat(before, x, y, w, h)
    let b = regionStat(after, x, y, w, h)
    let dr = a.mean.0 - b.mean.0, dg = a.mean.1 - b.mean.1, db = a.mean.2 - b.mean.2
    let meanDelta = (dr * dr + dg * dg + db * db).squareRoot()
    let verdict = m.changedPct >= minChangedPct ? "MOVED" : "STILL"
    print(String(format: "%@ changed_pct=%.2f minChangedPct=%.2f tol=%.1f changed=%d n=%d mean_px_dist=%.1f max_px_dist=%.1f mean_delta=%.2f before_var=%.1f after_var=%.1f",
                 verdict, m.changedPct, minChangedPct, tol, m.changed, m.count,
                 m.meanDistance, m.maxDistance, meanDelta, a.variance, b.variance))
    exit(m.changedPct >= minChangedPct ? 0 : 2)
case "step":
    guard args.count == 10 || args.count == 11,
          let rawX = Int(args[3]), let rawY = Int(args[4]),
          let len = Int(args[5]), let thick = Int(args[6]),
          let expected = Int(args[8]), let tol = Int(args[9]),
          ["right", "left", "down", "up"].contains(args[7]) else {
        fail("usage: pixcmp step <png> <x> <y> <len> <thick> <right|left|down|up> <expected> <tol> [colourTol]")
    }
    // 12 is the same euclidean RGB distance `moved` treats as noise.
    let colourTol = args.count == 11 ? (Double(args[10]) ?? 12.0) : 12.0
    let vertical = args[7] == "down" || args[7] == "up"
    let sign = (args[7] == "left" || args[7] == "up") ? -1 : 1
    let (bytes, width, height) = rgbaBytes(loadImage(args[2]))
    // Negative means "from the far edge", so a scan up from the bottom needs no
    // knowledge of the capture's height.
    let x = rawX < 0 ? width + rawX : rawX
    let y = rawY < 0 ? height + rawY : rawY
    guard let ref = stripMean(bytes, width, height, x, y, thick, vertical) else {
        fail("scan start \(x),\(y) outside image \(width)x\(height)")
    }
    var step = -1
    var profile: [String] = []
    for offset in 0..<len {
        let sx = vertical ? x : x + sign * offset
        let sy = vertical ? y + sign * offset : y
        guard let m = stripMean(bytes, width, height, sx, sy, thick, vertical) else { break }
        let dr = m.0 - ref.0, dg = m.1 - ref.1, db = m.2 - ref.2
        let d = (dr * dr + dg * dg + db * db).squareRoot()
        profile.append(String(format: "%d:%.0f,%.0f,%.0f(%.1f)", offset, m.0, m.1, m.2, d))
        if step < 0 && d > colourTol {
            step = offset
        }
    }
    // A negative expected asks for no step at all, which is how "this strip is
    // one colour all the way across" is stated.
    let ok = expected < 0 ? step < 0 : (step >= 0 && abs(step - expected) <= tol)
    print(String(format: "%@ step=%d expected=%d tol=%d colourTol=%.1f ref=%.0f,%.0f,%.0f profile=%@",
                 ok ? "STEP" : (step < 0 ? "NOSTEP" : "OFFSTEP"), step, expected, tol, colourTol,
                 ref.0, ref.1, ref.2, profile.joined(separator: " ")))
    exit(ok ? 0 : 2)
default:
    fail("unknown command '\(args[1])'")
}
