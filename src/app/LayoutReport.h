// ---------------------------------------------------------------------------
// Layout verification: measuring the interface and judging the result.
//
// Runs in CI to prove the interface actually loaded and laid out, rather than
// only that the process started and did not crash. The distinction matters:
// a WebView2 host that fails to navigate, fails to create the controller, or
// loads a page whose script throws will still show a window and still stay
// running. Without a check like this, CI passes on a blank white rectangle.
//
// The geometry is gathered by the page itself, in the page's own coordinate
// space, and sent back through the same bridge the editor uses. So the check
// also exercises the bridge end to end: page script, postMessage, host
// handler, reply.
//
// This file has no Windows dependency. The page's report arrives as text and
// is judged as text, which is what lets the judging be unit-tested — see
// tests/VerifyTests.cpp — rather than only observed by running the app.
// ---------------------------------------------------------------------------

#pragma once

#include <string>

namespace storynode {

/// The script the page runs to measure itself.
///
/// Kept here rather than in the HTML so the interface file stays about the
/// interface, and so a change to what is measured is a change in one place.
const wchar_t* LayoutProbeScript();

/// Format a layout report for a human to read.
///
/// The returned text ends in "PASS" or "FAIL", which is what the caller keys
/// on. It describes a failure when a required band of the interface is
/// missing, has no size, or overlaps its neighbour.
std::string FormatLayoutReport(const std::string& pageReport);

/// True when a formatted report represents a pass. One place decides, so the
/// caller and the tests cannot disagree about what "PASS" means.
bool LayoutReportPassed(const std::string& formattedReport);

} // namespace storynode
