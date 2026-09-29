// ---------------------------------------------------------------------------
// Tests for the document session: the document, the history, and the saved
// marker.
//
// The saved-state marker is the one behaviour here that cannot be checked by
// eye. Undoing back to the saved state has to report clean, and any further
// edit has to report dirty again — a copy-and-compare marker would pass the
// first and be unusable in a real document, and a flag set on edit and cleared
// on save would pass neither.
// ---------------------------------------------------------------------------

#include "TestFramework.h"

#include "core/history/Command.h"
#include "core/session/DocumentSession.h"

using namespace storynode;

namespace {

/// A story with one dialog node at (100, 100) and an output port to connect
/// from. The shape most tests here edit and then take back.
Story MakeStory()
{
    Story story = MakeEmptyStory("Test");

    Node dialog;
    dialog.id = "dialog-1";
    dialog.type = "dialog";
    dialog.position = Vec2 { 100.0, 100.0 };
    dialog.ports.push_back(Port { "in", "In", Port::Kind::Input, Port::DataType::Flow, false });
    dialog.ports.push_back(Port { "out", "Out", Port::Kind::Output, Port::DataType::Flow, false });
    story.nodes.push_back(std::move(dialog));

    return story;
}

/// A fixed instant, so a test never depends on how long it took to run.
HistoryClock::time_point At(int milliseconds)
{
    return HistoryClock::time_point(std::chrono::milliseconds(milliseconds));
}

/// The position of a node, or a sentinel far outside any test's range.
Vec2 PositionOf(const Story& story, const std::string& id)
{
    const Node* node = story.FindNode(id);
    return node ? node->position : Vec2 { -9999.0, -9999.0 };
}

} // namespace

// --- the saved marker -------------------------------------------------------

TEST(ASessionStartsClean)
{
    DocumentSession session(MakeStory());
    CHECK_FALSE(session.IsDirty());
    CHECK_FALSE(session.CanUndo());
    CHECK_FALSE(session.CanRedo());
}

TEST(AnEditMakesTheSessionDirty)
{
    DocumentSession session(MakeStory());
    session.Apply(Command::MakeMoveNode("dialog-1", Vec2 { 100.0, 100.0 },
                                        Vec2 { 300.0, 300.0 }));
    CHECK(session.IsDirty());
}

TEST(SavingMakesTheSessionClean)
{
    DocumentSession session(MakeStory());
    session.Apply(Command::MakeMoveNode("dialog-1", Vec2 { 100.0, 100.0 },
                                        Vec2 { 300.0, 300.0 }));
    session.MarkSaved();
    CHECK_FALSE(session.IsDirty());
}

TEST(UndoingBackToTheSavedStateReportsClean)
{
    DocumentSession session(MakeStory());

    session.Apply(Command::MakeMoveNode("dialog-1", Vec2 { 100.0, 100.0 },
                                        Vec2 { 300.0, 300.0 }));
    session.MarkSaved();
    session.Apply(Command::MakeMoveNode("dialog-1", Vec2 { 300.0, 300.0 },
                                        Vec2 { 500.0, 500.0 }));
    CHECK(session.IsDirty());

    // Back past the save: still clean, because the state is the one that was
    // written. A flag set on edit and cleared on save cannot express this, and
    // that is the whole reason the marker is a revision.
    session.Undo();
    CHECK_FALSE(session.IsDirty());
}

TEST(EditingAgainAfterUndoingToTheSavedStateReportsDirty)
{
    DocumentSession session(MakeStory());

    session.Apply(Command::MakeMoveNode("dialog-1", Vec2 { 100.0, 100.0 },
                                        Vec2 { 300.0, 300.0 }));
    session.MarkSaved();
    session.Apply(Command::MakeMoveNode("dialog-1", Vec2 { 300.0, 300.0 },
                                        Vec2 { 500.0, 500.0 }));
    session.Undo();
    CHECK_FALSE(session.IsDirty());

    session.Apply(Command::MakeMoveNode("dialog-1", Vec2 { 300.0, 300.0 },
                                        Vec2 { 350.0, 350.0 }));
    CHECK(session.IsDirty());
}

