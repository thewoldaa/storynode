// ---------------------------------------------------------------------------
// Command-line handling for the editor.
//
// Windows-only, because it reads the process command line through Win32.
// Everything it configures is declared in LayoutReport.h, which has no
// platform dependency and can be tested.
// ---------------------------------------------------------------------------

#pragma once

#include <string>

namespace storynode {

/// Parsed command line.
struct CommandLine
{
    /// Run the verification pass and exit instead of showing the editor.
    bool verify = false;

    /// Where to write the verification report. Empty writes to stdout.
    std::wstring reportPath;

    /// Client size to lay the interface out at during verification.
    ///
    /// Configurable so CI can check more than one size. A layout that happens
    /// to be correct at one window size is not evidence that it is correct:
    /// the footer sitting at the bottom of an 800-pixel window proves nothing
    /// about a 500-pixel one, which is where a missing `min-height: 0` or a
    /// fixed row height shows up.
    int width = 1280;
    int height = 800;
};

/// Read the command line. Recognises:
///   --verify                 run the layout check and exit
///   --report <path>          write the report to a file instead of stdout
///   --size <width>x<height>  lay out at this size during verification
CommandLine ParseCommandLine();

} // namespace storynode
