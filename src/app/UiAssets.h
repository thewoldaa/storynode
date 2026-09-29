// ---------------------------------------------------------------------------
// Loading the interface.
//
// The interface is split across files so that parallel tasks never edit the
// same one. The page cannot load them itself: it is delivered with
// NavigateToString, which gives it an opaque origin, so a <script src> or a
// fetch to a sibling is refused by the browser engine and there is no local
// server to ask instead.
//
// So every asset is embedded as a resource and inlined here, into the shell
// page, before it is navigated to. The result is one self-contained document:
// no temporary files, no HTTP server, and nothing to go missing at runtime.
//
// Which assets exist is decided by the build, which globs the asset directory
// and writes ui_manifest.h. That is deliberate: a hand-written list would be a
// shared file every task must edit, which is the conflict the split exists to
// prevent.
// ---------------------------------------------------------------------------

#pragma once

#include <string>

namespace storynode::ui {

/// The interface, assembled and ready to hand to NavigateToString.
///
/// Returns a minimal error page rather than throwing when an asset is missing,
/// because the caller is a window procedure with nowhere to propagate to, and
/// a page that says what is wrong is more useful than a blank window.
///
/// The returned string is UTF-16 because that is what NavigateToString takes.
std::wstring BuildInterfaceDocument();

} // namespace storynode::ui
