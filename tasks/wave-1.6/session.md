# Wave 1.6 — The document session

Runs in parallel with wave 1's `canvas` and `inspector`. Lands the surface that
`undo` and `project-io` both need, so that neither has to touch a file the
other is touching.

## Why this wave exists

Wave 1 was planned as four parallel tasks. Two of them cannot be:

| Task | Declared territory | What it actually needs |
| --- | --- | --- |
| `undo` | `src/core/history/**` | plus `Bridge`, `ui.html`, `main.cpp` |
| `project-io` | `src/core/io/**` | plus `main.cpp`, `Bridge`, `ui.html` |

Both need the host to own a document whose saved state is knowable, both need
new bridge messages, and both need the page to handle new keys. That is three
shared files, and the territory check would have passed anyway — because the
declarations did not describe what the work required.

A check that passes on a plan that cannot work is worse than no check. So the
shared surface becomes a task of its own, which is what
[CONTRIBUTING.md](../../CONTRIBUTING.md) says to do when two tasks need the
same change.

## Territory

- src/core/session/**
- src/core/history/**
- src/app/**
- src/ui/assets/ui.html
- src/ui/assets/styles/**
- docs/**
- CHANGELOG.md

## Deliverables

1. **`DocumentSession`.** One object owning the document, the saved-state
   marker, and the undo stack. The host currently keeps a `Story` and a `bool`
   in two globals, which cannot answer "is this the same as what is on disk"
   after an undo — the question `project-io` needs answered and the property
   `undo` is judged on.

   ```cpp
   class DocumentSession
   {
   public:
       Story& Document();
       const Story& Document() const;

       /// Apply a command, recording it for undo.
       void Apply(Command command);

       bool CanUndo() const;
       bool CanRedo() const;
       void Undo();
       void Redo();

       /// True when the document differs from the last saved state.
       bool IsDirty() const;

       /// Called by whoever writes the file, after it is written.
       void MarkSaved();
   };
   ```

   The saved marker is a revision number, not a copy of the document. A copy
   would double the memory and make "is it dirty" a deep comparison on every
   frame.

2. **The command stack**, in `src/core/history/`. A command is a value that
   can be applied and reverted, not a closure that mutates in place — that is
   what makes undo work after a save, and after another undo.

   Coalescing is time-and-target based: consecutive moves of the same node
   within a short window merge into one step, so a drag is one undo, not two
   hundred.

   The depth is bounded. An unbounded stack in a long session is a slow leak.

3. **The bridge messages.** `undo`, `redo`, and a `history` message the host
   sends so the page can show whether undo and redo are available. The page
   must not guess: after a save, or after the stack is trimmed, only the host
   knows.

4. **The page's keyboard handling.** Ctrl+Z and Ctrl+Y, dispatched as
   messages. The shell owns this, because it is a key binding and not a
   canvas or inspector concern.

5. **The host uses the session.** `main.cpp` stops holding a `Story` and a
   `bool` and holds a `DocumentSession`.

## Acceptance criteria

- [ ] `ctest` passes, including the layout checks.
- [ ] Moving a node, then undoing, restores the previous position.
- [ ] A drag is one undo step, not one per mouse move.
- [ ] Undoing back to the saved state reports clean, and any further edit
      reports dirty again.
- [ ] Redo is unavailable after a new edit, and the page is told.
- [ ] The stack stops at its cap without growing.
- [ ] `canvas` and `inspector` still build against the new shell.

## Notes

`undo` and `project-io` as separate wave 1 tasks are superseded. The stack
lands here; `project-io` becomes wave 1.7 and lands alone, against a session
that already exists.

The page is told what it can do rather than deciding for itself. That is the
same principle as the rest of the bridge: the host owns the state, the page
renders it.
