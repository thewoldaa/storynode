// ---------------------------------------------------------------------------
// The inspector: the property panel for whatever is selected.
//
// Owns the side panel and nothing else. It reads the selection from the shell,
// renders the properties the selected nodes declare, and reports edits to the
// host. It never touches the canvas and never edits the document itself: an
// edit is one message when it is committed, and the host answers with a fresh
// snapshot which this panel renders again.
//
// The panel is driven by the node type registry below rather than by a fixed
// pair of fields. A type declares its properties — key, label, kind, default
// and whether a value is required — and the panel renders that declaration
// without knowing what a dialogue node is. A type the registry has never heard
// of still gets a panel: its properties are listed as raw values rather than
// hidden, because a panel that silently omits data cannot be trusted to show
// what a node contains.
//
// The registry lives here because the inspector is its only consumer. The
// host's model has no node-type declarations yet; the node-type work in wave 2
// puts them there, and this table becomes the page's cache of what the host
// declares rather than the only copy.
// ---------------------------------------------------------------------------

(function () {
  "use strict";

  var shell = null;
  var panel = null;
  var body = null;

  /// The cells on screen, in render order.
  ///
  /// Rebuilt by every render, so a commit writes to the nodes and the value
  /// the panel drew, rather than to whatever the DOM happens to hold now.
  var cells = [];

  // --- the node type registry ----------------------------------------------
  //
  // What each node type declares: the properties it has, what each is called,
  // what kind of value it holds, what it holds when unset, and whether it must
  // not be empty. Adding a node type is adding an entry here; adding a
  // property to a type is adding one line. Nothing else in this file changes,
  // which is the property that matters: the panel knows that a node has
  // properties, not what a dialogue node is.
  //
  // `kind` names an entry in KINDS below. `def` is what an unset property
  // shows, which is not the same as a value being written: an unset property
  // stays unset until the user changes it.

  var TYPES = {
    start: {
      name: "Start",
      properties: [
        { key: "label", label: "Label", kind: "text", def: "Start",
          hint: "Shown on the node in the graph." }
      ]
    },

    dialog: {
      name: "Dialogue",
      properties: [
        { key: "speaker", label: "Speaker", kind: "text", def: "",
          hint: "Empty for narration." },
        { key: "text", label: "Line", kind: "multiline", def: "", required: true,
          hint: "What is said, or the narration itself." },
        { key: "emotion", label: "Emotion", kind: "choice", def: "neutral",
          options: ["neutral", "happy", "sad", "angry", "afraid", "surprised"] }
      ]
    },

    branch: {
      name: "Branch",
      properties: [
        { key: "prompt", label: "Prompt", kind: "multiline", def: "",
          hint: "What the player is asked before choosing." },
        { key: "condition", label: "Condition", kind: "text", def: "",
          hint: "Empty always takes the first choice." }
      ]
    },

    choice: {
      name: "Choice",
      properties: [
        { key: "text", label: "Label", kind: "text", def: "", required: true },
        { key: "target", label: "Target", kind: "asset", def: "",
          hint: "Story this choice leads to, when it leaves this graph." },
        { key: "hidden", label: "Hidden until seen", kind: "checkbox", def: false }
      ]
    },

    jump: {
      name: "Jump",
      properties: [
        { key: "target", label: "Target", kind: "asset", def: "", required: true }
      ]
    },

    end: {
      name: "End",
      properties: [
        { key: "ending", label: "Ending", kind: "text", def: "",
          hint: "Which ending this is, for the export." },
        { key: "label", label: "Label", kind: "text", def: "" }
      ]
    }
  };

  /// The declaration a type with no entry gets. Empty rather than absent, so
  /// every caller can use it without checking, and the undeclared-property
  /// section below is what gives such a node a usable panel.
  var NO_DECLARATION = { name: "", properties: [] };

  /// One entry per kind of value.
  ///
  /// `control` is how the value is drawn and read back. A kind is deliberately
  /// dumb: it knows how to show one value and how to read one back, and
  /// nothing about what the property means. What a value must look like is the
  /// declaration's business, not the control's.
  var KINDS = {
    text:      { control: "text" },
    multiline: { control: "textarea" },
    number:    { control: "number" },
    checkbox:  { control: "checkbox" },
    choice:    { control: "select" },
    asset:     { control: "asset" }
  };

  // --- the declaration lookup ----------------------------------------------

  function declarationFor(type) {
    // hasOwnProperty rather than a plain lookup: a node type called
    // "constructor" or "toString" would otherwise find something on the
    // prototype and the panel would render nonsense for it.
    if (type && Object.prototype.hasOwnProperty.call(TYPES, type)) {
      return TYPES[type];
    }
    return NO_DECLARATION;
  }

  function kindOf(field) {
    return KINDS[field.kind] || KINDS.text;
  }

  // --- the selection -------------------------------------------------------

  /// The selected nodes, in the order the document lists them.
  ///
  /// Reads `selectedIds` when the shell has it and falls back to the single
  /// `selectedId`. Multi-select is the canvas task's, and it widens the shell
  /// from one id to a set; this panel is written against the set from the
  /// start so that the two do not have to be changed together.
  function selectedNodes(state) {
    var ids = [];
    if (state.selectedIds && state.selectedIds.length) {
      ids = state.selectedIds;
    } else if (state.selectedId) {
      ids = [state.selectedId];
    }

    var nodes = [];
    for (var i = 0; i < ids.length; i++) {
      var node = shell.findNode(ids[i]);
      if (node) { nodes.push(node); }
    }
    return nodes;
  }

  /// A value that changes exactly when the selection does, so that focus is
  /// restored after a re-render of the same selection and not after a change
  /// of selection. Restoring focus into a field of a node the user just
  /// clicked would take the keyboard away from the canvas.
  function selectionSignature(nodes) {
    var parts = [];
    for (var i = 0; i < nodes.length; i++) { parts.push(nodes[i].id); }
    return parts.join("\u001f");
  }

  function isSelected(subjectId, nodes) {
    if (!subjectId) { return false; }
    for (var i = 0; i < nodes.length; i++) {
      if (nodes[i].id === subjectId) { return true; }
    }
    return false;
  }

  // --- values --------------------------------------------------------------

  function dataKeys(node) {
    return (node.data && typeof node.data === "object") ? Object.keys(node.data) : [];
  }

  function hasValue(node, key) {
    return node.data && typeof node.data === "object" &&
           Object.prototype.hasOwnProperty.call(node.data, key);
  }

  /// True when two property values are the same as far as an edit is
  /// concerned. An absent property and a null one are the same thing here,
  /// because the document has no way to tell the user which they have.
  function sameValue(a, b) {
    var aAbsent = (a === null || a === undefined);
    var bAbsent = (b === null || b === undefined);
    if (aAbsent || bAbsent) { return aAbsent && bAbsent; }
    return a === b;
  }

  /// What an unset property shows.
  function defaultFor(field) {
    if (field.def !== undefined) { return field.def; }
    return kindOf(field).control === "checkbox" ? false : "";
  }

  /// A value the user can edit with a control.
  ///
  /// A structure has no one-line form, so it is shown as JSON and the field is
  /// read-only. Letting a commit through would replace an object with whatever
  /// the text happened to say, which is data loss dressed up as an edit.
  function editableValue(value) {
    return value === null || value === undefined ||
           typeof value === "string" || typeof value === "number" ||
           typeof value === "boolean";
  }

  function displayValue(value) {
    if (value === null || value === undefined) { return ""; }
    if (typeof value === "string") { return value; }
    if (typeof value === "number" || typeof value === "boolean") { return String(value); }
    return JSON.stringify(value);
  }

  // --- building a cell -----------------------------------------------------
  //
  // A cell is one property row: the declaration, the nodes it applies to, the
  // value shown, and whether that value is shared. It is built once per render
  // and kept, so the commit handler never has to work out which node a
  // keystroke was meant for.

  function buildCell(field, nodes, problems) {
    var cell = {
      field: field,
      nodes: nodes,
      mixed: false,
      readonly: false,
      value: null,
      text: "",
      problem: problemForField(field.key, nodes, problems),
      element: null,
      note: null
    };

    cell.value = hasValue(nodes[0], field.key) ? nodes[0].data[field.key] : defaultFor(field);

    for (var i = 1; i < nodes.length; i++) {
      var other = hasValue(nodes[i], field.key) ? nodes[i].data[field.key] : defaultFor(field);
      if (!sameValue(cell.value, other)) {
        // The selection disagrees about this property. The control shows that
        // rather than one node's value: showing one of several values makes an
        // edit that writes it to all of them look like no change at all.
        cell.mixed = true;
        break;
      }
    }

    cell.readonly = !editableValue(cell.value);
    if (kindOf(field).control === "checkbox" && typeof cell.value !== "boolean") {
      // A checkbox field holding something that is not a boolean would show an
      // unchecked box for a value that is set, and a commit would then write
      // false over it.
      cell.readonly = true;
    }

    cell.text = cell.mixed ? "" : displayValue(cell.value);
    return cell;
  }

  /// The properties every selected node declares, in the order the first
  /// node's type declares them.
  function sharedFields(nodes) {
    var declared = declarationFor(nodes[0].type).properties;
    var fields = [];

    for (var i = 0; i < declared.length; i++) {
      var shared = true;
      for (var n = 1; n < nodes.length; n++) {
        if (indexOfField(declarationFor(nodes[n].type).properties, declared[i].key) === -1) {
          shared = false;
          break;
        }
      }
      if (shared) { fields.push(declared[i]); }
    }
    return fields;
  }

  function indexOfField(fields, key) {
    for (var i = 0; i < fields.length; i++) {
      if (fields[i].key === key) { return i; }
    }
    return -1;
  }

  /// Properties the document holds that no declaration covers.
  ///
  /// Shown rather than hidden, for two reasons. A property written by a newer
  /// build is the user's data and this panel is where they would look for it.
  /// And a node type with no declaration at all would otherwise render as an
  /// empty panel, which reads as "this node has nothing" rather than as "this
  /// build does not know this type".
  function undeclaredKeys(nodes, declared) {
    var first = dataKeys(nodes[0]);
    var keys = [];

    for (var i = 0; i < first.length; i++) {
      var key = first[i];
      if (indexOfField(declared, key) !== -1) { continue; }

      var shared = true;
      for (var n = 1; n < nodes.length; n++) {
        if (dataKeys(nodes[n]).indexOf(key) === -1) { shared = false; break; }
      }
      if (shared) { keys.push(key); }
    }
    return keys;
  }

  /// A declaration for a property nothing declares.
  ///
  /// The kind is taken from the value, so that a boolean is a checkbox and a
  /// long string is a text area. That is the difference between an undeclared
  /// property being editable and being a wall of text in a one-line box.
  function describeUndeclared(key, value) {
    var kind = "text";
    if (typeof value === "boolean") {
      kind = "checkbox";
    } else if (typeof value === "number") {
      kind = "number";
    } else if (typeof value === "string" &&
               (value.indexOf("\n") !== -1 || value.length > 60)) {
      kind = "multiline";
    }
    return { key: key, label: key, kind: kind, def: null, undeclared: true };
  }

  // --- problems ------------------------------------------------------------

  /// The problem the validator reported for one property, if any.
  ///
  /// Matched on the subject id plus the key appearing as a word in the
  /// message. A problem carries a subject and a sentence, not a field name,
  /// and the sentence naming the property is the only thing that ties the two
  /// together.
  ///
  /// This is a heuristic and it can be wrong in both directions: a problem
  /// about the node rather than about a property is left to the header and to
  /// the list below, and a property whose name happens to be an ordinary word
  /// in the sentence would be marked for a problem that is not about it. It is
  /// still worth having, because the alternative is a message list with no
  /// indication of which of a dozen fields it is about. It goes away when the
  /// validator can name the property it rejected.
  function problemForField(key, nodes, problems) {
    // The key is escaped before it becomes a pattern. A declared key is a bare
    // identifier, but an undeclared one comes from the document, and a file
    // people edit by hand can hold a key with a bracket in it — which would
    // throw out of the render loop and leave the panel showing the last node.
    var needle = new RegExp("\\b" + escapeRegExp(key) + "\\b");
    for (var i = 0; i < problems.length; i++) {
      var problem = problems[i];
      if (!problem || !problem.message) { continue; }
      if (!isSelected(problem.subjectId, nodes)) { continue; }
      if (needle.test(problem.message)) { return problem; }
    }
    return null;
  }

  function escapeRegExp(text) {
    return String(text).replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  }

  function problemsForNodes(nodes, problems) {
    var relevant = [];
    for (var i = 0; i < problems.length; i++) {
      if (problems[i] && isSelected(problems[i].subjectId, nodes)) {
        relevant.push(problems[i]);
      }
    }
    return relevant;
  }

  function severityClass(problem) {
    return problem && problem.severity === "warning" ? "warning" : "error";
  }

  /// The validation state of the selection, as a word and a class.
  ///
  /// In the header because it answers "is what I selected alright" before the
  /// user has to go looking for the node in the graph, and because a problem
  /// the validator reported for the node as a whole has no property to sit
  /// next to.
  function stateChip(nodes, problems) {
    var errors = 0;
    var warnings = 0;

    for (var i = 0; i < problems.length; i++) {
      if (!problems[i] || !isSelected(problems[i].subjectId, nodes)) { continue; }
      if (problems[i].severity === "warning") { warnings++; } else { errors++; }
    }

    if (errors === 0 && warnings === 0) {
      return { cls: "ok", text: "No problems" };
    }
    if (errors === 0) {
      return { cls: "warn", text: warnings + (warnings === 1 ? " warning" : " warnings") };
    }

    var text = errors + (errors === 1 ? " error" : " errors");
    if (warnings > 0) {
      text += ", " + warnings + (warnings === 1 ? " warning" : " warnings");
    }
    return { cls: "error", text: text };
  }

  // --- rendering -----------------------------------------------------------

  function render(state) {
    var problems = state.problems || [];
    var nodes = selectedNodes(state);

    if (nodes.length === 0) {
      panel.classList.remove("open");
      body.innerHTML = '<p class="hint">Select a node to edit it.</p>';
      cells = [];
      return;
    }

    panel.classList.add("open");

    // Taken before the body is replaced and put back afterwards: a commit is
    // answered with a fresh document, which re-renders the panel, which
    // replaces the element the user is typing in. Without this the caret is
    // lost after every edit and the next keystroke reaches the page, where
    // Delete removes the selected node.
    var focus = captureFocus();
    var signature = selectionSignature(nodes);

    cells = [];
    var declared = sharedFields(nodes);
    for (var i = 0; i < declared.length; i++) {
      cells.push(buildCell(declared[i], nodes, problems));
    }

    var undeclared = undeclaredKeys(nodes, declared);
    for (var j = 0; j < undeclared.length; j++) {
      var key = undeclared[j];
      var value = hasValue(nodes[0], key) ? nodes[0].data[key] : null;
      cells.push(buildCell(describeUndeclared(key, value), nodes, problems));
    }

    var html = headerHtml(nodes, problems);

    if (declared.length > 0) {
      html += '<h2 class="section">Properties</h2>' + fieldsHtml(cells, true);
    }
    if (undeclared.length > 0) {
      html += '<h2 class="section">Other properties</h2>' + fieldsHtml(cells, false);
    }
    if (declared.length === 0 && undeclared.length === 0) {
      html += '<h2 class="section">Properties</h2>'
           +  '<p class="hint">This node type declares no properties.</p>';
    }

    html += problemsHtml(nodes, problems);

    body.innerHTML = html;
    bindCells();
    restoreFocus(focus, signature);
  }

  function headerHtml(nodes, problems) {
    var types = [];
    for (var i = 0; i < nodes.length; i++) {
      var type = nodes[i].type || "(no type)";
      if (types.indexOf(type) === -1) { types.push(type); }
    }

    var names = [];
    for (var j = 0; j < types.length; j++) {
      var name = declarationFor(types[j]).name;
      if (name) { names.push(name); }
    }

    var ids = [];
    for (var k = 0; k < nodes.length; k++) { ids.push(nodes[k].id); }
    var idText = nodes.length === 1 ? ids[0] : nodes.length + " nodes selected";

    var chip = stateChip(nodes, problems);

    return '<div class="node-head">'
         +   '<div class="head-row">'
         +     '<span class="node-type">' + shell.escapeHtml(types.join(", ")) + '</span>'
         +     (names.length ? '<span class="node-name">' + shell.escapeHtml(names.join(", ")) + '</span>' : '')
         +   '</div>'
         +   '<div class="head-row">'
         +     '<span class="node-id" title="' + shell.escapeHtml(ids.join(", ")) + '">'
         +       shell.escapeHtml(idText)
         +     '</span>'
         +     '<span class="node-state ' + chip.cls + '">' + shell.escapeHtml(chip.text) + '</span>'
         +   '</div>'
         + '</div>';
  }

  /// The property rows for a run of cells.
  ///
  /// The cells are already in order and the declared ones come first, so the
  /// two sections are two slices of one list rather than two lists that have
  /// to be kept in step.
  function fieldsHtml(all, declared) {
    var html = "";
    for (var i = 0; i < all.length; i++) {
      if (!!all[i].field.undeclared !== !declared) { continue; }
      html += fieldHtml(all[i]);
    }
    return html;
  }

  function fieldHtml(cell) {
    var field = cell.field;
    var kind = kindOf(field);
    var key = shell.escapeHtml(field.key);

    // A property the validator rejected is marked here as well as in the
    // problem list below. The message alone would make the user read the panel
    // to find which of a dozen fields it is about.
    var state = cell.problem ? " invalid" : "";

    // The wrapper carries `data-field` and the control carries `data-key`, so
    // that the two can never be confused for one another when looking up the
    // control a commit belongs to.
    var html = '<div class="field kind-' + shell.escapeHtml(kind.control) + state + '"'
             +  ' data-field="' + key + '">';

    if (kind.control === "checkbox") {
      // The label sits beside the box rather than above it: a checkbox with
      // its name on the line above reads as a heading for whatever comes next.
      html += '<label class="check">'
           +    controlHtml(cell, kind)
           +    '<span>' + shell.escapeHtml(field.label) + '</span>'
           +    (cell.mixed ? '<span class="mixed">multiple values</span>' : '')
           +  '</label>';
    } else {
      html += '<label for="field-' + key + '">'
           +    shell.escapeHtml(field.label)
           +    (cell.mixed ? '<span class="mixed">multiple values</span>' : '')
           +  '</label>'
           +  controlHtml(cell, kind);
    }

    if (field.hint) {
      html += '<p class="hint">' + shell.escapeHtml(field.hint) + '</p>';
    }

    return html + messageHtml(cell) + '</div>';
  }

  function controlHtml(cell, kind) {
    var key = shell.escapeHtml(cell.field.key);
    var attrs = ' id="field-' + key + '" data-key="' + key + '"';

    if (cell.readonly) {
      return '<input' + attrs + ' class="readonly" value="' + shell.escapeHtml(cell.text) + '"'
           +  ' readonly title="This value is a structure, which has no one-line form.">';
    }

    switch (kind.control) {
      case "textarea":
        return '<textarea' + attrs + ' rows="4"'
             +  (cell.mixed ? ' placeholder="multiple values"' : '')
             +  '>' + shell.escapeHtml(cell.text) + '</textarea>';

      case "number":
        return '<input' + attrs + ' type="number" value="' + shell.escapeHtml(cell.text) + '"'
             +  (cell.mixed ? ' placeholder="multiple values"' : '') + '>';

      case "checkbox":
        return '<input' + attrs + ' type="checkbox"'
             +  (cell.value === true ? ' checked' : '') + '>';

      case "select":
        return selectHtml(cell);

      case "asset":
        // The value is a path, and the picker that would fill it in belongs to
        // the asset panel in a later wave. The button is here and disabled so
        // that the field says what it is for, rather than pretending a text
        // box is the whole of an asset reference.
        return '<div class="asset">'
             +    '<input' + attrs + ' type="text" value="' + shell.escapeHtml(cell.text) + '"'
             +      (cell.mixed ? ' placeholder="multiple values"' : '') + '>'
             +    '<button type="button" class="browse" disabled'
             +      ' title="Not implemented yet: the file picker arrives with the asset panel.">'
             +      'Browse</button>'
             +  '</div>';

      default:
        return '<input' + attrs + ' type="text" value="' + shell.escapeHtml(cell.text) + '"'
             +  (cell.mixed ? ' placeholder="multiple values"' : '') + '>';
    }
  }

  function selectHtml(cell) {
    var field = cell.field;
    var key = shell.escapeHtml(field.key);
    var options = field.options || [];
    var html = '<select id="field-' + key + '" data-key="' + key + '">';
    var known = false;

    for (var i = 0; i < options.length; i++) {
      var selected = (!cell.mixed && options[i] === cell.value) ? ' selected' : '';
      if (options[i] === cell.value) { known = true; }
      html += '<option value="' + shell.escapeHtml(options[i]) + '"' + selected + '>'
           +  shell.escapeHtml(options[i]) + '</option>';
    }

    if (cell.mixed) {
      html += '<option value="" selected>multiple values</option>';
    } else if (!known) {
      // The value is not one the declaration lists — an empty one, or one
      // written by a newer build. Shown as its own option rather than dropped:
      // a select with no matching option displays the first one, and the next
      // commit would then write that wrong value into the document.
      var text = displayValue(cell.value);
      html += '<option value="' + shell.escapeHtml(text) + '" selected>'
           +  (text === "" ? "(not set)" : shell.escapeHtml(text) + " (not in the list)")
           +  '</option>';
    }

    return html + '</select>';
  }

  function messageHtml(cell) {
    if (!cell.problem) {
      return '<div class="message"></div>';
    }
    return '<div class="message ' + severityClass(cell.problem) + '">'
         +  shell.escapeHtml(cell.problem.message) + '</div>';
  }

  function problemsHtml(nodes, problems) {
    var relevant = problemsForNodes(nodes, problems);
    var html = '<h2 class="section">Problems</h2>';

    if (relevant.length === 0) {
      return html + '<p class="hint">Nothing reported for '
           + (nodes.length === 1 ? "this node" : "these nodes") + '.</p>';
    }

    for (var i = 0; i < relevant.length; i++) {
      var problem = relevant[i];
      var subject = nodes.length > 1 && problem.subjectId
                  ? '<span class="subject">' + shell.escapeHtml(problem.subjectId) + '</span>'
                  : '';
      html += '<div class="problem ' + severityClass(problem) + '">'
           +  subject + shell.escapeHtml(problem.message) + '</div>';
    }
    return html;
  }

  // --- the DOM -------------------------------------------------------------

  /// Attach the built cells to the elements that were just written.
  ///
  /// After innerHTML rather than during it, because the element and the
  /// message slot are what a later commit needs and neither exists until the
  /// markup has been parsed.
  function bindCells() {
    for (var i = 0; i < cells.length; i++) {
      var cell = cells[i];
      var field = body.querySelector('[data-field="' + attributeValue(cell.field.key) + '"]');
      // The control rather than the row: a row holds a label and a message as
      // well, and reading a value off the wrong one would post an empty string.
      cell.element = field ? field.querySelector("[data-key]") : null;
      cell.note = field ? field.querySelector(".message") : null;

      // A mixed checkbox shows the third state, which no attribute can
      // express. Clicking it clears it and commits true, which is what the
      // user meant by clicking a box that was neither on nor off.
      if (cell.element && cell.element.type === "checkbox" && cell.mixed) {
        cell.element.indeterminate = true;
      }
    }
  }

  /// A value safe to place inside a quoted attribute selector.
  ///
  /// `querySelector` parses a backslash as an escape and a quote as the end of
  /// the string, so a document key holding either would throw out of the
  /// render loop and leave the panel showing the previous node. Keys are
  /// normally bare identifiers, but they come from a file people edit by hand.
  function attributeValue(text) {
    return String(text).replace(/\\/g, "\\\\").replace(/"/g, '\\"');
  }

  function captureFocus() {
    var active = document.activeElement;
    if (!active || !active.getAttribute || !body.contains(active)) { return null; }

    var key = active.getAttribute("data-key");
    if (!key) { return null; }

    return { key: key, start: active.selectionStart, end: active.selectionEnd };
  }

  function restoreFocus(focus, signature) {
    if (!focus || signature !== selectionSignature(selectedNodes(shell.state))) { return; }

    var element = body.querySelector('[data-key="' + focus.key + '"]');
    if (!element) { return; }
    element.focus();

    // A number input refuses setSelectionRange, and a checkbox has no
    // selection to restore, so only the text controls get the caret back.
    var caret = element.tagName === "TEXTAREA" ||
                (element.tagName === "INPUT" && element.type === "text");
    if (caret && focus.start !== null && focus.end !== null) {
      element.setSelectionRange(focus.start, focus.end);
    }
  }

  // --- editing -------------------------------------------------------------

  /// Turn what the control holds into a document value.
  ///
  /// The only refusals are a number that is not a number and a required
  /// property that is empty. Both are about the value's shape, which the
  /// control knows; whether a value makes sense for the story is the
  /// validator's question and is answered by the host.
  function readValue(cell) {
    var field = cell.field;
    var kind = kindOf(field);
    var element = cell.element;

    if (kind.control === "checkbox") {
      return { ok: true, value: element.checked };
    }

    if (kind.control === "number") {
      var text = element.value.trim();
      if (text === "") {
        if (field.required) {
          return { ok: false, message: "A number is required here." };
        }
        // Cleared means zero: a number field that refuses an empty box would
        // leave the user with no way to say "none".
        return { ok: true, value: 0 };
      }
      var number = Number(text);
      if (!isFinite(number)) {
        return { ok: false, message: '"' + element.value + '" is not a number.' };
      }
      return { ok: true, value: number };
    }

    var value = element.value;
    if (field.required && value.trim() === "") {
      return { ok: false, message: "This cannot be empty." };
    }
    return { ok: true, value: value };
  }

  /// Send one committed edit to the host.
  ///
  /// One message per changed node, which for a single selection is one message
  /// per edit. The alternative — one message carrying several nodes — is a
  /// message shape the host does not have, and the cost of adding it would
  /// land on the bridge rather than here.
  function commitField(cell) {
    if (!cell || cell.readonly || !cell.element) { return; }

    var read = readValue(cell);
    if (!read.ok) {
      markField(cell, read.message);
      return;
    }
    clearField(cell);

    for (var i = 0; i < cell.nodes.length; i++) {
      var node = cell.nodes[i];
      var current = hasValue(node, cell.field.key) ? node.data[cell.field.key] : null;
      if (sameValue(current, read.value)) { continue; }
      shell.post({
        type: "setProperty",
        id: node.id,
        key: cell.field.key,
        value: read.value
      });
    }
  }

  function markField(cell, message) {
    var field = cell.element ? cell.element.closest(".field") : null;
    if (field) { field.classList.add("invalid"); }
    if (cell.note) {
      cell.note.className = "message error";
      cell.note.textContent = message;
    }
  }

  function clearField(cell) {
    var field = cell.element ? cell.element.closest(".field") : null;
    if (field) { field.classList.remove("invalid"); }
    if (cell.note) {
      cell.note.className = "message";
      cell.note.textContent = "";
    }
  }

  function cellFor(key) {
    for (var i = 0; i < cells.length; i++) {
      if (cells[i].field.key === key) { return cells[i]; }
    }
    return null;
  }

  // --- registration --------------------------------------------------------

  shell = window.storynode.shell;

  shell.register({
    initialise: function () {
      panel = document.getElementById("panel");
      body = document.getElementById("inspector");

      // `change` fires when a text field loses focus or the user presses
      // Enter, which is the moment an edit is finished. `input` would fire per
      // keystroke, and a message per character would put the whole document
      // through the bridge once per letter typed.
      body.addEventListener("change", function (event) {
        var element = event.target;
        var key = element && element.getAttribute ? element.getAttribute("data-key") : null;
        if (key) { commitField(cellFor(key)); }
      });
    },

    render: render
  });
})();
