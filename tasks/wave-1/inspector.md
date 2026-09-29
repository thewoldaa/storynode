# Inspector

The property panel for the selected node.

## Territory

- src/ui/assets/inspector/**

## Deliverables

The panel currently renders a fixed pair of fields for every node type. This
task makes it driven by what a node type actually declares.

1. **A node type registry.** Each type declares its properties: a key, a
   label, a type, a default, and enough validation to say what a value must
   look like. The registry lives in this task's territory, because the
   inspector is its only consumer until the node-type work in wave 2.

2. **Field kinds.** Text, multi-line text, number, checkbox, choice, and an
   asset reference. Each renders appropriately and validates on commit.

3. **Validation feedback per field.** A property the document validator
   rejected is marked where it was entered, not only in the status bar.

4. **Multi-select editing.** When several nodes are selected, show the
   properties they share, and edit them together. A field whose values differ
   across the selection says so rather than showing one node's value.

5. **A node-type header.** The type, the id, and the node's validation state,
   so the panel says what is selected without the user looking back at the
   graph.

## Acceptance criteria

- [ ] `ctest` passes, including the layout checks.
- [ ] Each field kind round-trips a value through the host and back.
- [ ] A property the validator rejects is marked on the field.
- [ ] Editing a field sends one message when the edit is committed, not one per
      keystroke.
- [ ] Selecting two nodes with different values for a property shows the
      difference rather than silently picking one.

## Notes

The `setProperty` message already exists and needs no change: it carries one
key and one value, which is what a single edit is. A multi-node edit sends one
message per node, which is fine — the alternative is a message shape the host
does not have, and the cost of the change would land on a shared surface.
