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

## Wave 1 — Editor surface

Parallel. Four disjoint surfaces.

| Task | Status | Territory | Deliverable |
| --- | --- | --- | --- |
| `canvas` | Planned | `src/ui/assets/canvas/**` | Pan, zoom, node drag, edge routing, selection |
| `inspector` | Planned | `src/ui/assets/inspector/**` | Property panel driven by node-type schema |
| `project-io` | Planned | `src/core/io/**` | File dialogs, recent files, dirty tracking |
| `undo` | Planned | `src/core/history/**` | Command stack, coalescing, bounded depth |

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
