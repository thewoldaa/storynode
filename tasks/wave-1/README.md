# Wave 1 — Editor surface

Parallel. Two tasks, both interface areas with their own directory.

| Task | Territory | Deliverable |
| --- | --- | --- |
| `canvas` | `src/ui/assets/canvas/**` | Pan, zoom, node drag, edge routing, selection |
| `inspector` | `src/ui/assets/inspector/**` | Property panel driven by the node type's schema |

## Why only two

Wave 1 was planned as four. `undo` and `project-io` could not be two of them:
both needed the host to own a document whose saved state is knowable, both
needed new bridge messages, and both needed the page to handle new keys — three
shared files.

Worse, the territory check would have **passed**, because the declarations did
not describe what the work required. A check that passes on a plan that cannot
work is worse than no check, so the shared surface became its own wave.

`session` (wave 1.6) lands it, and `project-io` (wave 1.7) follows alone.

## Running them

```powershell
scripts/harness/wave-new.ps1 -Wave 1 -Task canvas
scripts/harness/wave-new.ps1 -Wave 1 -Task inspector
```

Both branch from a `dev` that contains the interface split, and neither touches
a file the other owns. `session` runs alongside them; its territory
(`src/app/**`, `ui.html`, `src/core/session/**`, `src/core/history/**`) does not
overlap either.

Each worktree needs its own WebView2 SDK — `third_party/` is git-ignored, so a
fresh worktree has none. `scripts/fetch-deps.ps1` fetches it, or copy it from
an existing checkout.
