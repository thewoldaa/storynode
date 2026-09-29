// ---------------------------------------------------------------------------
// The undo stack: a bounded list of commands, and the revision it produces.
//
// The stack is what makes "is this the same as what is on disk" answerable.
// Every applied command takes the document to a new *revision*, a number that
// only ever goes up while the document is edited; the session remembers which
// revision was last written to disk, so dirty is a comparison of two integers
// rather than a deep comparison of two documents on every frame.
//
// That is also what makes an undo back to the saved state report clean: the
// undo restores the revision the document had when it was saved, not merely
// some document that happens to compare equal.
//
// Two behaviours are easy to get subtly wrong and are pinned by tests:
//
//   * Coalescing. A drag sends one message per gesture already, but a gesture
//     can be several messages if the pointer is released and pressed again
//     inside the window. Consecutive moves of one node inside a short window
//     merge into a single step, so the user undoes the drag and not the last
//     few pixels of it. The merged step keeps the *first* command's "before"
//     position — merging the "after" of the second would make an undo land
//     halfway through the drag.
//
//   * The bound. A long session is an unbounded stack otherwise, which is a
//     slow leak that only shows up in the session that has been open longest.
//     Trimming drops the oldest steps, which can put the saved revision out of
//     reach: the document is then permanently dirty until it is saved again,
//     which is true — it does differ from the file, and no amount of undo will
//     change that.
// ---------------------------------------------------------------------------

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/Model.h"
#include "core/history/Command.h"

namespace storynode {

/// The clock the coalescing window is measured against.
///
/// Steady rather than system: a window measured against the wall clock would
/// close early if the machine synchronised its time mid-drag.
using HistoryClock = std::chrono::steady_clock;

/// One applied edit, with the revisions of the states either side of it.
struct HistoryEntry
{
    Command command;

    /// The revision of the document before this command was applied.
    std::uint64_t revisionBefore = 0;

    /// The revision of the document after. Undo restores `revisionBefore`,
    /// redo restores `revisionAfter`, which is what keeps the saved-state
    /// comparison meaningful across a round trip.
    std::uint64_t revisionAfter = 0;

    /// When the command was applied. Used only to decide whether the next one
    /// coalesces into this entry.
    HistoryClock::time_point at {};
};

/// A bounded undo/redo stack over a document.
///
/// The stack does not own the document and does not keep a copy of it. It
/// applies commands to whatever document it is handed, which is what lets a
/// test drive it with a bare `Story`.
class History
{
public:
    /// How many steps are kept. Far past what a user reaches for in one
    /// session, and small enough that the memory is not worth thinking about.
    static constexpr std::size_t kDefaultLimit = 200;

    /// How long after a move a further move of the same node still merges into
    /// it.
    ///
    /// Long enough to cover the gap between releasing and re-pressing the
    /// button inside one drag, short enough that two deliberate moves of a
    /// node do not merge. Changing this changes how many undos a drag costs.
    static constexpr std::chrono::milliseconds kCoalesceWindow { 600 };

    /// A stack keeping at most `limit` steps. A limit of 0 records nothing,
    /// which disables undo without disabling editing.
    explicit History(std::size_t limit = kDefaultLimit);

    /// Apply `command` to `story` and record it.
    ///
    /// Returns false when the command does nothing — one that names something
    /// the document does not have, or a move that ends where it started. A
    /// refused command leaves both the document and the stack untouched,
    /// because a no-op step in the stack is an undo the user has to press
    /// twice for no visible reason.
    ///
    /// `now` is a parameter rather than a call to the clock inside, so a test
    /// can drive the coalescing window without sleeping.
    bool Apply(Story& story, Command command, HistoryClock::time_point now);

    /// Apply `command`, timestamped with the current time.
    bool Apply(Story& story, Command command);

    bool CanUndo() const { return _position > 0; }
    bool CanRedo() const { return _position < _commands.size(); }

    /// Take the last command back. Returns false when there is nothing to undo
    /// or when the command can no longer be taken back.
    bool Undo(Story& story);

    /// Re-apply the last undone command. Returns false when there is nothing
    /// to redo.
    bool Redo(Story& story);

    /// Forget every step, and start again at revision 0.
    ///
    /// Used when the document is replaced wholesale — a new document, or a
    /// file just opened. Keeping the old steps would let an undo apply a
    /// command built against the previous document to this one.
    void Clear();

    /// Stop the next edit merging into the one on top.
    ///
    /// Called after a save. Without it, an edit made within the coalescing
    /// window of the edit that was just written would merge into it, and the
    /// single undo step would then span the save — so undoing would jump past
    /// the saved state and the document would be dirty with no way back to
    /// clean. Merging is for one gesture, and a save ends the gesture.
    void BreakCoalescing();

    /// The revision of the document as it stands. 0 is a document as it was
    /// loaded, before any edit.
    std::uint64_t Revision() const { return _revision; }

    /// The number of steps retained. Never exceeds Limit().
    std::size_t Depth() const { return _commands.size(); }

    /// How many retained steps are behind the current state.
    std::size_t UndoDepth() const { return _position; }

    /// How many are ahead of it, available to redo.
    std::size_t RedoDepth() const { return _commands.size() - _position; }

    std::size_t Limit() const { return _limit; }

    /// The retained steps, oldest first. For tests and diagnostics.
    const std::vector<HistoryEntry>& Entries() const { return _commands; }

private:
    /// True when `command` merges into the entry on top rather than starting a
    /// new one.
    bool CanCoalesce(const Command& command, HistoryClock::time_point now) const;

    /// Drop the oldest steps until the stack is within its limit.
    void Trim();

    std::vector<HistoryEntry> _commands;

    /// One past the last applied step. Everything before it is applied;
    /// everything at or after it has been undone and is available to redo.
    std::size_t _position = 0;

    std::size_t _limit = kDefaultLimit;

    /// The revision of the current document state.
    std::uint64_t _revision = 0;

    /// The revision the next *new* state will take. Monotonic, so two
    /// different states never share a revision even after a redo, an undo and
    /// an unrelated edit — which is what stops an old saved revision from
    /// being reached again by accident.
    std::uint64_t _nextRevision = 1;

    /// Set by Undo, Redo and BreakCoalescing; cleared by the next recorded
    /// edit.
    ///
    /// An undo or a redo ends the gesture: the next move starts a new step
    /// even if it is of the same node and inside the window, because merging it
    /// into a step that has been undone and redone would rewrite a state the
    /// user has already visited. A save ends it for the reason on
    /// BreakCoalescing.
    bool _coalesceBlocked = false;
};

} // namespace storynode
