# Release notes

One entry per release, newest first. Add an entry as
part of `build/scripts/update-chromium.sh finish <newtag>` before tagging the release -
that command already bumps `build/VERSION`'s `chromium_tag`, so copy the value
straight from there.

## Entry template

```
## <Atrium version> - <date, YYYY-MM-DD>

Upstream Chromium: <chromium_tag from build/VERSION>

Security fixes included (from the Chromium release notes / CVE list for this tag):
- CVE-XXXX-XXXXX: <one-line description>
- ...

Atrium changes:
- ...
```

No entries yet - this file was created ahead of the first tagged release. The first
`update-chromium.sh finish` after this point should add one and remove this line.
