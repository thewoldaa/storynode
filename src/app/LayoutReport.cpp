// ---------------------------------------------------------------------------
// Layout verification: the probe script and the report formatter.
//
// No Windows dependency, so the judging can be unit-tested. See
// LayoutReport.h for why the check exists at all.
// ---------------------------------------------------------------------------

#include "app/LayoutReport.h"

#include <sstream>

#include "core/json/JsonValue.h"

namespace storynode {

const wchar_t* LayoutProbeScript()
{
    // Returns a JSON object. Every measurement is taken in the page's own
    // coordinate space, so it is independent of the window size, the display
    // scaling and the screenshot mechanism.
    return LR"JS(
(function () {
  function box(selector) {
    var element = document.querySelector(selector);
    if (!element) { return null; }
    var rect = element.getBoundingClientRect();
    var style = window.getComputedStyle(element);
    return {
      top: Math.round(rect.top),
      bottom: Math.round(rect.bottom),
      left: Math.round(rect.left),
      right: Math.round(rect.right),
      width: Math.round(rect.width),
      height: Math.round(rect.height),
      display: style.display,
      visible: rect.width > 0 && rect.height > 0 && style.display !== "none"
    };
  }

  var shell = window.storynode && window.storynode.shell;

  // --- the seam between the areas -----------------------------------------
  //
  // The layout checks above prove the shell laid out and the areas loaded.
  // Neither catches two areas that load and then disagree: the canvas
  // selecting three nodes while the inspector edits a fourth. That failure
  // exists only once both areas are written, so it needs a check that drives
  // one and reads the other.
  //
  // The canvas exposes select() for exactly this, and it takes the same path
  // the mouse does, so this exercises real code rather than a test hook.
  //
  // A document with three nodes is installed first. --verify opens an empty
  // story, and a selection test needs something to select.
  var seam = null;
  try {
    if (shell && window.storynode.canvas && window.storynode.canvas.select) {
      var probeDocument = {
        formatVersion: 1,
        id: "probe",
        title: "Probe",
        metadata: {},
        nodes: [
          { id: "p-a", type: "dialog", position: { x: 40, y: 40 }, size: { x: 220, y: 120 },
            ports: [ { id: "in", kind: "input" }, { id: "out", kind: "output" } ],
            data: { speaker: "A", text: "first" } },
          { id: "p-b", type: "dialog", position: { x: 340, y: 40 }, size: { x: 220, y: 120 },
            ports: [ { id: "in", kind: "input" }, { id: "out", kind: "output" } ],
            data: { speaker: "B", text: "second" } },
          { id: "p-c", type: "end", position: { x: 640, y: 40 }, size: { x: 220, y: 120 },
            ports: [ { id: "in", kind: "input" } ],
            data: {} }
        ],
        edges: []
      };

      // Through the shell's own handler, which is the path a document message
      // from the host takes.
      window.storynode.receive(JSON.stringify({ type: "document", document: probeDocument }));
      window.storynode.canvas.select(["p-a", "p-b"]);

      var inspectorBody = document.getElementById("inspector");
      // Every control, not just <input>. A choice field is a <select> and a
      // multi-line field is a <textarea>; counting only inputs reported one
      // field for a dialogue node that has three.
      var shown = inspectorBody
        ? inspectorBody.querySelectorAll("[data-key]")
        : [];

      seam = {
        // What the shell believes is selected.
        shellSelected: (shell.state.selectedIds || []).slice(),
        shellAnchor: shell.state.selectedId,
        // What the canvas drew as selected.
        drawnSelected: document.querySelectorAll(".node.selected").length,
        // What the inspector is showing. This is the part that was broken:
        // the canvas selected two, the inspector showed one, and nothing
        // failed because both were doing what they had been told.
        panelOpen: !!(document.getElementById("panel") ||
                      document.querySelector("aside")).classList.contains("open"),
        inspectorFields: shown.length,
        // With two nodes whose "speaker" differs, the field must say so rather
        // than showing one node's value.
        showsDifference: inspectorBody
          ? /multiple values/i.test(inspectorBody.textContent)
          : false
      };
    }
  } catch (e) {
    seam = { error: String(e && e.message ? e.message : e) };
  }

  var result = {
    type: "layoutReport",
    viewport: { width: window.innerWidth, height: window.innerHeight },
    toolbar: box("header.toolbar"),
    graph: box("main.graph"),
    footer: box("footer"),
    panel: box("aside"),
    undo: box("#undo"),
    redo: box("#redo"),
    undoDisabled: !!document.getElementById("undo") &&
                  document.getElementById("undo").disabled,
    redoDisabled: !!document.getElementById("redo") &&
                  document.getElementById("redo").disabled,
    // How many interface areas registered with the shell. An area whose script
    // threw during parsing registers nothing and the page still lays out,
    // because the markup is all in the shell — so a toolbar with no canvas
    // behind it looks exactly like a working one from the outside.
    areas: shell && shell.areaCount ? shell.areaCount() : -1,
    seam: seam,
    nodes: document.querySelectorAll(".node").length,
    ports: document.querySelectorAll(".port").length,
    buttons: document.querySelectorAll("button").length
  };

  window.chrome.webview.postMessage(JSON.stringify(result));
})();
)JS";
}

