// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Pixel regression for a 560x500 window at (0,40), with a 220pt floating
// sidebar. Examine every intermediate frame, not just the final capture.
// Usage: swift floating-panel-corners.swift page-alone.png burst-dir left|right
import AppKit

func bitmap(_ path: String) -> NSBitmapImageRep {
  guard let data = FileManager.default.contents(atPath: path),
        let image = NSBitmapImageRep(data: data) else {
    fatalError("Cannot read \(path)")
  }
  return image
}

func distance(_ a: NSBitmapImageRep, _ b: NSBitmapImageRep,
              _ x: Int, _ y: Int) -> Double {
  let ca = a.colorAt(x: x, y: y)!.usingColorSpace(.deviceRGB)!
  let cb = b.colorAt(x: x, y: y)!.usingColorSpace(.deviceRGB)!
  return max(abs(ca.redComponent - cb.redComponent),
             abs(ca.greenComponent - cb.greenComponent),
             abs(ca.blueComponent - cb.blueComponent)) * 255
}

guard CommandLine.arguments.count == 4 else {
  fatalError("Expected page-alone.png burst-dir left|right")
}
let control = bitmap(CommandLine.arguments[1])
let directory = CommandLine.arguments[2]
let right = CommandLine.arguments[3] == "right"
let paths = try FileManager.default.contentsOfDirectory(atPath: directory)
  .filter { $0.hasPrefix("frame-") && $0.hasSuffix(".png") }.sorted()
var sampled = 0
var failures = 0
for path in paths {
  let frame = bitmap(directory + "/" + path)
  // A flat band below the sidebar rows identifies the moving edge. Restrict
  // the measurement to the panel span so desktop and other windows cannot
  // satisfy it. Ignore absent/settled frames; require real intermediate ones.
  let changed = (5..<225).filter {
    distance(frame, control, right ? 559 - $0 : $0, 480) > 60
  }
  guard let edge = changed.last, edge > 45, edge < 195 else { continue }
  sampled += 1
  // Two points below the panel top, two points inside its leading edge is
  // outside its 19pt rounded corner, but inside a square frost rectangle.
  for (label, x, y) in [("leading-top", edge - 2, 47),
                         ("leading-bottom", edge - 2, 532),
                         ("dock-top", 6, 50),
                         ("dock-bottom", 6, 529)] {
    let delta = distance(frame, control, right ? 559 - x : x, y)
    if delta > 35 {
      print("FAIL \(path) \(label) edge=\(edge) delta=\(Int(delta))")
      failures += 1
    }
  }
}
print("\(sampled) intermediate frames, \(failures) exposed corners")
exit(sampled > 0 && failures == 0 ? 0 : 1)
