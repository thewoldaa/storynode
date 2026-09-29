// ---------------------------------------------------------------------------
// Tests for the verification report formatter.
//
// The formatter is what turns the page's self-measurement into a pass or fail,
// and it runs in CI. If it accepts a broken layout, CI is green on a blank
// window; if it rejects a good one, CI is red for no reason and gets ignored.
// Both failures are worse than having no check, so the cases are pinned here.
//
// These run with no window and no WebView2: they take the report as text,
// which is what the page sends over the bridge.
// ---------------------------------------------------------------------------

#include "TestFramework.h"

#include "app/LayoutReport.h"

using namespace storynode;

namespace {

/// A report describing a correctly laid out interface.
const char* kGoodReport = R"({
  "type": "layoutReport",
  "viewport": { "width": 1010, "height": 583 },
  "toolbar": { "top": 0, "bottom": 37, "left": 0, "right": 1010,
               "width": 1010, "height": 37, "display": "flex", "visible": true },
  "graph":   { "top": 37, "bottom": 558, "left": 0, "right": 1010,
               "width": 1010, "height": 521, "display": "block", "visible": true },
  "footer":  { "top": 558, "bottom": 583, "left": 0, "right": 1010,
               "width": 1010, "height": 25, "display": "flex", "visible": true },
  "undo": { "width": 60, "height": 24, "visible": true },
  "redo": { "width": 60, "height": 24, "visible": true },
  "undoDisabled": true, "redoDisabled": true, "areas": 2,
  "nodes": 0, "ports": 0, "buttons": 7
})";

bool Passed(const std::string& text)
{
    return LayoutReportPassed(text);
}

} // namespace

TEST(AcceptsACorrectLayout)
{
    const std::string report = FormatLayoutReport(kGoodReport);
    CHECK(Passed(report));
    CHECK(report.find("footer: top=558") != std::string::npos);
}

TEST(RejectsAMissingFooter)
{
    // The case this check exists for: the grid gives the graph row the whole
    // height and the footer is pushed off the bottom. The window still opens
    // and the process still runs, so nothing else would catch it.
    const std::string report = FormatLayoutReport(R"({
      "type": "layoutReport",
      "viewport": { "width": 1010, "height": 583 },
      "toolbar": { "top": 0, "bottom": 37, "width": 1010, "height": 37, "visible": true },
      "graph":   { "top": 37, "bottom": 583, "width": 1010, "height": 546, "visible": true },
  "undo": { "width": 60, "height": 24, "visible": true },
  "redo": { "width": 60, "height": 24, "visible": true },
  "undoDisabled": true, "redoDisabled": true, "areas": 2,
  "nodes": 0, "ports": 0, "buttons": 7
    })");

    CHECK_FALSE(Passed(report));
    CHECK(report.find("footer: MISSING") != std::string::npos);
}

TEST(RejectsAZeroHeightFooter)
{
    // Present in the DOM but collapsed to nothing, which is what happens when
    // a grid row shrinks below its content.
    const std::string report = FormatLayoutReport(R"({
      "type": "layoutReport",
      "viewport": { "width": 1010, "height": 583 },
      "toolbar": { "top": 0, "bottom": 37, "width": 1010, "height": 37, "visible": true },
      "graph":   { "top": 37, "bottom": 583, "width": 1010, "height": 546, "visible": true },
      "footer":  { "top": 583, "bottom": 583, "width": 1010, "height": 0,
                   "display": "flex", "visible": false },
  "undo": { "width": 60, "height": 24, "visible": true },
  "redo": { "width": 60, "height": 24, "visible": true },
  "undoDisabled": true, "redoDisabled": true, "areas": 2,
  "nodes": 0, "ports": 0, "buttons": 7
    })");

    CHECK_FALSE(Passed(report));
    CHECK(report.find("NOT VISIBLE") != std::string::npos);
}

TEST(RejectsAFooterThatOverlapsTheGraph)
{
    // The footer is drawn, but on top of the graph rather than below it. A
    // user would see it floating over the middle of the canvas.
    const std::string report = FormatLayoutReport(R"({
      "type": "layoutReport",
      "viewport": { "width": 1010, "height": 583 },
      "toolbar": { "top": 0, "bottom": 37, "width": 1010, "height": 37, "visible": true },
      "graph":   { "top": 37, "bottom": 583, "width": 1010, "height": 546, "visible": true },
      "footer":  { "top": 400, "bottom": 425, "width": 1010, "height": 25, "visible": true },
  "undo": { "width": 60, "height": 24, "visible": true },
  "redo": { "width": 60, "height": 24, "visible": true },
  "undoDisabled": true, "redoDisabled": true, "areas": 2,
  "nodes": 0, "ports": 0, "buttons": 7
    })");

    CHECK_FALSE(Passed(report));
    CHECK(report.find("overlaps the graph") != std::string::npos);
}

