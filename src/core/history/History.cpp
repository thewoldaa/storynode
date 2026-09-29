#include "core/history/History.h"

#include <utility>

namespace storynode {

History::History(std::size_t limit) : _limit(limit)
{
}

bool History::Apply(Story& story, Command command)
{
    return Apply(story, std::move(command), HistoryClock::now());
}

bool History::Apply(Story& story, Command command, HistoryClock::time_point now)
{
    if (command.kind == Command::Kind::None || command.IsNoop())
    {
        return false;
    }

    // The decision needs the entry on top, and that entry is about to be
    // replaced or joined, so it is taken before the document changes.
    const bool coalesces = CanCoalesce(command, now);

    // The state the document is in now. This becomes the new step's
    // `revisionBefore`, which is what an undo of it returns to. Reading it here
    // rather than deriving it from the previous entry is what makes the number
    // correct after an undo followed by a different edit.
    const std::uint64_t before = _revision;

    if (!command.Apply(story))
    {
        // The document does not contain what the command names. Nothing
        // changed, so nothing is recorded.
        return false;
    }

    _revision = _nextRevision;
    _nextRevision += 1;
    _coalesceBlocked = false;

    if (_limit == 0)
    {
        // Undo is disabled, but the document changed and the revision moved,
        // so the caller still learns it is dirty.
        return true;
    }

    if (coalesces)
    {
        // The step keeps its original `from` and takes the new `to`; its
        // `revisionAfter` moves to the state just produced. The
        // `revisionBefore` is deliberately left alone: it is the state an undo
        // returns to, and that is the state before the drag began, not before
        // its last frame.
        HistoryEntry& entry = _commands.back();
        entry.command.move.to = command.move.to;
        entry.revisionAfter = _revision;
        entry.at = now;
        return true;
    }

    // A new edit discards the redo branch. The states it led to are no longer
    // reachable from this one, and keeping them would let a redo apply a
    // command built against a document that no longer exists.
    _commands.resize(_position);

    HistoryEntry entry;
    entry.command = std::move(command);
    entry.revisionBefore = before;
    entry.revisionAfter = _revision;
    entry.at = now;

    _commands.push_back(std::move(entry));
    _position = _commands.size();

    Trim();
    return true;
}

bool History::Undo(Story& story)
{
    if (!CanUndo())
    {
        return false;
    }

    const HistoryEntry& entry = _commands[_position - 1];
    if (!entry.command.Revert(story))
    {
        // The command cannot be taken back — the node it names is gone. The
        // stack is left alone so the caller can decide what to do, rather than
        // dropping a step and leaving the document and the history disagreeing
        // about where they are.
        return false;
    }

    _position -= 1;
    _revision = entry.revisionBefore;
    _coalesceBlocked = true;
    return true;
}

bool History::Redo(Story& story)
{
    if (!CanRedo())
    {
        return false;
    }

    const HistoryEntry& entry = _commands[_position];
    if (!entry.command.Apply(story))
    {
        return false;
    }

    _position += 1;
    _revision = entry.revisionAfter;
    _coalesceBlocked = true;
    return true;
}

void History::BreakCoalescing()
{
    _coalesceBlocked = true;
}

void History::Clear()
{
    _commands.clear();
    _position = 0;
    _revision = 0;
    _nextRevision = 1;
    _coalesceBlocked = false;
}

bool History::CanCoalesce(const Command& command, HistoryClock::time_point now) const
{
    if (_coalesceBlocked || _commands.empty() || _position != _commands.size())
    {
        return false;
    }

    const HistoryEntry& top = _commands.back();
    if (top.command.kind != Command::Kind::MoveNode ||
        command.kind != Command::Kind::MoveNode)
    {
        return false;
    }

    if (top.command.move.nodeId != command.move.nodeId)
    {
        return false;
    }

    // The window is measured from the previous move, not from the start of the
    // gesture, so a long slow drag still merges as long as it does not pause.
    return now - top.at <= kCoalesceWindow;
}

void History::Trim()
{
    if (_commands.size() <= _limit)
    {
        return;
    }

    const std::size_t excess = _commands.size() - _limit;
    _commands.erase(_commands.begin(),
                    _commands.begin() + static_cast<std::ptrdiff_t>(excess));

    // The position moves with the erasure. It can only fall to zero: a trim
    // happens immediately after a push, which leaves the position at the end
    // of the list, so there is never a redo branch to account for.
    _position = _position > excess ? _position - excess : 0;
}

} // namespace storynode
