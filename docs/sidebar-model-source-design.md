# How the sidebar model reaches C++

One decision unblocks three pieces of work. They are the same work seen from three places.

**Recommendation: option C, a browser-process cache Rust owns.** Reasoning and
the two rejected options below.

## What happens today

`AtriumBrowserWindow::PushSidebarSnapshot()` builds the document and executes
`window.atriumSetSidebar(...)` in the sidebar's renderer. It is called from
seven places - the `sidebarReady` handshake and six mutation points in
`atrium_browser_controller.mm`. It is a PUSH, on the UI thread, and it is
synchronous end to end:

```
BuildSidebarTabsJSON(browser)
  -> UIBridge::CopySidebarModelJSON(host)     // dlsym into the Swift dylib
  -> MergeLiveSelection(model, browser)       // marks the live tab selected
```

Swift answers from memory. That is why it is fast enough to be synchronous, and
it is the property any replacement has to keep.

## The constraint that killed the obvious version

Reading `spaces.json` inside `BuildSidebarTabsJSON` is a **fatal DCHECK** -
`base::ReadFileToString` on the UI thread trips `tls_blocking_disallowed`. The
app died three seconds after launch, every launch. Measured on 2026-08-09 and
reverted the same night.

So the model has to be in memory by the time the push runs. The only question
is whose memory, and who keeps it fresh.

## Option A: read the file, off the UI thread

Post the read to a blocking pool, then push when it lands.

Rejected. Every push becomes asynchronous, so the seven call sites each need a
completion path, and a mutation followed immediately by a push races its own
write. It also keeps the debounce problem: the file lags memory by up to a
second, so a drag would render its old position.

## Option B: C++ holds the parsed model

C++ reads the store once at startup, keeps the parsed document, and hands it to
Rust for each snapshot.

Rejected, but only just. It works, and it is the smallest change. What it costs
is that the model then lives in C++ - which is what H1 exists to move out - so
the state would have to be moved again in H5. Two migrations for one thing.

## Option C: a cache in the Rust core

The core holds the loaded document in memory and owns its lifetime:

```c
// Loads once and keeps it. Idempotent; safe to call from anywhere.
bool atrium_core_open(const char* spaces_path, double migration_instant);

// The sidebar model from what is already in memory. No IO. Safe on the UI
// thread, which is the whole point.
char* atrium_core_sidebar_model_cached(int32_t active_space_index,
                                       const char* live_tabs_json);
```

`atrium_core_open` is called once, off the UI thread, during startup - the same
place profile bootstrap already does blocking work.
`atrium_core_sidebar_model_cached` is a pure in-memory build, so
`PushSidebarSnapshot` stays synchronous and every call site is unchanged.

It also lands H1.4 for free. Once the core owns the document, it is the writer:
mutations go to Rust, Rust writes the file through the path that already exists
in `persist.rs`, and the debounce lives there rather than in Swift.

## What C still needs answering

**Threading.** The cache is touched from the UI thread on every push and from
whatever thread mutations arrive on. Either the ABI takes a lock inside the
core, or C++ promises single-threaded access. The lock is safer and the cost is
nothing next to building the JSON.

**Multi-window.** `active_space_index` is per window slot. Today Swift resolves
host to slot to space. After the cut nothing does, so either C++ keeps that
mapping - it already knows its own slot - or the core does. C++ keeping it is
less to move.

**Failure.** A store the build refuses to read must not render an empty
sidebar; the flat tab list already exists as the fallback and
`ChooseSidebarModelSource` already names that case.

## What is already built

- `atrium_core_sidebar_model` - the stateless version, tested both sides of the
  ABI. Option C is this plus a cache.
- `ChooseSidebarModelSource` and `SpacesFilePath` - the decision and the path
  resolution, six tests.
- `persist.rs` - atomic write at 0600, `.bak`, the one-shot pre-Rust copy, byte
  comparison against what Swift writes.
- **The cache itself** - `rust/atrium-core/src/cache.rs` and four ABI calls at
  version 3: `atrium_core_open`, `atrium_core_close`, `atrium_core_is_open`,
  `atrium_core_sidebar_model_cached`, plus `atrium_core_active_space_index` for
  seeding a window's slot.

The three questions above are answered as this section recommended: the lock
lives in the core, C++ keeps the window-to-slot mapping (a negative index means
"the document decides"), and a refused store leaves the cache shut so the
caller falls back to the flat tab list.

The load-bearing property is that a snapshot after the file is DELETED still
returns the same model. That test exists on both sides of the ABI, and it was
control-tested by putting the read back - it failed, alone, and named itself.

## What is still missing

C++ calls none of this yet. `PushSidebarSnapshot` still asks Swift, because
the cache holds what the FILE said and mutations still land in Swift's memory,
which the file lags by up to a second. So a consumer switched over today would
render the pre-drag order on every drag.

The cache is the place that state moves to; it is not the move. That is H5.
