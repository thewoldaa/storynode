# Wave 1.5 — Split the interface

Between wave 1's four parallel tasks and wave 2's, because those tasks cannot
start until the interface is split and the split is a change to a shared file.

## Why this wave exists

Wave 1 plans four tasks running in parallel, two of which — `canvas` and
`inspector` — work on the interface. But the interface is currently one file,
`src/ui/assets/ui.html`, holding the stylesheet, the markup and all the script
for both areas.

Two tasks editing one file is exactly the conflict the worktree harness exists
to prevent, and the territory check would refuse to start them. The fix is not
to relax the check; it is to make the file not shared.

This wave does that, and does it as a single task because the work touches the
shared file from end to end. After it merges, `canvas/` and `inspector/` are
directories that a task can own outright.

## Territory

- src/ui/assets/**
- src/app/UiAssets.cpp
- src/app/UiAssets.h
- cmake/GenerateUiResources.cmake
- CMakeLists.txt
- src/CMakeLists.txt
- src/tools/**
- scripts/harness/**
- tasks/wave-1/**
- docs/**
- CHANGELOG.md

## Deliverables

1. **A shell page and per-area assets.** `ui.html` becomes the page skeleton:
   the markup, and the script that owns the message protocol, the document
   state and the render loop. The canvas and the inspector each move into
   their own directory with their own stylesheet and script.

2. **A registration seam.** An area registers itself with the shell and is
   called back to render. The shell never calls into an area by name and an
   area never reads another area's state, so the two are independent enough to
   be written in parallel.

3. **Asset embedding by glob.** The build discovers the assets, embeds each as
   a resource, and generates the list the host inlines them from. No
   hand-written list, because a hand-written list is a shared file every task
   must edit — the same problem one level down.

4. **A resource probe.** A small tool that reports whether each embedded asset
   can be found by name, because a resource that is in the binary and not
   findable is otherwise invisible.

5. **Wave 1 task declarations**, so the four parallel tasks can start.

## Acceptance criteria

- [ ] `cmake --preset default && cmake --build --preset default` succeeds.
- [ ] `ctest` passes, including the layout checks at four window sizes.
- [ ] Adding a `.js` file under `src/ui/assets/canvas/` makes it load, with no
      edit to any file outside `canvas/`.
- [ ] `src/ui/assets/canvas/` and `src/ui/assets/inspector/` share no file, so
      the territory check accepts two tasks owning them.
- [ ] `probe_resources` reports every asset as found.
- [ ] The interface behaves as it did before the split: nodes render, drag,
      select, and the inspector edits properties.

## Notes

The split is not tidiness. It is the precondition for wave 1 running in
parallel at all, and the alternative — four tasks queueing behind one file — is
the serialisation the harness exists to avoid.

## Out of scope

Any behaviour change. This wave moves code and changes nothing about what it
does. A behaviour change mixed into a move makes both harder to review, and the
move is the part that has to be exactly right.