TEST(RejectsAGraphThatOverlapsTheToolbar)
{
    const std::string report = FormatLayoutReport(R"({
      "type": "layoutReport",
      "viewport": { "width": 1010, "height": 583 },
      "toolbar": { "top": 0, "bottom": 37, "width": 1010, "height": 37, "visible": true },
      "graph":   { "top": 10, "bottom": 583, "width": 1010, "height": 573, "visible": true },
      "footer":  { "top": 558, "bottom": 583, "width": 1010, "height": 25, "visible": true },
  "undo": { "width": 60, "height": 24, "visible": true },
  "redo": { "width": 60, "height": 24, "visible": true },
  "undoDisabled": true, "redoDisabled": true, "areas": 2,
  "nodes": 0, "ports": 0, "buttons": 7
    })");

    CHECK_FALSE(Passed(report));
    CHECK(report.find("overlaps the toolbar") != std::string::npos);
}

TEST(RejectsAMissingToolbar)
{
    const std::string report = FormatLayoutReport(R"({
      "type": "layoutReport",
      "viewport": { "width": 1010, "height": 583 },
      "graph":   { "top": 0, "bottom": 583, "width": 1010, "height": 583, "visible": true },
      "footer":  { "top": 558, "bottom": 583, "width": 1010, "height": 25, "visible": true },
  "undo": { "width": 60, "height": 24, "visible": true },
  "redo": { "width": 60, "height": 24, "visible": true },
  "undoDisabled": true, "redoDisabled": true, "areas": 2,
  "nodes": 0, "ports": 0, "buttons": 7
    })");

    CHECK_FALSE(Passed(report));
    CHECK(report.find("toolbar: MISSING") != std::string::npos);
}

TEST(RejectsAToolbarWithNoButtons)
{
    // The toolbar is the right size but its contents did not render, which is
    // what a script error partway through the page looks like.
    const std::string report = FormatLayoutReport(R"({
      "type": "layoutReport",
      "viewport": { "width": 1010, "height": 583 },
      "toolbar": { "top": 0, "bottom": 37, "width": 1010, "height": 37, "visible": true },
      "graph":   { "top": 37, "bottom": 558, "width": 1010, "height": 521, "visible": true },
      "footer":  { "top": 558, "bottom": 583, "width": 1010, "height": 25, "visible": true },
  "undo": { "width": 60, "height": 24, "visible": true },
  "redo": { "width": 60, "height": 24, "visible": true },
  "undoDisabled": true, "redoDisabled": true, "areas": 2,
  "nodes": 0, "ports": 0, "buttons": 0
    })");

    CHECK_FALSE(Passed(report));
    CHECK(report.find("did not render its buttons") != std::string::npos);
}

TEST(RejectsAReportThatIsNotJson)
{
    const std::string report = FormatLayoutReport("this is not json");
    CHECK(report.find("FAIL") != std::string::npos);
}

TEST(RejectsAReportOfTheWrongType)
{
    // The bridge delivers several message types; a document message must not
    // be mistaken for a layout report and pass.
    const std::string report = FormatLayoutReport(R"({"type":"document"})");
    CHECK(report.find("FAIL") != std::string::npos);
    CHECK(report.find("layoutReport") != std::string::npos);
}

TEST(AcceptsATightButValidLayout)
{
    // One pixel of rounding between the toolbar's bottom and the graph's top
    // is tolerated, because sub-pixel layout legitimately produces it. The
    // check must not fail on rounding.
    const std::string report = FormatLayoutReport(R"({
      "type": "layoutReport",
      "viewport": { "width": 900, "height": 500 },
      "toolbar": { "top": 0, "bottom": 37, "width": 900, "height": 37, "visible": true },
      "graph":   { "top": 36, "bottom": 475, "width": 900, "height": 439, "visible": true },
      "footer":  { "top": 474, "bottom": 499, "width": 900, "height": 25, "visible": true },
  "undo": { "width": 60, "height": 24, "visible": true },
  "redo": { "width": 60, "height": 24, "visible": true },
  "undoDisabled": true, "redoDisabled": true, "areas": 2,
  "nodes": 0, "ports": 0, "buttons": 7
    })");

    CHECK(Passed(report));
}

