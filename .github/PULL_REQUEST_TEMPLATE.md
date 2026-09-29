# ---------------------------------------------------------------------------
# Pull request template.
#
# The harness fills this in automatically when it opens a PR; it exists so a
# PR opened by hand gets the same shape.
# ---------------------------------------------------------------------------

## Task

`tasks/wave-<wave>/<task>.md`

## What changed

<!-- One paragraph. What the task delivers, not a file list. -->

## Acceptance criteria

<!-- Copy the checklist from the task declaration and tick what is done. -->

- [ ]

## Checks

- [ ] `ctest` passes locally
- [ ] Changes stay inside the task's declared territory
- [ ] No new compiler warnings
- [ ] `CHANGELOG.md` updated if this is the last task in its wave

## Notes for the reviewer

<!-- Anything that is deliberately unfinished, any decision worth a second
     opinion, any place the task declaration and the code disagree. -->
