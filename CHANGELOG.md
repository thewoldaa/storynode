# Changelog

Notable changes, newest first. Updated at the end of each wave.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Fixed

**Wave 1.5 — the interface split, and defects found by review**

- `SaveToFile` no longer deletes the original before replacing it. The old
  order left a window in which neither file existed, and a failed rename
  destroyed both. Windows now uses `ReplaceFileW`, which replaces a file's
  contents in one step.
- A key this version does not recognise inside an edge's endpoint is preserved
  instead of being deleted on the first save. Every other level of the document
  already did this; the endpoint level did not.
- Two undefined double-to-integer conversions, both from a range guard written
  on the wrong side of the cast.
- A file declaring format version 4294967297 was truncated to 1, passed the
  "not newer than this build" check, and was saved back as version 1.
- `NextId` counted in `int`, so an id near the integer limit could overflow and
  produce a duplicate id on the next call.
- A non-integer format version such as 1.9 was truncated and accepted.

### Changed

**Wave 1.5 — the interface split**

- The interface is no longer one file. `ui.html` is the shell: the markup, the
  message protocol, the document state and the render loop. The canvas and the
  inspector each live in their own directory with their own stylesheet and
  script, and register themselves with the shell.
- Interface assets are discovered by the build and embedded as resources, so
  adding a file under `canvas/` loads it with no edit outside `canvas/`.
- `probe_resources` reports whether each embedded asset can be found by name,
  because a resource that is in the binary and cannot be found is otherwise
  invisible.

### Added

**Wave 1.5**

- Wave 1 task declarations for `canvas`, `inspector`, `project-io` and `undo`,
  with territories that the harness confirms are disjoint.

**Wave 0 — core**

- CMake build system with presets for development, release and CI. Sources are
  globbed rather than listed, so adding a file inside a task's own territory
  never requires editing a shared file.
- A JSON value tree with parse and serialise. Object key order is preserved,
  unknown keys survive a round trip, and parse errors carry a line and column.
- The document model: `Story`, `Node`, `Port`, `Edge`, with structural
  validation that reports every problem rather than the first, and graph
  validation that warns about unreachable nodes and dead ends.
- The `.snproj` file format, with atomic saves and a refusal to open a file
  written by a newer format version.
- The message bridge between the page and the document, testable without a
  window.
- A Win32 application hosting WebView2, with the interface embedded in the
  executable and loaded without a temporary file or a local server.
- An HTML/CSS/JavaScript interface: node canvas with pan, zoom and drag, an
  inspector, and a status bar showing validation problems.
- `--verify`, which loads the interface, has the page measure its own layout,
  and fails if a band of the interface is missing, collapsed or overlapping.
- CI: build, test and CMake formatting on every pull request, on Windows and
  Linux respectively.
- 136 unit tests and six CTest tests covering the interface layout at four
  window sizes.

**Repository**

- MIT license, contributing guide, ignore and attribute rules, and a checker
  that refuses to commit files which must not be public.
- Git worktree harness for parallel tasks: `wave-new`, `wave-list`,
  `wave-sync`, `wave-done`, `wave-clean`, in both PowerShell and Bash, with
  territory overlap detection that runs before a worktree is created.

### Notes

The interface is HTML rendered by WebView2 rather than drawn with native
controls, and the reasoning is recorded in [docs/UI-ENGINE.md](docs/UI-ENGINE.md)
along with the alternatives that were rejected.

[Unreleased]: https://github.com/thewoldaa/storynode/commits/dev
