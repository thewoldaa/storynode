# Project I/O

Opening, saving, and knowing whether the document has unsaved changes.

## Territory

- src/core/io/**

## Deliverables

The application can open and save a file through a dialog. This task makes the
file handling something a person can live with.

1. **Recent files.** A list, persisted per user, with the missing-file case
   handled by removing the entry rather than failing to open.

2. **A save that survives interruption.** `SaveToFile` is already atomic; this
   task adds the recovery around it: a backup of the previous version, and a
   clear report when a save fails rather than a message box that says only
   "cannot replace".

3. **Dirty tracking as a first-class thing.** The host currently sets a flag
   when the bridge reports a change. That is right, but it cannot answer "is
   this the same as what is on disk" after an undo. This task owns that
   question.

4. **Autosave.** A periodic save to a recovery file, and a prompt to recover
   it after a crash. The recovery file is separate from the document: an
   autosave that overwrites the user's file is worse than no autosave.

5. **File association and drag-and-drop.** Opening a `.snproj` by double
   clicking it in Explorer, and by dropping it on the window.

## Acceptance criteria

- [ ] `ctest` passes.
- [ ] A file saved, modified, undone back to its saved state, reports clean.
- [ ] A save that fails leaves the previous file intact and says what failed.
- [ ] An autosave recovery file is offered after an unclean exit and discarded
      after a clean one.
- [ ] Opening a file with a newer format version refuses it without touching
      the open document.

## Notes

`src/core/ProjectIO.cpp` is a shared surface and is NOT in this territory. It
was fixed in wave 1.5 and is frozen. If this task needs a change there, say so
in the task file and it becomes a `core` task — do not edit it.
