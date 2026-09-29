# Undo

A command stack with coalescing, undo and redo.

## Territory

- src/core/history/**

## Deliverables

1. **A command stack.** Each edit is a value that can be applied and reverted.
   The document model is plain data, so a command is a record of what changed
   rather than a closure that mutates in place — which is what makes an undo
   after a save, or after an undo, possible at all.

2. **Coalescing.** A drag that moves one node is one undo step, not two
   hundred. The rule is time and target: consecutive moves of the same node
   within a short window merge. A pause, or a different node, starts a new
   step.

3. **A bounded depth.** A cap on the number of steps, with the oldest dropped.
   An unbounded stack is a slow memory leak in a long session.

4. **Undo and redo through the bridge.** `undo` and `redo` messages, and the
   interface's Ctrl+Z and Ctrl+Y. The menu items exist and are currently
   inert.

5. **The dirty flag follows the stack.** Undoing back to the last save reports
   clean. That is the property people actually notice, and it is the reason
   this task and `project-io` have to agree on what "saved state" means —
   agree through the document, not by calling each other.

## Acceptance criteria

- [ ] `ctest` passes.
- [ ] Moving a node, then undoing, restores the previous position.
- [ ] A drag is one undo step.
- [ ] Undoing past the save point reports the document clean.
- [ ] The stack stops at its cap without growing.

## Notes

The undo stack records what the bridge applied, so it lives next to the
document rather than inside the interface. The interface sends intent and the
host records it; an undo stack in the page would be lost on a reload and would
disagree with the host about what happened.
