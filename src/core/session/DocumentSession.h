// ---------------------------------------------------------------------------
// One open document: the story, the undo stack, and the saved-state marker.
//
// The host used to keep a `Story` and a `bool` beside it, which cannot answer
// the question the file layer needs answered — "is this the same as what is on
// disk" — because a bool is set by whoever remembers to set it and an undo has
// no way to unset it. Undoing back to the saved state has to report clean, and
// editing again has to report dirty, and neither is knowable from a flag.
//
// The marker is therefore a revision number, not a copy of the document. A
// copy would double the memory of the largest thing in the process and turn
// "is it dirty" into a deep comparison of two documents — on every frame, if
// the title bar is drawn from it. Comparing two integers is the same answer
// for none of the cost.
//
// The document lives here rather than in the host so that the whole thing
// builds and runs in a test process with no window, which is what lets the
// undo rules be tested directly rather than only observed by clicking.
// ---------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/Model.h"
#include "core/history/Command.h"
#include "core/history/History.h"

namespace storynode {

/// The document a window has open, with its history and its saved marker.
class DocumentSession
{
public:
    /// A session over an empty story, clean, with an empty history.
    DocumentSession();

    /// A session over `document`, clean, with an empty history.
    ///
    /// A document that arrives with a session — from a file, or from a new
    /// document command — is by definition the state that is on disk (or the
    /// state that has no disk to be on), so it starts clean.
    explicit DocumentSession(Story document);

    /// As above, with an explicit history limit. Used by tests that need the
    /// bound to be small enough to reach.
    DocumentSession(Story document, std::size_t historyLimit);

    Story& Document() { return _document; }
    const Story& Document() const { return _document; }

    /// Apply a command, recording it for undo.
    ///
    /// Returns false when the command does nothing — one naming something the
    /// document does not have, or an edit that would leave the document as it
    /// already is. A refused command is not recorded, so the user is not asked
    /// to press undo twice for one visible change.
    bool Apply(Command command);

    /// Apply a command at an explicit time.
    ///
    /// The coalescing window is measured against a clock, and a test that had
    /// to sleep to cross it would be slow and flaky. Injecting the time is how
    /// the window is tested without either.
    bool ApplyAt(Command command, HistoryClock::time_point now);

    bool CanUndo() const { return _history.CanUndo(); }
    bool CanRedo() const { return _history.CanRedo(); }

    /// Take the last command back.
    ///
    /// Returns false when there is nothing to undo, or when the command can no
    /// longer be taken back. The caller needs the difference between "undone"
    /// and "nothing happened", because the second must not be reported to the
    /// host as a change.
    bool Undo();

    /// Re-apply the last undone command. Returns false on the same terms as
    /// Undo.
    bool Redo();

    /// True when the document differs from the last saved state.
    bool IsDirty() const { return _history.Revision() != _savedRevision; }

    /// Called by whoever writes the file, after it is written.
    ///
    /// After this the document is clean, and an undo back to this point — or a
    /// redo forward to it — reports clean again, because the revision it
    /// restores is the one recorded here.
    void MarkSaved();

    /// Replace the document wholesale, as New and Open do.
    ///
    /// The history is discarded rather than kept: its commands name nodes of
    /// the document that just went away, and applying one to the replacement
    /// would edit a document it was never built against.
    void Reset(Story document);

    /// The revision of the document as it stands. For diagnostics.
    std::uint64_t Revision() const { return _history.Revision(); }

    /// How many steps can be undone, and how many redone. Sent to the page so
    /// its buttons say what is actually available rather than what it assumes.
    std::size_t UndoDepth() const { return _history.UndoDepth(); }
    std::size_t RedoDepth() const { return _history.RedoDepth(); }

private:
    Story _document;
    History _history;

    /// The revision the document had when it was last written, or when it was
    /// last known to match what is on disk. Zero for a document that has never
    /// been saved, which is what a revision-0 document compares equal to.
    std::uint64_t _savedRevision = 0;
};

} // namespace storynode