// --- the undo and redo controls ---------------------------------------------
//
// These exist because a page whose script threw partway through still lays out
// and still shows a toolbar, so counting buttons is not enough to know that
// the controls are there and that the host's state reached them.

TEST(RejectsAMissingUndoControl)
{
    const std::string report = FormatLayoutReport(R"({
      "type": "layoutReport",
      "viewport": { "width": 1010, "height": 583 },
      "toolbar": { "top": 0, "bottom": 37, "width": 1010, "height": 37, "visible": true },
      "graph":   { "top": 37, "bottom": 558, "width": 1010, "height": 521, "visible": true },
      "footer":  { "top": 558, "bottom": 583, "width": 1010, "height": 25, "visible": true },
      "redo": { "width": 60, "height": 24, "visible": true },
      "undoDisabled": true, "redoDisabled": true, "areas": 2,
      "nodes": 0, "ports": 0, "buttons": 7
    })");

    CHECK_FALSE(Passed(report));
    CHECK(report.find("undo control") != std::string::npos);
}

TEST(RejectsAnUndoControlThatDidNotReadTheHostState)
{
    // The control is there and the layout is right, but it is enabled on a
    // document with no history. That means the page is deciding for itself
    // instead of using what the host sent, which is the failure the history
    // message exists to prevent.
    const std::string report = FormatLayoutReport(R"({
      "type": "layoutReport",
      "viewport": { "width": 1010, "height": 583 },
      "toolbar": { "top": 0, "bottom": 37, "width": 1010, "height": 37, "visible": true },
      "graph":   { "top": 37, "bottom": 558, "width": 1010, "height": 521, "visible": true },
      "footer":  { "top": 558, "bottom": 583, "width": 1010, "height": 25, "visible": true },
      "undo": { "width": 60, "height": 24, "visible": true },
      "redo": { "width": 60, "height": 24, "visible": true },
      "undoDisabled": false, "redoDisabled": true, "areas": 2,
      "nodes": 0, "ports": 0, "buttons": 7
    })");

    CHECK_FALSE(Passed(report));
    CHECK(report.find("undo is enabled") != std::string::npos);
}

TEST(RejectsAnAreaThatDidNotRegister)
{
    // An area whose script threw during parsing registers nothing, and the
    // page still lays out: the markup is all in the shell. A graph area with
    // nothing behind it looks exactly like a document with no nodes in it, so
    // the count is the only thing that tells them apart.
    const std::string report = FormatLayoutReport(R"({
      "type": "layoutReport",
      "viewport": { "width": 1010, "height": 583 },
      "toolbar": { "top": 0, "bottom": 37, "width": 1010, "height": 37, "visible": true },
      "graph":   { "top": 37, "bottom": 558, "width": 1010, "height": 521, "visible": true },
      "footer":  { "top": 558, "bottom": 583, "width": 1010, "height": 25, "visible": true },
      "undo": { "width": 60, "height": 24, "visible": true },
      "redo": { "width": 60, "height": 24, "visible": true },
      "undoDisabled": true, "redoDisabled": true, "areas": 1,
      "nodes": 0, "ports": 0, "buttons": 7
    })");

    CHECK_FALSE(Passed(report));
    CHECK(report.find("interface area") != std::string::npos);
}

TEST(RejectsAReportThatCouldNotCountTheAreas)
{
    // The shell itself is missing, which means the page threw before it
    // exposed anything. The probe reports -1 rather than a count, and that
    // must fail rather than be read as "fewer than two, but close enough".
    const std::string report = FormatLayoutReport(R"({
      "type": "layoutReport",
      "viewport": { "width": 1010, "height": 583 },
      "toolbar": { "top": 0, "bottom": 37, "width": 1010, "height": 37, "visible": true },
      "graph":   { "top": 37, "bottom": 558, "width": 1010, "height": 521, "visible": true },
      "footer":  { "top": 558, "bottom": 583, "width": 1010, "height": 25, "visible": true },
      "undo": { "width": 60, "height": 24, "visible": true },
      "redo": { "width": 60, "height": 24, "visible": true },
      "undoDisabled": true, "redoDisabled": true, "areas": -1,
      "nodes": 0, "ports": 0, "buttons": 7
    })");

    CHECK_FALSE(Passed(report));
}
