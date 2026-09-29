// ---------------------------------------------------------------------------
// The canvas: the node graph.
//
// Owns the graph area and nothing else. It draws nodes and edges, handles
// panning, zooming, dragging, selection, connection dragging and keyboard
// navigation, and reports what the user did to the host. It never edits the
// document itself: a drag moves the nodes on screen and sends one message when
// the drag ends, and the host answers with a fresh snapshot.
//
// That is why a drag feels immediate despite the round trip. The movement is
// local and the message is sent once, at the end, rather than per mouse move.
//
// The area registers itself with the shell and is called back to render. It
// reads the document and the selection from the shell's state and never
// reaches into the inspector.
//
// Two shapes of state live here and they are not the same kind of thing:
//
//   elements   a cache of the DOM the current document produced. Keyed by node
//              id and rebuilt only where it disagrees with the document.
//   gesture    the interaction in progress, if any. At most one at a time.
//
// The cache exists because a story can hold hundreds of nodes. Panning and
// zooming change nothing about the document, so they must not rebuild it: they
// move one transform and leave every element alone. See the note on `stats`.
// ---------------------------------------------------------------------------

(function () {
  "use strict";

  var shell = null;

  var graph = null;
  var viewport = null;
  var edgesSvg = null;
  var marquee = null;

  // --- geometry ------------------------------------------------------------
  //
  // These numbers are the contract between this file and canvas.css. The port
  // positions are computed here and drawn there, so if one changes the other
  // must. They are named rather than written inline at both places so the
  // pairing is visible.

  var NODE_WIDTH = 220;
  var NODE_HEIGHT = 120;
  var HEADER_HEIGHT = 34;
  var PORT_SPREAD = 44;

  /// How far apart two edges between the same pair of ports are drawn.
  ///
  /// Small enough that a bundle of three still reads as one connection, wide
  /// enough that the strokes do not merge into a single line at the zoom a
  /// whole story is looked at. The stroke is 1.5 wide, so anything above about
  /// six separates them.
  var EDGE_SPREAD = 16;

  /// The horizontal gap below which an edge is treated as running backwards.
  ///
  /// A source and target whose edges are nearly aligned are the hard case: a
  /// plain S-curve between them is a vertical line through both nodes. Below
  /// this gap the curve is routed around instead.
  var BACKWARD_GAP = 48;

  /// Where a port sits in graph coordinates.
  ///
  /// `side` is which edge of the node the dot is drawn on: an output leaves
  /// to the right and an input arrives on the left, unless the edge runs
  /// backwards and enters from the other side.
  function portPoint(node, portId, kind, side) {
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

    var right = kind === "out" ? side !== "left" : side === "right";
    return {
      x: x + (right ? width : 0),
      y: y + HEADER_HEIGHT + offset * PORT_SPREAD
    };
  }

  /// The vertical fraction a port sits at, for the stylesheet to position the
  /// dot with. Kept next to portPoint so the two cannot drift: the dot and the
  /// line have to meet.
  function portFraction(node, portId) {
    var ports = node.ports || [];
    var index = 0;
    for (var i = 0; i < ports.length; i++) {
      if (ports[i].id === portId) { index = i; break; }
    }
    var offset = ports.length > 1 ? (index / (ports.length - 1)) : 0.5;
    return ((HEADER_HEIGHT + offset * PORT_SPREAD) / NODE_HEIGHT) * 100;
  }

  /// The two control points of a cubic bezier between two ports.
  ///
  /// Two control points pulled sideways give the familiar graph-editor curve.
  /// The horizontal entry into the target reads as direction even when the
  /// edge runs backwards.
  ///
  /// The backward case is the reason this is not one line. With the control
  /// points pulled by half the horizontal distance, an edge whose target sits
  /// to the left of its source collapses into a vertical line through both
  /// nodes — the curve has nowhere to go. Pulling them by the target's width
  /// plus the gap instead sends the curve out past the target and back into
  /// its input from the left, which is where the dot is.
  ///
  /// Returned as numbers rather than as a path string so that the offsetting
  /// below moves points instead of parsing them. The first version of this
  /// built the string and then split it to move the control points, and the
  /// split collapsed the ", " separators into one — so the offset was applied
  /// to a shorter list than it expected, the guard below rejected it, and two
  /// edges between the same pair of ports drew the identical path.
  function curveControl(a, b, backward) {
    if (backward) {
      var reach = Math.max(80, Math.abs(a.x - b.x) * 0.5 + 60);
      return { x1: a.x + reach, y1: a.y, x2: b.x - reach, y2: b.y };
    }
    var dx = Math.max(40, Math.abs(b.x - a.x) * 0.5);
    return { x1: a.x + dx, y1: a.y, x2: b.x - dx, y2: b.y };
  }

  function formatCurve(a, b, control) {
    return "M " + a.x + " " + a.y +
           " C " + control.x1 + " " + control.y1 +
           ", " + control.x2 + " " + control.y2 +
           ", " + b.x + " " + b.y;
  }

  function curveBetween(a, b, backward) {
    return formatCurve(a, b, curveControl(a, b, backward));
  }

  /// A cubic bezier, displaced sideways.
  ///
  /// Used for the second and later edges between the same pair of ports. Both
  /// control points are moved the same way, which bows the curve without
  /// moving its endpoints — the two dots the line connects stay put.
  ///
  /// `offset` is measured perpendicular to the straight line between the
  /// endpoints, so a bundle of edges separates the same way whichever
  /// direction the pair runs.
  function curvePath(a, b, backward, offset) {
    var control = curveControl(a, b, backward);
    if (!offset) { return formatCurve(a, b, control); }

    var dx = b.x - a.x;
    var dy = b.y - a.y;
    var length = Math.sqrt(dx * dx + dy * dy);
    if (length < 0.001) { return formatCurve(a, b, control); }

    // The normal of (dx, dy), scaled so `offset` is the distance moved.
    var nx = (-dy / length) * offset;
    var ny = (dx / length) * offset;

    control.x1 += nx;
    control.y1 += ny;
    control.x2 += nx;
    control.y2 += ny;

    return formatCurve(a, b, control);
  }

  // --- measurement ---------------------------------------------------------
  //
  // What the last render actually did. Not a benchmark: a count of the work
  // the renderer performed, so that "panning does not rebuild the graph" can
  // be checked rather than believed. `storynode.canvas.stats()` reads it, and
  // `resetStats()` starts a fresh window.
  //
  // The numbers are here because the alternative is an assumption. A full
  // innerHTML rebuild of five hundred nodes is fast enough to feel fine on a
  // developer's machine and slow enough to drop frames on a laptop, and
  // nothing about the code says which it is.

  var stats = {
    frames: 0,          // render() calls
    elementCreates: 0,  // node elements built
    elementUpdates: 0,  // node elements whose content changed
    elementMoves: 0,    // node elements repositioned
    elementRemovals: 0,
    edgePaths: 0        // edge paths written
  };

  // --- the element cache ---------------------------------------------------

  var elements = {};   // node id -> { element, signature, x, y }

  /// Node lookup by id, rebuilt once per render pass.
  ///
  /// `shell.findNode` is a linear scan over the document, which is the right
  /// shape for a one-off lookup and the wrong one for a loop: drawing the
  /// edges asks for both endpoints of every edge, so a five-hundred-node story
  /// with five hundred edges is a quarter of a million string comparisons per
  /// frame — measured at most of a drag's cost. One index per pass turns that
  /// into two lookups per edge.
  var index = {};

  function buildIndex(state) {
    index = {};
    var nodes = (state.document && state.document.nodes) || [];
    for (var i = 0; i < nodes.length; i++) {
      index[nodes[i].id] = nodes[i];
    }
    return nodes;
  }

  function nodeById(id) {
    return index[id] || null;
  }

  function nodeSignature(node) {
    // Only what the node's own markup depends on. Position is deliberately
    // absent: it is a style write, not a rebuild, and a drag changes nothing
    // else about a node.
    var data = node.data || {};
    var summary = data.speaker || data.text || data.label;
    var ports = node.ports || [];
    var portKey = "";
    for (var i = 0; i < ports.length; i++) {
      portKey += ports[i].id + ":" + ports[i].kind + ":" + (ports[i].label || "") + ";";
    }
    return node.type + "\u001f" + (summary == null ? "" : String(summary)) +
           "\u001f" + portKey;
  }

  function buildNode(node) {
    var element = document.createElement("div");
    element.className = "node";
    element.dataset.id = node.id;
    element.innerHTML =
      '<div class="head">' +
        '<span class="kind"></span>' +
        '<span class="id"></span>' +
      '</div>' +
      '<div class="body"></div>' +
      '<div class="ports"></div>';
    fillNode(element, node);
    return element;
  }

  /// Write the parts of a node that are not its position.
  ///
  /// Assigning textContent rather than innerHTML: node content is user data
  /// and goes through the DOM's own escaping, so a line of dialogue
  /// containing markup is displayed as the text the writer typed.
  function fillNode(element, node) {
    var data = node.data || {};
    var summary = data.speaker || data.text || data.label;

    element.querySelector(".kind").textContent = node.type;
    element.querySelector(".id").textContent = node.id;

    var body = element.querySelector(".body");
    if (summary) {
      body.textContent = String(summary).slice(0, 80);
      body.classList.remove("empty");
    } else {
      body.textContent = "no content yet";
      body.classList.add("empty");
    }

    var ports = element.querySelector(".ports");
    ports.textContent = "";
    var list = node.ports || [];
    for (var i = 0; i < list.length; i++) {
      var port = list[i];
      var dot = document.createElement("div");
      dot.className = "port " + (port.kind === "input" ? "in" : "out");
      dot.dataset.port = port.id;
      dot.dataset.node = node.id;
      dot.title = port.label || port.id;
      dot.style.top = portFraction(node, port.id) + "%";
      ports.appendChild(dot);
    }
  }

  // --- drawing -------------------------------------------------------------

  /// Bring the node elements in line with the document.
  ///
  /// Incremental rather than a rebuild, and the difference is the whole point
  /// of the cache: a document message arrives after every edit, and rebuilding
  /// five hundred elements to move one node is work that scales with the
  /// story rather than with the change.
  ///
  /// A node whose markup is unchanged keeps its element and gets at most a
  /// style write. Only nodes whose content, type or ports changed are
  /// refilled, and only nodes that are gone are removed.
  function renderNodes(state) {
    var nodes = buildIndex(state);
    var seen = {};
    var order = [];

    for (var i = 0; i < nodes.length; i++) {
      var node = nodes[i];
      seen[node.id] = true;
      order.push(node.id);

      var x = (node.position && node.position.x) || 0;
      var y = (node.position && node.position.y) || 0;
      var signature = nodeSignature(node);
      var entry = elements[node.id];

      if (!entry) {
        entry = { element: buildNode(node), signature: signature, x: x, y: y };
        entry.element.style.left = x + "px";
        entry.element.style.top = y + "px";
        elements[node.id] = entry;
        viewport.appendChild(entry.element);
        stats.elementCreates++;
      } else if (entry.signature !== signature) {
        fillNode(entry.element, node);
        entry.signature = signature;
        stats.elementUpdates++;
      }

      // A style write is not free — it invalidates layout — so it is skipped
      // when the position has not moved. That is the common case in a
      // document message: one node moved, the rest did not.
      if (entry.x !== x || entry.y !== y) {
        entry.element.style.left = x + "px";
        entry.element.style.top = y + "px";
        entry.x = x;
        entry.y = y;
        stats.elementMoves++;
      }

      // The canvas's own set, not the shell's single anchor. The shell's
      // selectedId is the one node the inspector shows; a rubber band over
      // twenty nodes sets all twenty here and only one there, and drawing
      // from the shell's id would mark one node and lose the rest.
      applySelectionClass(entry.element, isSelected(node.id));
    }

    for (var id in elements) {
      if (elements.hasOwnProperty(id) && !seen[id]) {
        viewport.removeChild(elements[id].element);
        delete elements[id];
        stats.elementRemovals++;
      }
    }

    // The marquee is a child of the viewport so that it pans and zooms with
    // the graph. Appending it last keeps it above the nodes it covers.
    if (marquee && marquee.parentNode === viewport) {
      viewport.appendChild(marquee);
    }
  }

  /// Set or clear the selection class, writing only when it changes.
  ///
  /// `classList.toggle` with the force argument is idempotent, but it still
  /// touches the attribute, and a full document message walks every node.
  function applySelectionClass(element, selected) {
    var has = element.classList.contains("selected");
    if (selected && !has) { element.classList.add("selected"); }
    else if (!selected && has) { element.classList.remove("selected"); }
  }

  /// Position every port dot.
  ///
  /// An input is drawn on the left edge normally and on the right edge when
  /// every edge arriving at it comes from the right. Doing it per port rather
  /// than per edge keeps one dot in one place: two edges into the same port
  /// would otherwise want the dot on two sides at once.
  /// `only` limits the walk to a set of node ids. A drag changes the arrival
  /// direction of the dragged nodes and of whatever they connect to, and
  /// nothing else, so a drag passes that set and a document message passes
  /// null for the whole graph.
  function placePorts(state, only) {
    var nodes = (state.document && state.document.nodes) || [];
    var arrival = arrivalSides(state, only);

    for (var i = 0; i < nodes.length; i++) {
      var node = nodes[i];
      if (only && !only[node.id]) { continue; }

      var entry = elements[node.id];
      if (!entry) { continue; }

      var dots = entry.element.querySelectorAll(".port");
      for (var j = 0; j < dots.length; j++) {
        var dot = dots[j];
        var side = arrival[node.id + "\u001f" + dot.dataset.port] || "left";
        dot.classList.toggle("right", side === "right");
      }
    }
  }

  /// Which side each input port is entered from, given the edges attached.
  ///
  /// A port with no edges is left where it is. A port whose edges all arrive
  /// from the right moves to the right edge, which is what keeps a backward
  /// edge from crossing its own target node.
  function arrivalSides(state, only) {
    var doc = state.document;
    var sides = {};
    if (!doc) { return sides; }

    var edges = doc.edges || [];
    var right = {};
    var total = {};

    for (var i = 0; i < edges.length; i++) {
      var from = nodeById(edges[i].from.nodeId);
      var to = nodeById(edges[i].to.nodeId);
      if (!from || !to) { continue; }
      if (only && !only[to.id]) { continue; }

      var key = to.id + "\u001f" + edges[i].to.portId;
      total[key] = (total[key] || 0) + 1;

      // The source's right edge against the target's left edge. Computed from
      // the same numbers portPoint uses rather than from the endpoints of the
      // curve, so the decision is about the nodes and not about the drawing.
      var sourceRight = ((from.position && from.position.x) || 0) +
                        ((from.size && from.size.x) || NODE_WIDTH);
      var targetLeft = (to.position && to.position.x) || 0;
      if (sourceRight > targetLeft + BACKWARD_GAP) {
        right[key] = (right[key] || 0) + 1;
      }
    }

    for (var portKey in total) {
      if (total.hasOwnProperty(portKey) && right[portKey] === total[portKey]) {
        sides[portKey] = "right";
      }
    }
    return sides;
  }

  /// Redraw the edges.
  ///
  /// Rebuilt from the in-memory positions, with no round trip to the host, so
  /// an edge follows the node it is attached to while the drag is in progress.
  ///
  /// Every edge is a single path element. That is what makes the hover
  /// highlight and the `disconnect` gesture possible: the element carries the
  /// edge id, so a click on the line knows which line it was.
  /// One path element per edge, kept between passes.
  ///
  /// Keyed by edge id and updated in place. A drag redraws the edges on every
  /// mousemove so that they follow the node, and rebuilding five hundred path
  /// elements sixty times a second is the most expensive thing the canvas can
  /// be asked to do. Moving the ones whose endpoints moved costs a path
  /// string each and touches nothing else.
  var edgeElements = {};   // edge id -> { element, d }

  function renderEdges(state) {
    var doc = state.document;
    if (!doc) {
      edgesSvg.textContent = "";
      edgeElements = {};
      stats.edgePaths = 0;
      return;
    }

    var arrival = arrivalSides(state, null);
    var groups = parallelGroups(doc);
    var seen = {};
    var edges = doc.edges || [];
    var drawn = 0;

    for (var i = 0; i < edges.length; i++) {
      var edge = edges[i];
      var from = nodeById(edge.from.nodeId);
      var to = nodeById(edge.to.nodeId);
      // An edge whose endpoints are missing is a validation error the host
      // already reports. Drawing nothing is better than drawing a line to the
      // origin, which looks like a real connection.
      if (!from || !to) { continue; }
      seen[edge.id] = true;

      var key = edge.from.nodeId + "\u001f" + edge.from.portId + "\u001f" +
                edge.to.nodeId + "\u001f" + edge.to.portId;
      var group = groups[key] || { index: 0, count: 1 };

      var a = portPoint(from, edge.from.portId, "out", "right");
      var b = portPoint(to, edge.to.portId, "in",
                        arrival[edge.to.nodeId + "\u001f" + edge.to.portId] || "left");

      var backward = a.x > b.x - BACKWARD_GAP;
      // Centred, so a bundle of two straddles the straight line and a bundle
      // of three puts one on it.
      var offset = (group.index - (group.count - 1) / 2) * EDGE_SPREAD;
      var d = curvePath(a, b, backward, offset);

      var entry = edgeElements[edge.id];
      if (!entry) {
        var element = document.createElementNS("http://www.w3.org/2000/svg", "path");
        element.setAttribute("class", "edge" + (backward ? " backward" : ""));
        element.dataset.id = edge.id;
        element.setAttribute("d", d);
        edgeElements[edge.id] = { element: element, d: d, backward: backward };
        edgesSvg.appendChild(element);
      } else {
        // The path string is the only thing that changes, so it is the only
        // thing written. A geometry write on an SVG path is what makes the
        // browser re-rasterise it, and skipping it when nothing moved is what
        // keeps a document message cheap.
        if (entry.d !== d) {
          entry.element.setAttribute("d", d);
          entry.d = d;
        }
        if (entry.backward !== backward) {
          entry.element.setAttribute("class", "edge" + (backward ? " backward" : ""));
          entry.backward = backward;
        }
      }
      drawn++;
    }

    for (var id in edgeElements) {
      if (edgeElements.hasOwnProperty(id) && !seen[id]) {
        edgesSvg.removeChild(edgeElements[id].element);
        delete edgeElements[id];
      }
    }

    // The preview is not an edge and is kept across passes: a connection drag
    // is in progress while the document underneath it may be re-rendered.
    if (previewPath && previewPath.parentNode !== edgesSvg) {
      edgesSvg.appendChild(previewPath);
    }

    stats.edgePaths = drawn;
  }

  /// Which edge is which within a bundle of edges between the same two ports.
  ///
  /// Two edges joining the same pair of ports draw on top of each other, and a
  /// user cannot tell whether there are two or one. Giving each a position in
  /// the bundle is what separates them.
  ///
  /// Keyed by edge id, not by port pair. A single group object per pair looks
  /// equivalent and is not: the running position is per-edge state, and
  /// storing it on the shared object means the last edge to be numbered
  /// overwrites it for all of them. The first version did exactly that, and
  /// two edges between one pair of ports drew the identical path — which is
  /// the one outcome this function exists to prevent.
  function parallelGroups(doc) {
    var counts = {};
    var taken = {};
    var groups = {};
    var edges = doc.edges || [];
    var i;

    for (i = 0; i < edges.length; i++) {
      var key = edgePairKey(edges[i]);
      counts[key] = (counts[key] || 0) + 1;
    }

    for (i = 0; i < edges.length; i++) {
      var id = edgePairKey(edges[i]);
      var index = taken[id] || 0;
      taken[id] = index + 1;
      groups[edges[i].id] = { index: index, count: counts[id] };
    }

    return groups;
  }

  /// The four fields that decide which edges share a bundle.
  function edgePairKey(edge) {
    return edge.from.nodeId + "\u001f" + edge.from.portId + "\u001f" +
           edge.to.nodeId + "\u001f" + edge.to.portId;
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

  // --- selection -----------------------------------------------------------
  //
  // The shell's selection is one id, because the inspector edits one node and
  // a panel showing four nodes at once is a different panel. A canvas has to
  // select several, so this area keeps the set and tells the shell which of
  // them is the anchor — the one the inspector shows and the one a keyboard
  // step moves from.
  //
  // The set is therefore presentation, like pan and zoom: the host never sees
  // it, and a document message does not carry it.

  var selected = [];      // ids, in the order they were added
  var selectedSet = {};
  var anchorId = null;

  // True while this area is the one changing the shell's selection, so that
  // the callback the shell fires does not look like the host changing it.
  var applyingSelection = false;

  function isSelected(id) { return selectedSet[id] === true; }

  function applySelection(ids, anchor) {
    selected = [];
    selectedSet = {};
    for (var i = 0; i < ids.length; i++) {
      if (!selectedSet[ids[i]]) {
        selectedSet[ids[i]] = true;
        selected.push(ids[i]);
      }
    }
    anchorId = anchor || selected[0] || null;

    // The shell drops the call when the id is unchanged, and the render it
    // would have triggered is the one that draws the selection. So the render
    // is requested here rather than assumed.
    applyingSelection = true;
    shell.select(anchorId);
    applyingSelection = false;
    shell.render();
  }

  function selectOne(id) { applySelection(id ? [id] : [], id); }

  function toggleSelected(id) {
    var ids = selected.slice();
    var index = ids.indexOf(id);
    if (index >= 0) {
      ids.splice(index, 1);
      applySelection(ids, anchorId === id ? null : anchorId);
    } else {
      ids.push(id);
      applySelection(ids, id);
    }
  }

  function selectedNodes() {
    var nodes = [];
    for (var i = 0; i < selected.length; i++) {
      var node = nodeById(selected[i]);
      if (node) { nodes.push(node); }
    }
    return nodes;
  }

  // --- gestures ------------------------------------------------------------

  var gesture = null;

  function graphToWorld(clientX, clientY) {
    var state = shell.state;
    var rect = graph.getBoundingClientRect();
    return {
      x: (clientX - rect.left - state.pan.x) / state.zoom,
      y: (clientY - rect.top - state.pan.y) / state.zoom
    };
  }

  function nodeRect(node) {
    var x = (node.position && node.position.x) || 0;
    var y = (node.position && node.position.y) || 0;
    return {
      left: x,
      top: y,
      right: x + ((node.size && node.size.x) || NODE_WIDTH),
      bottom: y + ((node.size && node.size.y) || NODE_HEIGHT)
    };
  }

  function beginPan(event) {
    var state = shell.state;
    gesture = {
      kind: "pan",
      x: event.clientX,
      y: event.clientY,
      px: state.pan.x,
      py: state.pan.y
    };
    graph.classList.add("panning");
    event.preventDefault();
  }

  /// Start dragging the selection, or the node under the cursor.
  ///
  /// Pressing a node that is already selected keeps the whole selection, which
  /// is what makes "select several and move them together" work: the press
  /// that starts the move must not collapse the set to one node.
  function beginDrag(nodeElement, event) {
    var id = nodeElement.dataset.id;

    if (event.shiftKey || event.ctrlKey || event.metaKey) {
      toggleSelected(id);
      return;
    }
    if (!isSelected(id)) {
      selectOne(id);
    }

    var nodes = selectedNodes();
    if (nodes.length === 0) { return; }

    var origins = [];
    for (var i = 0; i < nodes.length; i++) {
      origins.push({
        node: nodes[i],
        x: (nodes[i].position && nodes[i].position.x) || 0,
        y: (nodes[i].position && nodes[i].position.y) || 0
      });
    }

    gesture = {
      kind: "drag",
      x: event.clientX,
      y: event.clientY,
      origins: origins,
      moved: false
    };
    graph.classList.add("dragging");
    event.preventDefault();
    event.stopPropagation();
  }

  function moveDrag(event) {
    var state = shell.state;
    var scale = state.zoom;
    var dx = (event.clientX - gesture.x) / scale;
    var dy = (event.clientY - gesture.y) / scale;

    // A press that never moves must not count as a move. Otherwise a plain
    // click would send a document message per node and mark the file dirty.
    if (Math.abs(event.clientX - gesture.x) > 1 || Math.abs(event.clientY - gesture.y) > 1) {
      gesture.moved = true;
    }

    var touched = {};
    for (var i = 0; i < gesture.origins.length; i++) {
      var origin = gesture.origins[i];
      origin.node.position.x = origin.x + dx;
      origin.node.position.y = origin.y + dy;

      var entry = elements[origin.node.id];
      if (entry) {
        entry.element.style.left = origin.node.position.x + "px";
        entry.element.style.top = origin.node.position.y + "px";
        entry.x = origin.node.position.x;
        entry.y = origin.node.position.y;
      }
      touched[origin.node.id] = true;
    }

    // Redraw the edges so they follow, and move the port dots of the nodes the
    // drag can have changed the direction of — the dragged ones and whatever
    // they connect to. Recomputing every port on every mouse move would walk
    // the whole story per frame, which is the cost this file exists to avoid.
    var affected = affectedBy(touched);
    placePorts(state, affected);
    renderEdges(state);
  }

  function endDrag() {
    if (!gesture.moved) {
      gesture = null;
      graph.classList.remove("dragging");
      return;
    }

    // One message per node when the drag ends, not one per mouse move. The
    // bridge carries one node per message, so a group move is several — sent
    // once, at the end, rather than sixty times a second for the gesture.
    for (var i = 0; i < gesture.origins.length; i++) {
      var node = gesture.origins[i].node;
      shell.post({
        type: "moveNode",
        id: node.id,
        x: node.position.x,
        y: node.position.y
      });
    }
    gesture = null;
    graph.classList.remove("dragging");
  }

  /// The nodes whose port sides a change to `touched` can have flipped.
  function affectedBy(touched) {
    var doc = shell.state.document;
    var affected = {};
    var id;

    for (id in touched) {
      if (touched.hasOwnProperty(id)) { affected[id] = true; }
    }

    var edges = (doc && doc.edges) || [];
    for (var i = 0; i < edges.length; i++) {
      if (touched[edges[i].from.nodeId]) { affected[edges[i].to.nodeId] = true; }
      if (touched[edges[i].to.nodeId]) { affected[edges[i].from.nodeId] = true; }
    }
    return affected;
  }

  // --- rubber-band selection ----------------------------------------------

  function beginMarquee(event) {
    var world = graphToWorld(event.clientX, event.clientY);
    gesture = {
      kind: "marquee",
      start: world,
      additive: event.shiftKey || event.ctrlKey || event.metaKey,
      base: selected.slice()
    };
    event.preventDefault();
  }

  function moveMarquee(event) {
    var state = shell.state;
    var world = graphToWorld(event.clientX, event.clientY);
    var rect = marqueeRect(gesture.start, world);

    if (!marquee) {
      marquee = document.createElement("div");
      marquee.className = "marquee";
      viewport.appendChild(marquee);
    }
    marquee.style.left = rect.left + "px";
    marquee.style.top = rect.top + "px";
    marquee.style.width = (rect.right - rect.left) + "px";
    marquee.style.height = (rect.bottom - rect.top) + "px";

    // The set updates while the band is dragged, so the user sees what is
    // about to be selected rather than finding out on release.
    var ids = gesture.additive ? gesture.base.slice() : [];
    var nodes = (state.document && state.document.nodes) || [];
    for (var i = 0; i < nodes.length; i++) {
      if (intersects(rect, nodeRect(nodes[i]))) {
        if (ids.indexOf(nodes[i].id) < 0) { ids.push(nodes[i].id); }
      }
    }

    // applySelection asks for a render, and a render is a full pass over the
    // document. During a drag only the selection classes change, so they are
    // written here and the shell is told without the render.
    selected = ids;
    selectedSet = {};
    for (var j = 0; j < ids.length; j++) { selectedSet[ids[j]] = true; }
    anchorId = ids[0] || null;
    paintSelection();
  }

  function endMarquee() {
    // The set was updated live; this is where the shell and the other areas
    // are told about it.
    applyingSelection = true;
    shell.select(anchorId);
    applyingSelection = false;
    shell.render();
    clearMarquee();
    gesture = null;
  }

  function marqueeRect(a, b) {
    return {
      left: Math.min(a.x, b.x),
      top: Math.min(a.y, b.y),
      right: Math.max(a.x, b.x),
      bottom: Math.max(a.y, b.y)
    };
  }

  function intersects(a, b) {
    return a.left <= b.right && a.right >= b.left &&
           a.top <= b.bottom && a.bottom >= b.top;
  }

  function clearMarquee() {
    if (marquee && marquee.parentNode) {
      marquee.parentNode.removeChild(marquee);
    }
    marquee = null;
  }

  /// Write the selection classes without a full render.
  function paintSelection() {
    for (var id in elements) {
      if (elements.hasOwnProperty(id)) {
        applySelectionClass(elements[id].element, selectedSet[id] === true);
      }
    }
  }

  // --- connection dragging -------------------------------------------------
  //
  // The rules here are the host's rules, not this file's opinions. Every
  // refusal below is a document state the host's own Validate() reports as an
  // error, so refusing it in the gesture is what stops the user from being
  // able to draw a document the host would then complain about. A rule that
  // lived only here would disappear the moment the file was edited by hand.

  var connect = null;     // { from: {nodeId, portId}, kind, preview }
  var previewPath = null;

  /// Why an edge from `from` to `to` would be refused, or an empty string.
  function refusalReason(fromNode, fromPort, toNode, toPort) {
    if (fromNode.id === toNode.id && fromPort.id === toPort.id) {
      return "A port cannot connect to itself.";
    }
    if (fromPort.kind !== "output" || toPort.kind !== "input") {
      return "Edges run from an output to an input.";
    }

    var doc = shell.state.document;
    var edges = (doc && doc.edges) || [];
    var sameInput = 0;

    for (var i = 0; i < edges.length; i++) {
      var edge = edges[i];
      if (edge.from.nodeId === fromNode.id && edge.from.portId === fromPort.id &&
          edge.to.nodeId === toNode.id && edge.to.portId === toPort.id) {
        return "That connection already exists.";
      }
      if (edge.to.nodeId === toNode.id && edge.to.portId === toPort.id) {
        sameInput++;
      }
    }

    if (sameInput > 0 && !toPort.multiple) {
      return "Port \"" + (toPort.label || toPort.id) + "\" already has a connection.";
    }
    return "";
  }

  /// Every port's position on screen, measured once at the start of a drag.
  ///
  /// Hit-testing cannot use `elementFromPoint`: the port layer sits inside a
  /// scaled and translated container, so the browser answers in viewport
  /// coordinates and a dot's own box has to be read the same way. Measuring
  /// the boxes once is both exact and cheap — the nodes cannot move during a
  /// connection drag, so the snapshot stays valid for the whole gesture, and
  /// a mousemove costs a scan of a flat array instead of a walk of the graph.
  function snapshotPorts() {
    var nodes = (shell.state.document && shell.state.document.nodes) || [];
    var found = [];

    for (var i = 0; i < nodes.length; i++) {
      var entry = elements[nodes[i].id];
      if (!entry) { continue; }

      var dots = entry.element.querySelectorAll(".port");
      for (var j = 0; j < dots.length; j++) {
        var port = shell.findPort(nodes[i], dots[j].dataset.port);
        if (!port) { continue; }
        found.push({
          node: nodes[i],
          port: port,
          element: dots[j],
          box: dots[j].getBoundingClientRect()
        });
      }
    }
    return found;
  }

  /// The port under a point, or null.
  function portAt(clientX, clientY) {
    var ports = connect ? connect.ports : null;
    if (!ports) { return null; }

    for (var i = 0; i < ports.length; i++) {
      var box = ports[i].box;
      // A dot is 10px before zoom and the box scales with it, so the slop
      // comes from the box. The minimum keeps a port grabbable when the graph
      // is zoomed far out, where the box is a few pixels across.
      var slopX = Math.max(3, box.width / 2);
      var slopY = Math.max(3, box.height / 2);
      var cx = box.left + box.width / 2;
      var cy = box.top + box.height / 2;

      if (Math.abs(clientX - cx) <= slopX && Math.abs(clientY - cy) <= slopY) {
        return ports[i];
      }
    }
    return null;
  }

  function beginConnect(portInfo, event) {
    connect = {
      from: { nodeId: portInfo.node.id, portId: portInfo.port.id },
      // An output drags forwards to an input; an input drags backwards to an
      // output. Both are the same gesture from the user's point of view.
      fromKind: portInfo.port.kind,
      fromPort: portInfo.port,
      fromNode: portInfo.node,
      ports: snapshotPorts(),
      target: null,
      reason: ""
    };
    graph.classList.add("connecting");
    event.preventDefault();
    event.stopPropagation();
  }

  function moveConnect(event) {
    var state = shell.state;
    var target = portAt(event.clientX, event.clientY);
    var reason = "";

    if (target) {
      reason = connect.fromKind === "output"
        ? refusalReason(connect.fromNode, connect.fromPort, target.node, target.port)
        : refusalReason(target.node, target.port, connect.fromNode, connect.fromPort);
      // A drag that started at an output cannot land on another output, and
      // the message for that is the same one the host would give.
      if (connect.fromKind === "input" && target.port.kind !== "output") {
        reason = "Edges run from an output to an input.";
      }
    }

    setConnectTarget(target, reason);
    drawConnectPreview(event.clientX, event.clientY, reason !== "");
  }

  /// Mark the port under the cursor as a valid or refused target.
  ///
  /// The refusal has to be visible during the gesture and not only on release:
  /// a drag that ends in nothing with no explanation reads as a broken drag.
  function setConnectTarget(target, reason) {
    if (connect.target && connect.target.element) {
      connect.target.element.classList.remove("valid", "invalid");
    }
    connect.target = null;
    connect.reason = reason;

    if (target) {
      target.element.classList.add(reason ? "invalid" : "valid");
      connect.target = target;
    }
  }

  function drawConnectPreview(clientX, clientY, refused) {
    var state = shell.state;
    if (!previewPath) {
      previewPath = document.createElementNS("http://www.w3.org/2000/svg", "path");
      previewPath.setAttribute("class", "preview");
      previewPath.setAttribute("id", "connect-preview");
    }
    // renderEdges rebuilds the layer with innerHTML, so the preview has to be
    // put back rather than assumed to have survived.
    if (previewPath.parentNode !== edgesSvg) {
      edgesSvg.appendChild(previewPath);
    }

    var from = connect.fromKind === "output"
      ? portPoint(connect.fromNode, connect.from.portId, "out", "right")
      : portPoint(connect.fromNode, connect.from.portId, "in", "left");

    var world = graphToWorld(clientX, clientY);
    var to = connect.target
      ? portPoint(connect.target.node, connect.target.port.id,
                  connect.target.port.kind === "input" ? "in" : "out",
                  connect.target.port.kind === "input" ? "left" : "right")
      : world;

    var backward = from.x > to.x - BACKWARD_GAP;
    previewPath.setAttribute("d", curveBetween(from, to, backward));
    previewPath.classList.toggle("refused", refused);
  }

  function endConnect() {
    var state = shell.state;

    if (connect.target && !connect.reason) {
      var fromNode = connect.fromKind === "output" ? connect.fromNode : connect.target.node;
      var fromPort = connect.fromKind === "output" ? connect.fromPort : connect.target.port;
      var toNode = connect.fromKind === "output" ? connect.target.node : connect.fromNode;
      var toPort = connect.fromKind === "output" ? connect.target.port : connect.fromPort;

      // Field names match the host's handler, which reads fromNode, fromPort,
      // toNode and toPort. The protocol table in docs/ARCHITECTURE.md says
      // `from` and `to`; the host is what the message has to satisfy.
      shell.post({
        type: "connect",
        fromNode: fromNode.id,
        fromPort: fromPort.id,
        toNode: toNode.id,
        toPort: toPort.id
      });
    } else if (connect.reason) {
      shell.showError(connect.reason);
    }

    if (connect.target && connect.target.element) {
      connect.target.element.classList.remove("valid", "invalid");
    }
    if (previewPath && previewPath.parentNode) {
      previewPath.parentNode.removeChild(previewPath);
    }
    previewPath = null;
    connect = null;
    graph.classList.remove("connecting");
  }

  // --- keyboard navigation -------------------------------------------------
  //
  // Arrows change what is selected rather than moving the nodes. The rest of
  // the gesture set is navigation — Tab to the next node, Enter to the
  // inspector — and an arrow that nudged positions would make the graph a
  // layout tool by accident, with no way to undo a stray press.

  /// The nearest node in a direction from the anchor.
  ///
  /// Scored by distance along the axis plus twice the offset across it, so a
  /// node straight to the right beats a closer one that is mostly below.
  function nodeInDirection(dx, dy) {
    var state = shell.state;
    var nodes = (state.document && state.document.nodes) || [];
    if (nodes.length === 0) { return null; }

    var from = anchorId ? nodeById(anchorId) : null;
    if (!from) {
      // Nothing selected: the first arrow press selects rather than moving.
      return nodes[0];
    }

    var origin = nodeRect(from);
    var ox = (origin.left + origin.right) / 2;
    var oy = (origin.top + origin.bottom) / 2;

    var best = null;
    var bestScore = Infinity;

    for (var i = 0; i < nodes.length; i++) {
      if (nodes[i].id === from.id) { continue; }
      var rect = nodeRect(nodes[i]);
      var cx = (rect.left + rect.right) / 2;
      var cy = (rect.top + rect.bottom) / 2;

      var along = (cx - ox) * dx + (cy - oy) * dy;
      if (along <= 1) { continue; }

      var across = Math.abs((cx - ox) * dy - (cy - oy) * dx);
      var score = along + across * 2;
      if (score < bestScore) {
        bestScore = score;
        best = nodes[i];
      }
    }
    return best;
  }

  function stepSelection(offset) {
    var state = shell.state;
    var nodes = (state.document && state.document.nodes) || [];
    if (nodes.length === 0) { return; }

    var index = 0;
    for (var i = 0; i < nodes.length; i++) {
      if (nodes[i].id === anchorId) { index = i; break; }
    }
    index = (index + offset + nodes.length) % nodes.length;
    selectOne(nodes[index].id);
  }

  /// Put the caret in the inspector.
  ///
  /// The inspector owns its own fields, so this only reaches for the first
  /// focusable one. It reads no state from that area, and does nothing when
  /// the panel has no fields to offer.
  function focusInspector() {
    var body = document.getElementById("inspector");
    if (!body) { return false; }
    var field = body.querySelector("input:not([disabled]), textarea, select");
    if (!field) { return false; }
    field.focus();
    if (field.select) { field.select(); }
    return true;
  }

  /// Frame the graph in the window.
  function fitToWindow() {
    var state = shell.state;
    var nodes = (state.document && state.document.nodes) || [];
    if (nodes.length === 0) { return; }

    var minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
    for (var i = 0; i < nodes.length; i++) {
      var rect = nodeRect(nodes[i]);
      minX = Math.min(minX, rect.left);
      minY = Math.min(minY, rect.top);
      maxX = Math.max(maxX, rect.right);
      maxY = Math.max(maxY, rect.bottom);
    }

    var bounds = graph.getBoundingClientRect();
    var margin = 60;
    var scale = Math.min(
      (bounds.width - margin * 2) / Math.max(1, maxX - minX),
      (bounds.height - margin * 2) / Math.max(1, maxY - minY)
    );
    state.zoom = Math.min(1.5, Math.max(0.15, scale));

    state.pan.x = bounds.width / 2 - ((minX + maxX) / 2) * state.zoom;
    state.pan.y = bounds.height / 2 - ((minY + maxY) / 2) * state.zoom;
    applyTransform();
  }

  /// Place a new node where the user is looking, not at the document origin,
  /// which may be far off screen.
  function addNodeAtViewCentre(type) {
    var state = shell.state;
    var bounds = graph.getBoundingClientRect();
    var x = (bounds.width / 2 - state.pan.x) / state.zoom - NODE_WIDTH / 2;
    var y = (bounds.height / 2 - state.pan.y) / state.zoom - 40;
    shell.post({ type: "addNode", nodeType: type, x: x, y: y });
  }

  function removeSelected() {
    // One message per node, at the moment the key is pressed rather than per
    // frame. The bridge removes one node per message; a group delete is
    // therefore several, which is the same trade the group move makes.
    var ids = selected.slice();
    if (ids.length === 0) { return; }
    for (var i = 0; i < ids.length; i++) {
      shell.post({ type: "removeNode", id: ids[i] });
    }
    selectOne(null);
  }

  function inField(event) {
    var tag = event.target.tagName;
    return tag === "INPUT" || tag === "TEXTAREA" || tag === "SELECT" ||
           event.target.isContentEditable === true;
  }

  // --- wiring --------------------------------------------------------------

  function onMouseDown(event) {
    // Only the primary button starts a selection gesture; the others pan or
    // are reserved. A right-drag that selected would be a surprise.
    if (event.button !== 0 && event.button !== 1) { return; }

    var port = event.target.closest ? event.target.closest(".port") : null;
    if (port && event.button === 0) {
      var node = nodeById(port.dataset.node);
      var info = node ? shell.findPort(node, port.dataset.port) : null;
      if (info) {
        beginConnect({ node: node, port: info, element: port }, event);
      }
      return;
    }

    var nodeElement = event.target.closest ? event.target.closest(".node") : null;
    if (nodeElement && event.button === 0) {
      beginDrag(nodeElement, event);
      return;
    }

    var edgeElement = event.target.closest ? event.target.closest(".edge") : null;
    if (edgeElement && event.button === 0) {
      // Clicking an edge removes it. Deleting is the only thing a click on a
      // line can mean, and it saves the user from hunting for a menu.
      shell.post({ type: "disconnect", id: edgeElement.dataset.id });
      event.preventDefault();
      return;
    }

    if (event.button === 1) {
      // The middle button pans. Panning is on the middle button and on a held
      // space bar rather than on the left button, because the left button on
      // empty space is the rubber band and one button cannot do both.
      middlePanning = true;
      beginPan(event);
      return;
    }

    if (spaceHeld) {
      beginPan(event);
      return;
    }

    // Empty space with the left button: the rubber band.
    beginMarquee(event);
  }

  // Middle button and space both pan, so that a trackpad without a middle
  // button can still pan. Tracked on the window rather than on the graph,
  // because the pointer leaves the graph during a pan.
  var middlePanning = false;
  var spaceHeld = false;

  function onMouseMove(event) {
    if (!gesture && !connect) { return; }

    if (gesture) {
      if (gesture.kind === "pan") {
        var state = shell.state;
        state.pan.x = gesture.px + (event.clientX - gesture.x);
        state.pan.y = gesture.py + (event.clientY - gesture.y);
        applyTransform();
      } else if (gesture.kind === "drag") {
        moveDrag(event);
      } else if (gesture.kind === "marquee") {
        moveMarquee(event);
      }
    }

    if (connect) {
      moveConnect(event);
    }
  }

  function onMouseUp(event) {
    if (event && event.button === 1) { middlePanning = false; }
    if (gesture) {
      if (gesture.kind === "pan") {
        graph.classList.remove("panning");
        gesture = null;
      } else if (gesture.kind === "drag") {
        endDrag();
      } else if (gesture.kind === "marquee") {
        endMarquee();
      }
    }
    if (connect) {
      endConnect();
    }
  }

  function onKeyDown(event) {
    if (inField(event)) { return; }

    // The space bar is the pan modifier. It is tracked rather than acted on,
    // because a held modifier is not a command; the press that uses it is a
    // mouse press.
    if (event.key === " ") { spaceHeld = true; return; }

    if (event.key === "Escape") {
      if (connect) {
        if (connect.target && connect.target.element) {
          connect.target.element.classList.remove("valid", "invalid");
        }
        if (previewPath && previewPath.parentNode) {
          previewPath.parentNode.removeChild(previewPath);
        }
        previewPath = null;
        connect = null;
        graph.classList.remove("connecting");
        event.preventDefault();
      } else if (gesture && gesture.kind === "marquee") {
        clearMarquee();
        gesture = null;
        shell.render();
        event.preventDefault();
      }
      return;
    }

    if (event.key === "Delete" || event.key === "Backspace") {
      if (selected.length > 0) {
        removeSelected();
        event.preventDefault();
      }
      return;
    }

    if (event.key === "Tab") {
      // Shift+Tab is the same step backwards. Wrapping at both ends, so the
      // keyboard alone can reach every node in the story.
      stepSelection(event.shiftKey ? -1 : 1);
      event.preventDefault();
      return;
    }

    if (event.key === "Enter") {
      if (focusInspector()) { event.preventDefault(); }
      return;
    }

    var step = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] };
    var direction = step[event.key];
    if (!direction) { return; }

    // Shift extends: the node found becomes part of the set rather than
    // replacing it, which is how a run of nodes is picked without the mouse.
    if (event.shiftKey) {
      var next = nodeInDirection(direction[0], direction[1]);
      if (next && !isSelected(next.id)) { toggleSelected(next.id); }
    } else {
      var found = nodeInDirection(direction[0], direction[1]);
      if (found) { selectOne(found.id); }
    }
    event.preventDefault();
  }

  function onWheel(event) {
    event.preventDefault();

    var state = shell.state;
    var bounds = graph.getBoundingClientRect();
    var mx = event.clientX - bounds.left;
    var my = event.clientY - bounds.top;

    var factor = event.deltaY < 0 ? 1.1 : 1 / 1.1;
    var next = Math.min(4, Math.max(0.15, state.zoom * factor));
    var applied = next / state.zoom;

    state.pan.x = mx - (mx - state.pan.x) * applied;
    state.pan.y = my - (my - state.pan.y) * applied;
    state.zoom = next;

    applyTransform();
  }

  function wire() {
    graph.addEventListener("mousedown", onMouseDown);
    window.addEventListener("mousemove", onMouseMove);
    window.addEventListener("mouseup", onMouseUp);
    graph.addEventListener("wheel", onWheel, { passive: false });
    window.addEventListener("keydown", onKeyDown);
    window.addEventListener("keyup", function (event) {
      if (event.key === " ") { spaceHeld = false; }
    });
    // A window that loses focus never delivers the keyup, and a pan modifier
    // that is stuck down turns every later click into a pan.
    window.addEventListener("blur", function () {
      spaceHeld = false;
      middlePanning = false;
    });

    // A middle press is a pan, and the browser would otherwise start its own
    // scroll gesture on it and never deliver the mouseup.
    graph.addEventListener("auxclick", function (event) {
      if (event.button === 1) { event.preventDefault(); }
    });

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
    document.getElementById("delete").addEventListener("click", removeSelected);
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
      stats.frames++;
      renderNodes(state);
      placePorts(state, null);
      renderEdges(state);
      applyTransform();
    },

    onSelectionChanged: function (state) {
      // The host, or the inspector, changed the shell's selection. Adopting it
      // as the whole set is what keeps the canvas from showing three selected
      // nodes while the inspector edits a fourth.
      if (applyingSelection) { return; }
      if (state.selectedId === anchorId) { return; }
      selected = state.selectedId ? [state.selectedId] : [];
      selectedSet = {};
      if (state.selectedId) { selectedSet[state.selectedId] = true; }
      anchorId = state.selectedId;
    }
  });

  // --- the measurement surface --------------------------------------------
  //
  // The page has no console a developer can read from the host, so the numbers
  // are read through a global. `storynode.canvas.stats()` is what the
  // performance claim in the pull request is measured with; nothing in the
  // interface depends on it.

  window.storynode = window.storynode || {};
  window.storynode.canvas = {
    stats: function () {
      var copy = {};
      for (var key in stats) {
        if (stats.hasOwnProperty(key)) { copy[key] = stats[key]; }
      }
      copy.nodes = 0;
      for (var id in elements) {
        if (elements.hasOwnProperty(id)) { copy.nodes++; }
      }
      copy.selected = selected.length;
      return copy;
    },
    resetStats: function () {
      for (var key in stats) {
        if (stats.hasOwnProperty(key)) { stats[key] = 0; }
      }
    },
    /// Select by id, for a script driving the page. The same path the mouse
    /// takes, so a measurement exercises the real code.
    select: function (ids) {
      if (typeof ids === "string") { selectOne(ids); }
      else { applySelection(ids || [], null); }
    },
    fit: fitToWindow
  };
})();
