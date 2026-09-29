// ---------------------------------------------------------------------------
// The inspector: the property panel for the selected node.
//
// Owns the side panel and nothing else. It reads the selection from the
// shell, renders the selected node's properties, and reports edits to the
// host. It never touches the canvas and never edits the document itself.
//
// Properties are driven by a small table rather than by hard-coded fields.
// A later wave replaces the table with each node type's declared schema, and
// that change is confined to this file: the panel does not know what a
// dialogue node is, only that a node has properties with names and types.
// ---------------------------------------------------------------------------

(function () {
  "use strict";

  var shell = null;
  var panel = null;
  var body = null;

  /// The property fields shown for every node type.
  ///
  /// `key` is the document key. `label` is what the user sees. The pair is
  /// kept separate because the document format is a file people read and edit
  /// by hand, so its keys are lower case and stable, while the label is
  /// presentation and may change.
  var FIELDS = [
    { key: "speaker", label: "speaker", multiline: false },
    { key: "text",    label: "text",    multiline: true  }
  ];

  function propertyField(field, value) {
    var escaped = shell.escapeHtml(value);
    if (field.multiline) {
      return '<div class="field">'
           +   '<label>' + shell.escapeHtml(field.label) + '</label>'
           +   '<textarea data-key="' + shell.escapeHtml(field.key) + '" rows="4">'
           +     escaped
           +   '</textarea>'
           + '</div>';
    }
    return '<div class="field">'
         +   '<label>' + shell.escapeHtml(field.label) + '</label>'
         +   '<input data-key="' + shell.escapeHtml(field.key) + '" value="' + escaped + '">'
         + '</div>';
  }

  /// The problems the host reported for this node.
  ///
  /// Shown here rather than only in the status bar because a problem is about
  /// a specific node, and the place to read it is where the node is edited.
  function problemsFor(nodeId, problems) {
    var relevant = [];
    for (var i = 0; i < problems.length; i++) {
      if (problems[i].subjectId === nodeId) { relevant.push(problems[i]); }
    }

    if (relevant.length === 0) {
      return '<p class="hint">Nothing reported for this node.</p>';
    }

    var html = "";
    for (var j = 0; j < relevant.length; j++) {
      var problem = relevant[j];
      html += '<div class="problem ' + shell.escapeHtml(problem.severity) + '">'
           +  shell.escapeHtml(problem.message) + '</div>';
    }
    return html;
  }

  function render(state) {
    var node = state.selectedId ? shell.findNode(state.selectedId) : null;

    if (!node) {
      panel.classList.remove("open");
      body.innerHTML = '<p class="hint">Select a node to edit it.</p>';
      return;
    }

    panel.classList.add("open");

    var html = '<div class="field">'
             +   '<label>id</label>'
             +   '<input value="' + shell.escapeHtml(node.id) + '" disabled>'
             + '</div>'
             + '<div class="field">'
             +   '<label>type</label>'
             +   '<input value="' + shell.escapeHtml(node.type) + '" disabled>'
             + '</div>';

    var data = node.data || {};
    for (var i = 0; i < FIELDS.length; i++) {
      var field = FIELDS[i];
      var value = data[field.key];
      html += propertyField(field, value == null ? "" : String(value));
    }

    html += '<h2 class="section">Problems</h2>';
    html += problemsFor(node.id, state.problems);

    body.innerHTML = html;
  }

  /// Send an edit to the host.
  ///
  /// Sent when the field loses focus or the user presses Enter, not on every
  /// keystroke: a message per character would put the whole document through
  /// the bridge once per letter typed.
  function commit(input) {
    var node = shell.state.selectedId ? shell.findNode(shell.state.selectedId) : null;
    if (!node) { return; }

    shell.post({
      type: "setProperty",
      id: node.id,
      key: input.dataset.key,
      value: input.value
    });
  }

  shell = window.storynode.shell;

  shell.register({
    initialise: function () {
      panel = document.getElementById("panel");
      body = document.getElementById("inspector");

      // `change` fires on blur and on Enter, which is the right moment.
      // `input` would fire per keystroke.
      body.addEventListener("change", function (event) {
        var field = event.target.closest("input[data-key], textarea[data-key]");
        if (field) { commit(field); }
      });
    },

    render: render
  });
})();