TEST(UndoingPastTheSavedStateReportsDirty)
{
    // The saved state is the state that was written, not the state the
    // document was in before the first edit. Undoing further back leaves the
    // document differing from the file, and it has to say so.
    DocumentSession session(MakeStory());

    session.Apply(Command::MakeMoveNode("dialog-1", Vec2 { 100.0, 100.0 },
                                        Vec2 { 300.0, 300.0 }));
    session.MarkSaved();
    session.Undo();

    CHECK(session.IsDirty());
    CHECK_EQ(PositionOf(session.Document(), "dialog-1").x, 100.0);
}

TEST(RedoingForwardToTheSavedStateReportsClean)
{
    DocumentSession session(MakeStory());

    session.Apply(Command::MakeMoveNode("dialog-1", Vec2 { 100.0, 100.0 },
                                        Vec2 { 300.0, 300.0 }));
    session.MarkSaved();
    session.Undo();
    session.Redo();

    CHECK_FALSE(session.IsDirty());
    CHECK_EQ(PositionOf(session.Document(), "dialog-1").x, 300.0);
}

TEST(ThePageIsToldWhatItCanDo)
{
    DocumentSession session(MakeStory());

    CHECK_EQ(session.UndoDepth(), std::size_t(0));
    CHECK_EQ(session.RedoDepth(), std::size_t(0));

    session.Apply(Command::MakeMoveNode("dialog-1", Vec2 { 100.0, 100.0 },
                                        Vec2 { 300.0, 300.0 }));
    CHECK_EQ(session.UndoDepth(), std::size_t(1));
    CHECK_EQ(session.RedoDepth(), std::size_t(0));

    // Redo is unavailable the moment a new edit lands, and the depths are how
    // the page is told rather than being left to guess from what it sent.
    session.Undo();
    CHECK_EQ(session.UndoDepth(), std::size_t(0));
    CHECK_EQ(session.RedoDepth(), std::size_t(1));

    session.Apply(Command::MakeMoveNode("dialog-1", Vec2 { 100.0, 100.0 },
                                        Vec2 { 700.0, 700.0 }));
    CHECK_EQ(session.UndoDepth(), std::size_t(1));
    CHECK_EQ(session.RedoDepth(), std::size_t(0));
}

TEST(ResettingTheSessionDiscardsTheHistory)
{
    DocumentSession session(MakeStory());

    session.Apply(Command::MakeMoveNode("dialog-1", Vec2 { 100.0, 100.0 },
                                        Vec2 { 300.0, 300.0 }));
    session.MarkSaved();
    session.Reset(MakeEmptyStory("Other"));

    CHECK_FALSE(session.IsDirty());
    CHECK_FALSE(session.CanUndo());
    CHECK_FALSE(session.CanRedo());

    // An undo from the previous document must not be available: its commands
    // name nodes the new document does not have.
    session.Undo();
    CHECK_FALSE(session.IsDirty());
}

TEST(TheRevisionMovesForwardOnly)
{
    DocumentSession session(MakeStory());

    // Spaced apart so the two moves are two steps. Applied back to back they
    // would coalesce, which is correct behaviour and not what this checks.
    session.ApplyAt(Command::MakeMoveNode("dialog-1", Vec2 { 100.0, 100.0 },
                                          Vec2 { 300.0, 300.0 }), At(0));
    const std::uint64_t afterFirst = session.Revision();

    session.ApplyAt(Command::MakeMoveNode("dialog-1", Vec2 { 300.0, 300.0 },
                                          Vec2 { 500.0, 500.0 }), At(5000));
    const std::uint64_t afterSecond = session.Revision();
    CHECK(afterSecond > afterFirst);

    session.Undo();
    CHECK_EQ(session.Revision(), afterFirst);
    session.Undo();
    CHECK_EQ(session.Revision(), std::uint64_t(0));

    // An edit after an undo takes a revision the document has never had, so
    // the saved revision cannot be reached again by accident.
    session.ApplyAt(Command::MakeMoveNode("dialog-1", Vec2 { 100.0, 100.0 },
                                          Vec2 { 150.0, 150.0 }), At(10000));
    CHECK(session.Revision() > afterSecond);
}
