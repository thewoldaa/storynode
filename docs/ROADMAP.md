# Roadmap

Waves run in order. Tasks within a wave run in parallel. A wave is complete
when every task is merged into `dev` and `dev` is green; only then does the
next wave branch.

The rule that makes this work: **a wave's tasks must not share files.** If two
tasks need the same change, the change belongs in a `core` task in its own
wave that merges first. See [CONTRIBUTING.md](../CONTRIBUTING.md).

## Wave 0 — Core

Sequential. Everything depends on it.

| Task | Status | Deliverable |
| --- | --- | --- |
| `core` | **Complete** | Build system, document model, JSON schema, project I/O, application shell, CI |

## Wave 1.5 — Split the interface

Sequential, one task. **Complete.** Landed before wave 1, because wave 1 could
not start without it.

| Task | Status | Deliverable |
| --- | --- | --- |
| `ui-split` | **Complete** | The interface split into a shell and per-area directories, so two tasks can own two parts of it |

Wave 1 was planned as four parallel tasks, two of which work on the interface,
and the interface was one file. Two tasks editing one file is the conflict the
harness exists to prevent, so the split became its own wave rather than a
relaxation of the rule.

## Wave 1 — Editor surface

Parallel, two tasks. Both are interface areas with their own directory.

| Task | Status | Territory | Deliverable |
| --- | --- | --- | --- |
| `canvas` | Planned | `src/ui/assets/canvas/**` | Pan, zoom, node drag, edge routing, selection |
| `inspector` | Planned | `src/ui/assets/inspector/**` | Property panel driven by node-type schema |

## Wave 1.6 — The document session

Parallel with wave 1. Lands the surface the two tasks below both need.

| Task | Status | Deliverable |
| --- | --- | --- |
| `session` | **Complete** | `DocumentSession`, the undo stack, the undo/redo messages, and the saved-state marker |

### Why the original four became two plus two

Wave 1 was planned as four parallel tasks. `undo` and `project-io` could not be
two of them:

| Task | Declared territory | What it actually needed |
| --- | --- | --- |
| `undo` | `src/core/history/**` | plus `Bridge`, `ui.html`, `main.cpp` |
| `project-io` | `src/core/io/**` | plus `main.cpp`, `Bridge`, `ui.html` |

Both needed the host to own a document whose saved state is knowable, both
needed new bridge messages, and both needed the page to handle new keys. The
territory check would have **passed anyway**, because the declarations did not
describe what the work required.

A check that passes on a plan that cannot work is worse than no check. The
shared surface became `session`, which is what [CONTRIBUTING.md](../CONTRIBUTING.md)
says to do when two tasks need the same change.

## Wave 1.7 — Project I/O

Sequential, one task, after `session`.

| Task | Status | Territory | Deliverable |
| --- | --- | --- | --- |
| `project-io` | Planned | `src/core/io/**`, `src/app/**` | Recent files, save recovery, autosave, file association |

It owns `src/app/**` because it is the task that puts files on the user's disk,
so it owns the menu, the dialogs, the window title and drag-and-drop. That
overlap with `undo` is exactly why the two could not run together.

## Wave 1.8 — Interface measurement

Sequential, one task. Follows the canvas work, which needs a tool it has
nowhere to put.

| Task | Status | Territory | Deliverable |
| --- | --- | --- | --- |
| `measurement` | Planned | `src/tools/**`, `scripts/**`, `cmake/**` | A home for interface benchmarks, wired into CTest, with a measured baseline |

`src/ui/assets/` is for things that ship, and the build inlines every `.js` it
finds there. A tool that measures the interface is not one of those files, and
a benchmark that is not in the test suite stops being true. This wave settles
both.

## Wave 2 — Node types

Parallel. One task per node family plus the layout engine that arranges them.

| Task | Status | Territory | Deliverable |
| --- | --- | --- | --- |
| `nodes-dialog` | Planned | `src/ui/assets/nodes/dialog/**` | Dialogue node: speaker, lines, choices |
| `nodes-flow` | Planned | `src/ui/assets/nodes/flow/**` | Branch, jump, and end nodes |
| `nodes-media` | Planned | `src/ui/assets/nodes/media/**` | Image, audio, and video nodes |
| `layout` | Planned | `src/core/layout/**` | Auto-layout and graph validation (unreachable nodes, dead ends) |
| `assets` | Planned | `src/ui/assets/library/**` | Asset panel: import, preview, reference management |

## Wave 3 — Export

Parallel. Each exporter is independent; the preview player consumes the JSON
exporter's output and is written against its schema, not its code.

| Task | Status | Territory | Deliverable |
| --- | --- | --- | --- |
| `export-json` | Planned | `src/core/export/json/**` | Canonical story JSON |
| `export-webgal` | Planned | `src/core/export/webgal/**` | WebGAL script |
| `export-html` | Planned | `src/core/export/html/**` | Self-contained playable HTML |
| `preview` | Planned | `src/app/preview/**` | In-editor player running the exported JSON |

## Wave 4 — Polish and release

Sequential, mostly.

| Task | Status | Deliverable |
| --- | --- | --- |
| `polish` | Planned | Keyboard shortcuts, theming, empty states, error surfaces |
| `sample` | Planned | A sample project with three endings, used as the export test fixture |
| `docs` | Planned | User guide, node reference, export format reference |
| `release` | Planned | Versioning, installer, signed build, first tag on `main` |

## Versioning

`main` carries tags. `0.x` while the schema can still change; `1.0` when the
`.snproj` format is frozen and a file written by one version opens in the next.
