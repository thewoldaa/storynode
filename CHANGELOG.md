# Changelog

Notable changes, newest first. Updated at the end of each wave.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

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
