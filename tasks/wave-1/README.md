# Wave 1 — Editor surface

Parallel. Every task here branches from a `dev` that already contains wave 0,
and no two tasks write the same file.

## Tasks

| Task | Territory | Deliverable |
| --- | --- | --- |
| `canvas` | `src/ui/assets/canvas/**` | Node graph: pan, zoom, node drag, edge routing, selection rectangle |
| `inspector` | `src/ui/assets/inspector/**` | Property panel for the selected node, driven by the node type's schema |
| `project-io` | `src/core/io/**` | Open/save dialogs, recent files, dirty tracking, save-on-close prompt |
| `undo` | `src/core/history/**` | Command stack with coalescing, undo/redo, and a bounded depth |

## Why these four

They are the four surfaces a user touches constantly, and each owns a distinct
directory. `canvas` and `inspector` are both UI but never touch the same file:
the canvas owns the graph area, the inspector owns the side panel, and they
communicate through the document model rather than by calling each other.

`project-io` and `undo` are both core but likewise disjoint. `undo` records
commands against the model; `project-io` serialises the model. Neither needs
to know the other exists.

## Shared surfaces, and why they are not in this wave

The document model, the message bridge protocol, and the root `CMakeLists.txt`
are shared. They changed in wave 0 and are frozen for the duration of wave 1.
A task that needs a change to a shared surface must say so in its task file
and wait for a `core` task in wave 1.5 rather than editing it, because an edit
to a shared file from inside a parallel wave is exactly the conflict the
harness exists to prevent.

If two tasks turn out to need the same change, that is a signal the change
belongs in `core`, not that the territory rules should be relaxed.

## Merge order

All four merge into `dev` independently. Order does not matter because their
territories are disjoint; whoever is ready first merges first. Wave 2 does not
start until all four are in.
