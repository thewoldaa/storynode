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

  var result = {
    type: "layoutReport",
    viewport: { width: window.innerWidth, height: window.innerHeight },
    toolbar: box("header.toolbar"),
    graph: box("main.graph"),
    footer: box("footer"),
    panel: box("aside"),
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

    if (report["buttons"].AsInt() < 5)
    {
        out << "  ERROR: the toolbar did not render its buttons\n";
        ok = false;
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
