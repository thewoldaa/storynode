# Wave 1.7 — Project I/O

Lands alone, after `session`. It was planned as a wave 1 task alongside `undo`,
which was wrong: both needed the same shared surface, and the territory check
would not have caught it because the declarations did not describe what the
work required.

## Territory

- src/core/io/**
- src/app/**
- src/ui/assets/ui.html
- src/ui/assets/styles/**
- docs/**
- CHANGELOG.md

## Why `src/app/**` is in scope here

`project-io` is the task that puts files on the user's disk, so it is the task
that owns the menu, the dialogs, the window title, and the drag-and-drop
handler. Those all live in `src/app/`, and there is no way to deliver this task
without them.

This is exactly the overlap that made `undo` and `project-io` impossible to run
in parallel. Landing them in sequence is the fix; widening both declarations
and running them together would have been the mistake.

## Deliverables

1. **Recent files.** A list, persisted per user, with the missing-file case
   handled by removing the entry rather than failing to open. A recent-files
   menu that offers a file which no longer exists is worse than no menu.

2. **A save that reports what went wrong.** `SaveToFile` is already atomic and
   already says what the system said. This task makes that visible: a failure
   names the file and the reason, and offers to save somewhere else rather than
   dropping the user back into the editor with their work unsaved and no
   explanation.

3. **A backup of the previous version.** One generation, beside the file. The
   atomic save protects against a crash mid-write; a backup protects against
   the user saving over the wrong thing, which is the more common disaster.

4. **Autosave and recovery.** A periodic save to a recovery file, and a prompt
   to recover it after an unclean exit. The recovery file is separate from the
   document: an autosave that overwrites the user's file is worse than no
   autosave. A clean exit discards the recovery file.

5. **File association and drag-and-drop.** Opening a `.snproj` by double
   clicking it in Explorer, and by dropping it on the window. Both go through
   the same open path, including the unsaved-changes prompt.

6. **The dirty marker comes from the session.** `project-io` does not track
   dirtiness; it asks `DocumentSession::IsDirty()` and calls `MarkSaved()`.
   That is the whole reason `session` landed first.

## Acceptance criteria

- [ ] `ctest` passes.
- [ ] A file saved, modified, undone back to its saved state, reports clean.
- [ ] A save that fails leaves the previous file intact, says why, and lets the
      user pick another path without losing the document.
- [ ] A recovery file is offered after an unclean exit and discarded after a
      clean one.
- [ ] Opening a file with a newer format version refuses it without touching
      the open document.
- [ ] Double-clicking a `.snproj` opens it, and prompts about unsaved changes
      first.

## Notes

`src/core/ProjectIO.cpp` is a shared surface and is NOT in this territory. It
was fixed in wave 1.5 and is frozen. If this task needs a change there, say so
in the task file and it becomes a `core` task — do not edit it.
