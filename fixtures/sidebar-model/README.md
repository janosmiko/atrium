# Sidebar model snapshot

`five-profiles.json` is what `SidebarModelJSON` produces for a five-profile,
seven-space configuration. It is the contract between the model and the HTML
sidebar, pinned so a Rust producer can be compared against it before Swift is
deleted.

`SidebarModelSnapshotTests` builds the configuration through the store's own
mutations, writes it, reads it back, and compares. Refresh with
`ATRIUM_CORPUS_REGENERATE=1 make test`, then read the diff.

## What the configuration covers

Five profiles, seven spaces, a pinned row, a folder holding two tabs, a
renamed tab, favourites across three profiles, and a space named
`<script>alert(1)</script>` - which crosses as data, since `sidebar.js` writes
titles with `textContent`.

## Credentials

One tab URL carries `admin:hunter2@`. It is gone from the snapshot, because
`SpacesStore` redacts on write.

**`SidebarModelJSON` does not redact anything itself.** The test proves this:
it builds the same configuration WITHOUT saving, and the password is in the
output. Everything downstream of the sidebar model is protected only because
the state it reads has been through the store.

A Rust producer that builds this JSON from in-memory state that was never
persisted would leak the password. Redaction has to move with the model, or be
done again on this path.
