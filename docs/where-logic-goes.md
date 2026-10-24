# Where a new behaviour goes

Three layers can hold a decision. Pick by how fast the decision can be tested,
measured on this machine:

| Layer | Test loop | Owns |
|---|---|---|
| The page (`chrome/browser/resources/atrium`, `node --test`) | 0.2 s | Layout, gesture feel, what a control shows |
| The Rust core (`rust/atrium-core`, `cargo test`) | 10 s | Every rule: which row is drawn, what a drag commits, which width opens, what a chord means |
| The C++ window (`chrome/browser/ui/atrium`, a rebuild plus a VM run) | 25 s to rebuild one file, 2 to 4 min to see it | Windows, views, AppKit, the compositor, event delivery |

The rule: if it is a RULE, it does not go in the window. A decision the core
can make from its inputs is tested in ten seconds by a test that cannot flake.
The same decision in the window is tested in minutes, through a harness that
drops chords and lands drags short.

## What stays in C++

Geometry and event delivery. Moving those buys nothing and costs an ABI round
trip per event. The window reads a decision from the core and applies it to a
view.

## The surface-message router

Every `chrome.send` from a page reaches the window as a name and an argument
list. The names are data: `SurfaceMessageRouter` maps each to one handler, and
`AtriumBrowserWindow::SurfaceRoutes()` is the table. A new message is one row in
that table and one handler, never a new arm in a chain.
