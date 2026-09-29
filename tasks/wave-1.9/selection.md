# Wave 1.9 — One selection, owned by the shell

Fixes an integration gap that two parallel tasks created without either being
wrong on its own.

## The gap

`canvas` and `inspector` landed in the same wave. Each made a reasonable
decision, and the two decisions contradict.

**Canvas** keeps the multi-node selection private and tells the shell only an
anchor:

> The shell's selection is one id, because the inspector edits one node and a
> panel showing four nodes at once is a different panel. A canvas has to select
> several, so this area keeps the set and tells the shell which of them is the
> anchor.

**Inspector** reads a set from the shell and falls back to one id:

> Reads `selectedIds` when the shell has it and falls back to the single
> `selectedId`. Multi-select is the canvas task's, and it widens the shell from
> one id to a set; this panel is written against the set from the start so that
> the two do not have to be changed together.

Nothing writes `selectedIds`. The inspector's multi-select path is never
reached, so one of its acceptance criteria — "selecting two nodes with
different values for a property shows the difference rather than silently
picking one" — is not met, even though the code for it is written and correct.

## Why the canvas decision was wrong

Not because it was unreasonable. Because its premise was false: it assumed the
inspector edits one node, and the inspector task was chartered to build
multi-node editing. Neither task could see the other.

The deeper error is the classification. Canvas called the set "presentation,
like pan and zoom: the host never sees it". True of the host — the host has no
opinion about which nodes are highlighted. False of the shell: **two areas read
it**, and state read by two areas is shared state, not one area's presentation.

## Territory

- src/ui/assets/ui.html
- src/ui/assets/canvas/**
- src/ui/assets/inspector/**
- src/ui/assets/styles/**
- tests/**
- docs/**
- CHANGELOG.md

## Deliverables

1. **The shell owns the selection as a set.** `state.selectedIds` is an array
   in document order, and `state.selectedId` is the anchor derived from it —
   the node the inspector focuses and a keyboard step moves from. One is not
   a special case of the other; they answer different questions.

2. **One way to change it.** `shell.select(id)` for one, `shell.selectMany(ids,
   anchor)` for several. An area never assigns `state.selectedIds` directly,
   for the same reason it never assigns `state.document`: the other area would
   not be told.

3. **Canvas publishes what it selects.** Its rubber band calls `selectMany`
   rather than keeping a private set and sending an anchor.

4. **A test that would have caught this.** The layout check counts registered
   areas, which catches a script that fails to load. It does not catch two
   areas that load and disagree. This wave adds a check that drives the page
   the way a user does — select two nodes — and asserts the inspector shows
   both.

## Acceptance criteria

- [ ] `ctest` passes, including the layout checks.
- [ ] Rubber-banding two nodes makes the inspector show both.
- [ ] Editing a shared property with two nodes selected sends one message per
      changed node.
- [ ] The new test fails when `selectMany` is changed to send only an anchor.
      Verify by doing it.
- [ ] `canvas` and `inspector` each still work with one node selected.

## Notes

This is what parallel work costs. Two tasks that cannot see each other will
make contradictory assumptions about the surface between them, and neither
branch will fail — the contradiction only exists once both are merged.

The territory check cannot catch it, because the territories were genuinely
disjoint. What catches it is a test that exercises the seam, which is why
deliverable 4 is the important one.
