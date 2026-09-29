#include "core/session/DocumentSession.h"

#include <utility>

namespace storynode {

DocumentSession::DocumentSession() : _document(MakeEmptyStory("Untitled"))
{
}

DocumentSession::DocumentSession(Story document) : _document(std::move(document))
{
}

DocumentSession::DocumentSession(Story document, std::size_t historyLimit)
    : _document(std::move(document)), _history(historyLimit)
{
}

void DocumentSession::Apply(Command command)
{
    ApplyAt(std::move(command), HistoryClock::now());
}

void DocumentSession::ApplyAt(Command command, HistoryClock::time_point now)
{
    _history.Apply(_document, std::move(command), now);
}

bool DocumentSession::Undo()
{
    return _history.Undo(_document);
}

bool DocumentSession::Redo()
{
    return _history.Redo(_document);
}

void DocumentSession::MarkSaved()
{
    _savedRevision = _history.Revision();

    // The next edit must not merge into the step that was just written. If it
    // did, the single undo step would span the save: undoing would jump past
    // the saved state, the document would be dirty, and there would be no way
    // back to clean.
    _history.BreakCoalescing();
}

void DocumentSession::Reset(Story document)
{
    _document = std::move(document);
    _history.Clear();

    // A document that arrives this way came from a file, or from the New
    // command. Either way the state it is in is the state that is on disk, so
    // it is clean until the next edit.
    _savedRevision = 0;
}

} // namespace storynode