std::string FormatLayoutReport(const std::string& pageReport)
{
    const json::ParseResult parsed = json::Parse(pageReport);
    if (!parsed.ok)
    {
        return "FAIL: the page sent a report that is not valid JSON: " +
               parsed.Message() + "\n";
    }

    const json::Value& report = parsed.value;
    if (report["type"].AsString() != "layoutReport")
    {
        return "FAIL: expected a layoutReport, got \"" +
               report["type"].AsString() + "\"\n";
    }

    // A report can carry its own failure, which is how the host reports a
    // watchdog timeout: the reason travels the same path as a measurement, so
    // there is one place that formats a failure and one place that prints it.
    const std::string failure = report["failure"].AsString();
    if (!failure.empty())
    {
        return "FAIL: " + failure + "\n";
    }

    std::ostringstream out;
    out << "layout report\n";

    const json::Value& viewport = report["viewport"];
    out << "  viewport        " << viewport["width"].AsInt() << "x"
        << viewport["height"].AsInt() << "\n";
    // The three bands must be present and must not overlap. Checking the
    // measurements rather than eyeballing a screenshot is what makes this
    // usable in CI.
    const char* names[] = { "toolbar", "graph", "footer" };
    bool ok = true;

    for (const char* name : names)
    {
        const json::Value& box = report[name];
        if (box.IsNull())
        {
            out << "  " << name << ": MISSING\n";
            ok = false;
            continue;
        }

        const bool visible = box["visible"].AsBool();
        if (!visible)
        {
            out << "  " << name << ": NOT VISIBLE (width "
                << box["width"].AsInt() << ", height " << box["height"].AsInt() << ")\n";
            ok = false;
            continue;
        }

        out << "  " << name << ": top=" << box["top"].AsInt()
            << " bottom=" << box["bottom"].AsInt()
            << " h=" << box["height"].AsInt() << "\n";
    }

    const json::Value& toolbar = report["toolbar"];
    const json::Value& graph = report["graph"];
    const json::Value& footer = report["footer"];

    if (toolbar.IsObject() && graph.IsObject())
    {
        if (graph["top"].AsInt() < toolbar["bottom"].AsInt() - 1)
        {
            out << "  ERROR: the graph overlaps the toolbar\n";
            ok = false;
        }
    }

    if (graph.IsObject() && footer.IsObject())
    {
        if (footer["top"].AsInt() < graph["bottom"].AsInt() - 1)
        {
            out << "  ERROR: the footer overlaps the graph, so it is off screen\n";
            ok = false;
        }
    }

    out << "  nodes           " << report["nodes"].AsInt() << "\n";
    out << "  ports           " << report["ports"].AsInt() << "\n";
    out << "  buttons         " << report["buttons"].AsInt() << "\n";

    if (report["buttons"].AsInt() < 7)
    {
        out << "  ERROR: the toolbar did not render its buttons\n";
        ok = false;
    }

    // The undo and redo controls, and the state the host put them in. A page
    // whose script threw partway through still lays out and still shows a
    // toolbar, so the controls are checked by name rather than by counting
    // buttons alone.
    const json::Value& undo = report["undo"];
    if (undo.IsNull() || !undo["visible"].AsBool())
    {
        out << "  ERROR: the undo control is missing or collapsed\n";
        ok = false;
    }

    const json::Value& redo = report["redo"];
    if (redo.IsNull() || !redo["visible"].AsBool())
    {
        out << "  ERROR: the redo control is missing or collapsed\n";
        ok = false;
    }

    // Both start disabled, because a document that has just loaded has nothing
    // to undo. A page that rendered them enabled is not reading what the host
    // said, which is the failure the history message exists to prevent.
    if (!report["undoDisabled"].AsBool())
    {
        out << "  ERROR: undo is enabled on a document with no history\n";
        ok = false;
    }
    if (!report["redoDisabled"].AsBool())
    {
        out << "  ERROR: redo is enabled on a document with no history\n";
        ok = false;
    }

    // The areas that registered with the shell: the canvas and the inspector.
    //
    // A count rather than a name, because the shell does not know an area by
    // name and should not start. This is what catches an area whose script
    // threw: the page still lays out, the toolbar still renders, and the graph
    // area is simply empty, which from the outside looks like a document with
    // no nodes in it.
    const std::int64_t areas = report["areas"].AsInt(-1);
    out << "  areas           " << areas << "\n";
    if (areas < 2)
    {
        out << "  ERROR: only " << areas
            << " interface area(s) registered; the canvas and the inspector "
               "must both load\n";
        ok = false;
    }

    // --- the seam between the two areas ------------------------------------
    //
    // The checks above prove the shell laid out and the areas loaded. Neither
    // catches two areas that load and then disagree, which is the failure mode
    // parallel work produces: the canvas selecting two nodes while the
    // inspector shows one.
    //
    // This is the check that would have caught it. It selects two nodes
    // through the canvas and reads what the inspector shows.
    const json::Value& seam = report["seam"];
    if (seam.IsNull())
    {
        out << "  seam            NOT REPORTED\n";
        out << "  ERROR: the page did not run the selection check\n";
        ok = false;
    }
    else if (!seam["error"].AsString().empty())
    {
        out << "  seam            ERROR: " << seam["error"].AsString() << "\n";
        ok = false;
    }
    else
    {
        const std::int64_t shellSelected = seam["shellSelected"].Size();
        const std::int64_t drawn = seam["drawnSelected"].AsInt();
        const std::int64_t fields = seam["inspectorFields"].AsInt();
        const bool showsDifference = seam["showsDifference"].AsBool();

        out << "  seam selected   " << shellSelected << " (shell), "
            << drawn << " drawn, " << fields << " inspector field(s)\n";

        if (shellSelected != 2)
        {
            out << "  ERROR: selecting two nodes left " << shellSelected
                << " in the shell's selection\n";
            ok = false;
        }
        if (drawn != 2)
        {
            out << "  ERROR: the canvas drew " << drawn
                << " selected node(s), expected 2\n";
            ok = false;
        }
        if (fields == 0)
        {
            out << "  ERROR: the inspector showed nothing for a two-node "
                   "selection\n";
            ok = false;
        }
        if (!showsDifference)
        {
            // The acceptance criterion this whole check exists for. Two nodes
            // whose values differ must say so, not silently show one of them.
            out << "  ERROR: the inspector did not report differing values "
                   "across the selection\n";
            ok = false;
        }
    }

    out << (ok ? "PASS\n" : "FAIL\n");
    return out.str();
}

bool LayoutReportPassed(const std::string& formattedReport)
{
    // "PASS" must appear and "FAIL" must not. Checking both matters: the
    // report body quotes the measurements, and a value could contain the
    // word.
    return formattedReport.find("PASS") != std::string::npos &&
           formattedReport.find("FAIL") == std::string::npos;
}

} // namespace storynode
