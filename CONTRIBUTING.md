# Contributing

## Branch model

```
main   releases only. Every commit here is tagged and shippable.
  |
dev    integration. Waves merge here in order. Never force-pushed.
  |
wave/<wave>/<task>   one branch per task, one worktree per branch.
manual                the human's worktree. Not a task; no PR required.
```

`main` is only advanced by merging `dev` when a wave set is complete and
tagged. `dev` is only advanced by merging task branches, and only when CI is
green.

## The worktree harness

Parallel work is isolated by git worktrees. One task, one branch, one
worktree, one build directory. Nothing is shared and nothing is edited in
place.

```powershell
scripts/harness/wave-new.ps1   -Wave 1 -Task canvas
scripts/harness/wave-list.ps1
scripts/harness/wave-sync.ps1  -Task canvas
scripts/harness/wave-done.ps1  -Task canvas
scripts/harness/wave-clean.ps1 -Task canvas
```

Bash equivalents live alongside them with the same names and `.sh`. Both sets
implement the same contract; use whichever your shell is.

| Command | What it does |
| --- | --- |
| `wave-new <wave> <task>` | Verifies the task declaration, creates `wave/<wave>/<task>` from the latest `dev`, adds a worktree at `.worktrees/<wave>-<task>`, and writes a build directory at `build/<wave>-<task>`. |
| `wave-list` | Every worktree with its branch, dirty/clean state, and commits ahead/behind `dev`. |
| `wave-sync <task>` | Rebases that task's worktree onto `dev`. Touches no other worktree. |
| `wave-done <task>` | Configures, builds and tests in that worktree, then pushes the branch and opens a PR into `dev`. |
| `wave-clean <task>` | After the PR is merged, removes the worktree and deletes the branch locally and remotely. |

## Territory rules

Every task declares the file territory it owns in `tasks/<wave>/<task>.md`:

```markdown
## Territory

- src/ui/assets/canvas/**
- src/ui/assets/styles/canvas.css
```

The rules that make parallel work safe:

1. **A task may only edit files inside its declared territory.** The harness
   does not enforce this at commit time — it is a discipline, and review
   catches violations. What the harness *does* enforce is that no two tasks in
   the same wave declare overlapping territory, because that is the failure
   that produces merge conflicts after hours of work.

2. **Shared files belong to a `core` task in their own wave.** The root
   `CMakeLists.txt`, the project schema, and the core headers are shared
   surfaces. They change only in a wave whose task is explicitly named `core`,
   and that wave merges before any parallel wave that depends on it.

3. **Waves run in order; tasks within a wave run in parallel.** Wave *n+1*
   branches from a `dev` that already contains all of wave *n*. Starting a
   parallel wave early is how you get a rebase you cannot resolve.

4. **One worktree, one build directory.** `build/<wave>-<task>` keeps object
   files and CMake caches separate so parallel builds never touch the same
   file.

### Two paths no task has to declare

**`tests/`** — every task that adds behaviour adds a test for it, and a task
that adds behaviour without a test is the thing the tests exist to prevent. So
the test tree is not a territory to be claimed; it is a consequence of doing
the work.

Requiring each declaration to list it produces a rule that is either restated
in every file or broken by every file, and a rule that is always broken stops
being read. It was widened after the fact twice before this was written down,
which is the signal that the rule itself was wrong.

**A task's own declaration** — `tasks/wave-N/task.md`. Requiring a task to list
its declaration inside its declaration is a circle.

Note what is *not* exempt. `src/ui/assets/**` and `src/app/**` stay claimed,
because two tasks writing the same stylesheet or the same window code is
exactly the conflict the check exists to catch. Only the test tree is a
consequence of the work rather than a place to work.

## Commits

[Conventional Commits](https://www.conventionalcommits.org/), scoped to the
area of the codebase:

```
feat(canvas): add node drag with grid snap
fix(io): reject .snproj with duplicate node ids
docs(roadmap): record wave 1 completion
refactor(core): move port validation into the model
test(core): cover undo coalescing of rapid edits
chore(ci): cache the WebView2 SDK download
```

Keep commits small. A commit should be reviewable on its own and should not
mix a refactor with a behaviour change.

## Pull requests

A PR into `dev` must:

- build clean with no new warnings
- pass `ctest`
- pass the forbidden-file check (`.github/workflows/forbidden-files.yml`)
- stay inside the task's declared territory
- have a description naming the task and the acceptance criteria it satisfies

CI runs all of these. A red PR is not merged, and is not merged "afterwards"
either.

## Forbidden files

Some files must never reach this repository. They are listed in
`.gitignore`, checked by `scripts/check-forbidden.sh` in a pre-commit hook,
and re-checked in CI. The pre-commit hook is installed by:

```bash
bash scripts/install-hooks.sh
```

Do this once after cloning. The hook is a convenience; CI is the gate that
actually holds, because a hook can be bypassed with `--no-verify` and CI
cannot.

The categories:

- **Agent instruction files** — `AGENTS.md`, `CLAUDE.md`, `.claude/`,
  `.agents/`, `.cursor/`. These hold local working notes and are not part of
  the project.
- **Local scratch** — `.worktrees/`, `agent-notes/`, `scratch/`, `personal/`.
- **Secrets** — `.env`, `*.local`, `*.key`, `*.pem`, `secrets/`. Nothing in
  this project needs a credential, so any match is a mistake.
- **Build output** — `build/`, `out/`, `.vs/`, `.idea/`,
  `CMakeUserPresets.json`.

## Daily rhythm

Work happens in two worktrees at once: `manual` for the human, and one per
active task for agents. At the end of a session:

```powershell
scripts/harness/wave-list.ps1
```

Then write `docs/devlog/YYYY-MM-DD.md` — what moved, what is blocked, what the
next wave needs. The devlog is public, so it describes the work and not the
machine, the person, or anything private.

`CHANGELOG.md` and `docs/ROADMAP.md` are updated at the end of every wave, in
the same commit that records the wave as complete.
