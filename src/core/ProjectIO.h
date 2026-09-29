// ---------------------------------------------------------------------------
// Reading and writing .snproj files.
//
// The format is JSON with a stable key order, so a document saved by this
// editor diffs cleanly and can be edited by hand.
//
// Two rules shape everything here:
//
//   1. Loading reports every problem, with a location, rather than failing on
//      the first. A file with three mistakes should take one round trip to
//      fix, not three.
//
//   2. Loading never loses data. Keys this version does not recognise are
//      kept in `extra` and written back on save, so opening a file written by
//      a newer build and saving it does not quietly delete what that build
//      added.
// ---------------------------------------------------------------------------

#pragma once

#include <string>
#include <vector>

#include "core/Model.h"

namespace storynode {

/// The outcome of a load.
///
/// A load that produced problems still produces a story: a document with one
/// broken edge is worth opening so the user can fix it. `story` is therefore
/// always populated, and `problems` says what is wrong with it. Callers that
/// need a guaranteed-good document should check `problems` for any entry with
/// Severity::Error.
struct LoadResult
{
    Story story;
    std::vector<Problem> problems;

    /// True when nothing at all was reported.
    bool Clean() const { return problems.empty(); }

    /// True when at least one problem is an error rather than a warning.
    bool HasErrors() const;
};

/// Parse a document from JSON text.
///
/// A syntax error yields a LoadResult with one problem carrying the line and
/// column, and an empty story. A structurally valid document with semantic
/// problems yields the story plus the problems.
LoadResult Deserialize(const std::string& text);

/// Serialise a document to JSON text, with two-space indentation and a
/// trailing newline.
std::string Serialize(const Story& story);

/// Read a file and deserialise it.
///
/// A file that cannot be read yields one problem naming the path and the
/// system error, rather than throwing: the caller is an interface that has to
/// show something to the user either way.
LoadResult LoadFromFile(const std::string& path);

/// Serialise and write atomically.
///
/// Writes to a temporary file beside the target and renames it into place, so
/// a crash or a full disk during a save leaves the previous version intact
/// rather than a truncated file where the user's story used to be.
///
/// Returns an empty string on success, or a message describing what failed.
std::string SaveToFile(const Story& story, const std::string& path);

} // namespace storynode
