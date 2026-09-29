# Canvas

The node graph: pan, zoom, node drag, edge routing, and a selection rectangle.

## Territory

- src/ui/assets/canvas/**

## Deliverables

The canvas area already exists in a first form. This task takes it from
"renders and drags" to "usable on a real story".

1. **Rubber-band selection.** Drag on empty space to select several nodes, and
   move them together. The shell's selection is currently one id; widening it
   to a set is this task's job, and it is why the shell exposes `select` rather
   than letting an area set the selected id directly.

2. **Edge routing that survives a busy graph.** The current curve is a single
   cubic. With several edges between the same pair of nodes, or a node with
   many ports, they overlap. Offset parallel edges and pick the port side from
   the direction of travel.

3. **Connection dragging.** Press a port and drag to another port to create an
   edge, with a preview line, and a refusal the user can see when the target is
   not a valid one. The `connect` message already exists; this is the gesture.

4. **Keyboard navigation.** Arrow keys move the selection, Tab moves to the
   next node, Enter focuses the inspector. A graph editor that needs the mouse
   for everything is slow to use.

5. **Performance.** A story with 500 nodes must pan and zoom without dropping
   frames. The current render rebuilds every node element on every frame; this
   task makes that a measured decision rather than an assumption.

## Acceptance criteria

- [ ] `ctest` passes, including the layout checks.
- [ ] Selecting several nodes and dragging moves all of them in one message.
- [ ] Two edges between the same pair of nodes are visually distinguishable.
- [ ] Dragging from an output to an input creates an edge; dragging to an
      invalid target shows why and creates nothing.
- [ ] A 500-node story pans at a steady frame rate, measured, with the number
      recorded in the pull request.

## Notes

Everything this task needs is inside `src/ui/assets/canvas/`. If it turns out
to need a change to the shell — `ui.html` — that change belongs in a `core`
task in its own wave, not here. The shell is deliberately small so that this
does not come up often.
