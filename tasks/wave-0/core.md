# Wave 0 — Core

The foundation. Everything else depends on this, so it is a single task in its
own wave that merges before any parallel work starts.

## Territory

- CMakeLists.txt
- CMakePresets.json
- .github/**
- src/core/**
- src/app/**
- src/platform/**
- src/ui/**
- src/CMakeLists.txt
- tools/**
- tests/**
- docs/**
- CHANGELOG.md
- README.md
- CONTRIBUTING.md
- scripts/**

## Deliverables

1. **Build system.** Root `CMakeLists.txt` plus `CMakePresets.json`. Presets
   for `default` (Debug) and `release`. `STORYNODE_BUILD_TESTS` option.

2. **Document model.** `Story`, `Node`, `Port`, `Edge` as plain C++ types with
   no dependencies on Win32 or WebView2, so they are unit-testable and
   reusable by the exporters.

3. **JSON schema.** `docs/schema/story.schema.json` describing the `.snproj`
   format, plus a validator that reports every problem in a document rather
   than the first one.

4. **Project I/O.** Serialise and deserialise `.snproj`. Unknown keys survive
   a round trip so a file written by a newer version is not silently
   stripped.

5. **Application shell.** Win32 window, WebView2 host, message bridge. The
   interface is HTML embedded as a resource and loaded with
   `NavigateToString`, following the pattern in `docs/UI-ENGINE.md`.

6. **CI.** Build and test on `windows-latest` for every pull request, plus the
   forbidden-file check as a separate required job.

## Acceptance criteria

- [ ] `cmake --preset default && cmake --build --preset default` succeeds on a
      clean checkout after `scripts/fetch-deps.ps1`.
- [ ] `ctest` passes, with tests covering: model construction, JSON round
      trip, schema validation rejecting each documented error case, and
      unknown-key preservation.
- [ ] The application launches, shows the HTML interface, and the JS side
      receives a `ready` handshake from C++.
- [ ] A message sent from JS reaches C++ and a reply reaches JS, proven by a
      test that drives the bridge directly without a window.
- [ ] CI is green on the pull request.

## Notes

The root `CMakeLists.txt` globs sources with `CONFIGURE_DEPENDS` rather than
listing them. That is deliberate: with one worktree per task, an explicit
source list is a file every task must edit, which makes every task conflict
with every other task. Adding a `.cpp` file inside a task's own territory must
not require touching a shared file.

## Out of scope

The canvas, the inspector, node behaviour, undo/redo, and every exporter.
Those are waves 1 to 3. This task ships a shell that loads, validates, saves
and displays a document, and nothing more.
