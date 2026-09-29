// ---------------------------------------------------------------------------
// The canvas: the node graph.
//
// Owns the graph area and nothing else. It draws nodes and edges, handles
// panning, zooming, dragging and selection, and reports what the user did to
// the host. It never edits the document itself: a drag moves the node on
// screen and sends one message when the drag ends, and the host answers with
// a fresh snapshot.
//
// That is why a drag feels immediate despite the round trip. The movement is
// local and the message is sent once, at the end, rather than per mouse move.
//
// The area registers itself with the shell and is called back to render. It
// reads the document and the selection from the shell's state and never
// reaches into the inspector.
// ---------------------------------------------------------------------------

(function () {
  "use strict";

  var shell = null;

  var graph = null;
  var viewport = null;
  var edgesSvg = null;

  // The gesture in progress, if any. At most one at a time.
  var panning = null;
  var dragging = null;

  // --- geometry ------------------------------------------------------------
  //
  // These numbers are the contract between this file and canvas.css. The port
  // positions are computed here and drawn there, so if one changes the other
  // must. They are named rather than written inline at both places so the
  // pairing is visible.

  var NODE_WIDTH = 220;
  var HEADER_HEIGHT = 34;
  var PORT_SPREAD = 44;

  /// Where a port sits in graph coordinates.
  function portPoint(node, portId, kind) {
    var x = (node.position && node.position.x) || 0;
    var y = (node.position && node.position.y) || 0;
    var width = (node.size && node.size.x) || NODE_WIDTH;

    var ports = node.ports || [];
    var index = 0;
    for (var i = 0; i < ports.length; i++) {
      if (ports[i].id === portId) { index = i; break; }
    }
    // A single port sits centred; several are spread down the node.
    var offset = ports.length > 1 ? (index / (ports.length - 1)) : 0.5;

    return {
      x: x + (kind === "out" ? width : 0),
      y: y + HEADER_HEIGHT + offset * PORT_SPREAD
    };
  }

  /// A horizontal cubic bezier.
  ///
  /// Two control points pulled sideways give the familiar graph-editor curve,
  /// and the horizontal entry into the target reads as direction even when the
  /// edge runs backwards.
  function curve(a, b) {
    var dx = Math.max(40, Math.abs(b.x - a.x) * 0.5);
    return "M " + a.x + " " + a.y +
           " C " + (a.x + dx) + " " + a.y +
           ", " + (b.x - dx) + " " + b.y +
           ", " + b.x + " " + b.y;
  }

  function applyTransform() {
    var state = shell.state;
    var transform =
      "translate(" + state.pan.x + "px," + state.pan.y + "px) scale(" + state.zoom + ")";
    viewport.style.transform = transform;
    // The edge layer is in the same coordinate space, so it gets the same
    // transform. Both are children of the graph, which is what makes a single
    // transform correct for the whole scene.
    edgesSvg.style.transform = transform;
    document.getElementById("zoom").textContent = Math.round(state.zoom * 100) + "%";
  }

  // --- drawing -------------------------------------------------------------

  function renderNodes(state) {
    var nodes = (state.document && state.document.nodes) || [];

    // Rebuild rather than diff. A story has hundreds of nodes, and a full
    // rebuild of a few hundred small elements is well inside a frame. Diffing
    // would be faster and much easier to get subtly wrong.
    var html = "";
    for (var i = 0; i < nodes.length; i++) {
      var node = nodes[i];
      var selected = node.id === state.selectedId ? " selected" : "";
      var x = (node.position && node.position.x) || 0;
      var y = (node.position && node.position.y) || 0;

      html += '<div class="node' + selected + '" data-id="' + shell.escapeHtml(node.id) + '"'
           +  ' style="left:' + x + 'px; top:' + y + 'px">'
           +    '<div class="head">'
           +      '<span class="kind">' + shell.escapeHtml(node.type) + '</span>'
           +      '<span class="id">' + shell.escapeHtml(node.id) + '</span>'
           +    '</div>'
           +    '<div class="body">' + describeNode(node) + '</div>'
           +    portsHtml(node)
           +  '</div>';
    }

    viewport.innerHTML = html;
  }

  function describeNode(node) {
    var data = node.data || {};
    var summary = data.speaker || data.text || data.label;
    if (summary) {
      return shell.escapeHtml(String(summary).slice(0, 80));
    }
    return '<span class="empty">no content yet</span>';
  }

  function portsHtml(node) {
    var ports = node.ports || [];
    var html = "";
    for (var i = 0; i < ports.length; i++) {
      var port = ports[i];
      var cls = port.kind === "input" ? "in" : "out";
      var offset = ports.length > 1 ? (i / (ports.length - 1)) : 0.5;
      // Percentage of the node's height, matching portPoint's arithmetic.
      var top = ((HEADER_HEIGHT + offset * PORT_SPREAD) / 120) * 100;
      html += '<div class="port ' + cls + '" data-port="' + shell.escapeHtml(port.id) + '"'
           +  ' data-node="' + shell.escapeHtml(node.id) + '"'
           +  ' title="' + shell.escapeHtml(port.label || port.id) + '"'
           +  ' style="top:' + top + '%"></div>';
    }
    return html;
  }

  function renderEdges(state) {
    var doc = state.document;
    if (!doc) {
      edgesSvg.innerHTML = "";
      return;
    }

    var html = "";
    var edges = doc.edges || [];
    for (var i = 0; i < edges.length; i++) {
      var edge = edges[i];
      var from = shell.findNode(edge.from.nodeId);
      var to = shell.findNode(edge.to.nodeId);
      // An edge whose endpoints are missing is a validation error the host
      // already reports. Drawing nothing is better than drawing a line to the
      // origin, which looks like a real connection.
      if (!from || !to) { continue; }

      var a = portPoint(from, edge.from.portId, "out");
      var b = portPoint(to, edge.to.portId, "in");
      html += '<path data-id="' + shell.escapeHtml(edge.id) + '" d="' + curve(a, b) + '"></path>';
    }

    edgesSvg.innerHTML = html;
  }

  // --- interaction ---------------------------------------------------------

  function beginPan(event) {
    var state = shell.state;
    panning = { x: event.clientX, y: event.clientY, px: state.pan.x, py: state.pan.y };
    graph.classList.add("panning");
    event.preventDefault();
  }

  function beginDrag(nodeElement, event) {
    var state = shell.state;
    var id = nodeElement.dataset.id;
    var node = shell.findNode(id);
    if (!node) { return; }

    shell.select(id);

    dragging = {
      node: node,
      x: event.clientX,
      y: event.clientY,
      startX: (node.position && node.position.x) || 0,
      startY: (node.position && node.position.y) || 0
    };
    event.preventDefault();
    event.stopPropagation();
  }

  function moveDrag(event) {
    var state = shell.state;
    var scale = state.zoom;

    dragging.node.position.x = dragging.startX + (event.clientX - dragging.x) / scale;
    dragging.node.position.y = dragging.startY + (event.clientY - dragging.y) / scale;

    var element = viewport.querySelector('[data-id="' + dragging.node.id + '"]');
    if (element) {
      element.style.left = dragging.node.position.x + "px";
      element.style.top = dragging.node.position.y + "px";
    }
    // Redraw the edges so they follow. Cheap: the edge layer is rebuilt from
    // the in-memory positions, with no round trip to the host.
    renderEdges(state);
  }

  function endDrag() {
    // One message when the drag ends, not one per mouse move. A drag that sent
    // a message per frame would put the whole document through the bridge
    // sixty times a second for one gesture.
    shell.post({
      type: "moveNode",
      id: dragging.node.id,
      x: dragging.node.position.x,
      y: dragging.node.position.y
    });
    dragging = null;
  }

  /// Frame the graph in the window.
  function fitToWindow() {
    var state = shell.state;
    var nodes = (state.document && state.document.nodes) || [];
    if (nodes.length === 0) { return; }

    var minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
    for (var i = 0; i < nodes.length; i++) {
      var p = nodes[i].position || { x: 0, y: 0 };
      var w = (nodes[i].size && nodes[i].size.x) || NODE_WIDTH;
      var h = (nodes[i].size && nodes[i].size.y) || 120;
      minX = Math.min(minX, p.x);
      minY = Math.min(minY, p.y);
      maxX = Math.max(maxX, p.x + w);
      maxY = Math.max(maxY, p.y + h);
    }

    var rect = graph.getBoundingClientRect();
    var margin = 60;
    var scale = Math.min(
      (rect.width - margin * 2) / Math.max(1, maxX - minX),
      (rect.height - margin * 2) / Math.max(1, maxY - minY)
    );
    state.zoom = Math.min(1.5, Math.max(0.15, scale));

    state.pan.x = rect.width / 2 - ((minX + maxX) / 2) * state.zoom;
    state.pan.y = rect.height / 2 - ((minY + maxY) / 2) * state.zoom;
    applyTransform();
  }

  /// Place a new node where the user is looking, not at the document origin,
  /// which may be far off screen.
  function addNodeAtViewCentre(type) {
    var state = shell.state;
    var rect = graph.getBoundingClientRect();
    var x = (rect.width / 2 - state.pan.x) / state.zoom - NODE_WIDTH / 2;
    var y = (rect.height / 2 - state.pan.y) / state.zoom - 40;
    shell.post({ type: "addNode", nodeType: type, x: x, y: y });
  }

  // --- wiring --------------------------------------------------------------

  function wire() {
    // Panning. The whole graph is dragged with the left button on empty space,
    // which is what every node editor does and therefore what the hand expects.
    graph.addEventListener("mousedown", function (event) {
      var onEmpty = event.target === graph ||
                    event.target === viewport ||
                    event.target === edgesSvg;
      if (!onEmpty) { return; }

      // Clicking empty space also clears the selection.
      if (shell.state.selectedId) {
        shell.select(null);
      }
      beginPan(event);
    });

    window.addEventListener("mousemove", function (event) {
      if (panning) {
        var state = shell.state;
        state.pan.x = panning.px + (event.clientX - panning.x);
        state.pan.y = panning.py + (event.clientY - panning.y);
        applyTransform();
      }
      if (dragging) {
        moveDrag(event);
      }
    });

    window.addEventListener("mouseup", function () {
      if (panning) {
        panning = null;
        graph.classList.remove("panning");
      }
      if (dragging) {
        endDrag();
      }
    });

    viewport.addEventListener("mousedown", function (event) {
      // A port is a connection target, not a drag handle. Connecting is a later
      // wave; ignoring the press here keeps a port click from starting a drag.
      if (event.target.closest(".port")) { return; }

      var nodeElement = event.target.closest(".node");
      if (!nodeElement) { return; }
      beginDrag(nodeElement, event);
    });

    // Zoom, anchored on the pointer so the thing under the cursor stays put.
    // That is the difference between zooming feeling controlled and feeling
    // like the view is sliding away.
    graph.addEventListener("wheel", function (event) {
      event.preventDefault();

      var state = shell.state;
      var rect = graph.getBoundingClientRect();
      var mx = event.clientX - rect.left;
      var my = event.clientY - rect.top;

      var factor = event.deltaY < 0 ? 1.1 : 1 / 1.1;
      var next = Math.min(4, Math.max(0.15, state.zoom * factor));
      var applied = next / state.zoom;

      state.pan.x = mx - (mx - state.pan.x) * applied;
      state.pan.y = my - (my - state.pan.y) * applied;
      state.zoom = next;

      applyTransform();
    }, { passive: false });

    document.getElementById("add-dialog").addEventListener("click", function () {
      addNodeAtViewCentre("dialog");
    });
    document.getElementById("add-branch").addEventListener("click", function () {
      addNodeAtViewCentre("branch");
    });
    document.getElementById("add-end").addEventListener("click", function () {
      addNodeAtViewCentre("end");
    });

    document.getElementById("fit").addEventListener("click", fitToWindow);

    // Delete removes the selection, which is what the key means in every editor
    // and is the fastest way to undo a misclick. Ignored while a field has
    // focus so it does not eat a backspace.
    window.addEventListener("keydown", function (event) {
      var inField = event.target.tagName === "INPUT" ||
                    event.target.tagName === "TEXTAREA";
      if (inField) { return; }

      if (event.key === "Delete" || event.key === "Backspace") {
        if (shell.state.selectedId) {
          shell.post({ type: "removeNode", id: shell.state.selectedId });
          shell.select(null);
          event.preventDefault();
        }
      }
    });
  }

  // --- registration --------------------------------------------------------

  shell = window.storynode.shell;

  shell.register({
    initialise: function () {
      graph = document.getElementById("graph");
      viewport = document.getElementById("viewport");
      edgesSvg = document.getElementById("edges");
      wire();
    },

    render: function (state) {
      renderNodes(state);
      renderEdges(state);
      applyTransform();
    },

    onSelectionChanged: function () {
      // The node classes carry the selection, so a redraw is the whole update.
    }
  });
})();
