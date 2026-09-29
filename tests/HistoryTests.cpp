// ---------------------------------------------------------------------------
// Tests for the undo stack.
//
// Two of these behaviours are easy to get subtly wrong and impossible to
// notice by eye, which is why they are pinned here rather than checked by
// clicking:
//
//   * Coalescing. A drag that arrives as several messages must cost one undo,
//     and the merged step must remember the position the drag *started* at.
//     Merging the wrong end leaves the node halfway across the canvas after an
//     undo, which looks like a bug in the drag rather than in the history.
//
//   * The bound. An unbounded stack is a slow leak, and a stack that trims
//     without moving its position reports the wrong number of undos.
//
// The saved-state marker is the session's, and is tested in SessionTests.cpp.
// ---------------------------------------------------------------------------

#include "TestFramework.h"

#include "core/history/Command.h"
#include "core/history/History.h"

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

// --- moving a node ----------------------------------------------------------

TEST(UndoRestoresThePreviousPosition)
{
    Story story = MakeStory();
    History history;

    history.Apply(story, Command::MakeMoveNode("dialog-1", Vec2 { 100.0, 100.0 },
                                               Vec2 { 400.0, 250.0 }));

    CHECK_EQ(PositionOf(story, "dialog-1").x, 400.0);
    CHECK_EQ(PositionOf(story, "dialog-1").y, 250.0);
    CHECK(history.CanUndo());

    CHECK(history.Undo(story));
    CHECK_EQ(PositionOf(story, "dialog-1").x, 100.0);
    CHECK_EQ(PositionOf(story, "dialog-1").y, 100.0);
    CHECK_FALSE(history.CanUndo());
    CHECK(history.CanRedo());
}

TEST(RedoReappliesTheMove)
{
    Story story = MakeStory();
    History history;

    history.Apply(story, Command::MakeMoveNode("dialog-1", Vec2 { 100.0, 100.0 },
                                               Vec2 { 400.0, 250.0 }));
    history.Undo(story);
    CHECK(history.Redo(story));

    CHECK_EQ(PositionOf(story, "dialog-1").x, 400.0);
    CHECK_EQ(PositionOf(story, "dialog-1").y, 250.0);
    CHECK_FALSE(history.CanRedo());
}

TEST(AMoveThatEndsWhereItStartedIsRefused)
{
    Story story = MakeStory();
    History history;

    // A click that did not move the node must not cost an undo. Otherwise a
    // user who clicks a node and presses Ctrl+Z sees nothing happen.
    CHECK_FALSE(history.Apply(story, Command::MakeMoveNode(
        "dialog-1", Vec2 { 100.0, 100.0 }, Vec2 { 100.0, 100.0 })));
    CHECK_FALSE(history.CanUndo());
}

TEST(AMoveOfAMissingNodeIsRefused)
{
    Story story = MakeStory();
    History history;

    CHECK_FALSE(history.Apply(story, Command::MakeMoveNode(
        "ghost", Vec2 { 0.0, 0.0 }, Vec2 { 10.0, 10.0 })));
    CHECK_FALSE(history.CanUndo());
}

// --- coalescing -------------------------------------------------------------

TEST(ConsecutiveMovesOfOneNodeAreOneStep)
{
    Story story = MakeStory();
    History history;

    // One drag, delivered as three messages: the page sends a move when the
    // gesture ends, and a gesture that is released and pressed again inside
    // the window arrives as two. All of them are the same drag to the user.
    history.Apply(story, Command::MakeMoveNode("dialog-1", Vec2 { 100.0, 100.0 },
                                               Vec2 { 140.0, 110.0 }), At(0));
    history.Apply(story, Command::MakeMoveNode("dialog-1", Vec2 { 140.0, 110.0 },
                                               Vec2 { 200.0, 150.0 }), At(100));
    history.Apply(story, Command::MakeMoveNode("dialog-1", Vec2 { 200.0, 150.0 },
                                               Vec2 { 300.0, 260.0 }), At(200));

    CHECK_EQ(history.Depth(), std::size_t(1));

    // The undo must land at the start of the drag, not at the last leg of it.
    CHECK(history.Undo(story));
    CHECK_EQ(PositionOf(story, "dialog-1").x, 100.0);
    CHECK_EQ(PositionOf(story, "dialog-1").y, 100.0);
    CHECK_FALSE(history.CanUndo());
}

TEST(RedoAfterACoalescedDragLandsAtTheEnd)
{
    Story story = MakeStory();
    History history;

    history.Apply(story, Command::MakeMoveNode("dialog-1", Vec2 { 100.0, 100.0 },
                                               Vec2 { 140.0, 110.0 }), At(0));
    history.Apply(story, Command::MakeMoveNode("dialog-1", Vec2 { 140.0, 110.0 },
                                               Vec2 { 300.0, 260.0 }), At(100));
    history.Undo(story);

    CHECK(history.Redo(story));
    CHECK_EQ(PositionOf(story, "dialog-1").x, 300.0);
    CHECK_EQ(PositionOf(story, "dialog-1").y, 260.0);
}

TEST(MovesOutsideTheWindowAreSeparateSteps)
{
    Story story = MakeStory();
    History history;

    history.Apply(story, Command::MakeMoveNode("dialog-1", Vec2 { 100.0, 100.0 },
                                               Vec2 { 200.0, 200.0 }), At(0));
    history.Apply(story, Command::MakeMoveNode("dialog-1", Vec2 { 200.0, 200.0 },
                                               Vec2 { 300.0, 300.0 }),
                  At(static_cast<int>(History::kCoalesceWindow.count()) + 1));

    CHECK_EQ(history.Depth(), std::size_t(2));
}

TEST(MovesOfDifferentNodesAreSeparateSteps)
{
    Story story = MakeStory();

    Node other;
    other.id = "dialog-2";
    other.type = "dialog";
    other.position = Vec2 { 500.0, 500.0 };
    story.nodes.push_back(std::move(other));

    History history;
    history.Apply(story, Command::MakeMoveNode("dialog-1", Vec2 { 100.0, 100.0 },
                                               Vec2 { 200.0, 200.0 }), At(0));
    history.Apply(story, Command::MakeMoveNode("dialog-2", Vec2 { 500.0, 500.0 },
                                               Vec2 { 600.0, 600.0 }), At(10));

    CHECK_EQ(history.Depth(), std::size_t(2));
}

TEST(AnEditBetweenTwoMovesBreaksTheCoalesce)
{
    Story story = MakeStory();
    History history;

    history.Apply(story, Command::MakeMoveNode("dialog-1", Vec2 { 100.0, 100.0 },
                                               Vec2 { 200.0, 200.0 }), At(0));
    history.Apply(story, Command::MakeSetProperty(story, "dialog-1", "speaker",
                                                  json::Value("Narrator")), At(10));
    history.Apply(story, Command::MakeMoveNode("dialog-1", Vec2 { 200.0, 200.0 },
                                               Vec2 { 300.0, 300.0 }), At(20));

    CHECK_EQ(history.Depth(), std::size_t(3));
}

TEST(AnUndoEndsTheGestureSoTheNextMoveIsANewStep)
{
    Story story = MakeStory();
    History history;

    history.Apply(story, Command::MakeMoveNode("dialog-1", Vec2 { 100.0, 100.0 },
                                               Vec2 { 200.0, 200.0 }), At(0));
    history.Undo(story);

    // A move right after an undo is a new intention. Merging it into the step
    // that was just undone would rewrite a state the user has already visited,
    // and the redo would then land somewhere they never were.
    history.Apply(story, Command::MakeMoveNode("dialog-1", Vec2 { 100.0, 100.0 },
                                               Vec2 { 150.0, 150.0 }), At(10));

    CHECK_EQ(history.Depth(), std::size_t(1));
    CHECK_EQ(history.UndoDepth(), std::size_t(1));
    CHECK_FALSE(history.CanRedo());
}

// --- the redo branch --------------------------------------------------------

TEST(ANewEditDiscardsTheRedoBranch)
{
    Story story = MakeStory();
    History history;

    history.Apply(story, Command::MakeMoveNode("dialog-1", Vec2 { 100.0, 100.0 },
                                               Vec2 { 200.0, 200.0 }), At(0));
    history.Undo(story);
    CHECK(history.CanRedo());

    // Editing from an undone state abandons the future it was going to redo.
    history.Apply(story, Command::MakeMoveNode("dialog-1", Vec2 { 100.0, 100.0 },
                                               Vec2 { 300.0, 300.0 }), At(10));

    CHECK_FALSE(history.CanRedo());
    CHECK_EQ(history.Depth(), std::size_t(1));
    CHECK_EQ(PositionOf(story, "dialog-1").x, 300.0);
}

// --- the bound --------------------------------------------------------------

TEST(TheStackStopsAtItsLimit)
{
    Story story = MakeStory();
    History history(3);

    // Five separate edits, each outside the coalescing window so each is a
    // step of its own.
    for (int i = 0; i < 5; ++i)
    {
        const double from = 100.0 + i * 10.0;
        const double to = from + 10.0;
        history.Apply(story, Command::MakeMoveNode("dialog-1", Vec2 { from, from },
                                                   Vec2 { to, to }),
                      At(i * 1000));
    }

    CHECK_EQ(history.Depth(), std::size_t(3));
    CHECK_EQ(history.UndoDepth(), std::size_t(3));

    // Three undos reach the oldest state still on the stack, and the fourth
    // does nothing rather than reaching past it.
    CHECK(history.Undo(story));
    CHECK(history.Undo(story));
    CHECK(history.Undo(story));
    CHECK_FALSE(history.CanUndo());
    CHECK_FALSE(history.Undo(story));

    // The state the stack can reach is the one before the oldest step it still
    // holds — the state after the second edit, which is 120. The two dropped
    // steps took their own states with them.
    CHECK_EQ(PositionOf(story, "dialog-1").x, 120.0);
}

TEST(AZeroLimitRecordsNothingButStillEdits)
{
    Story story = MakeStory();
    History history(0);

    CHECK(history.Apply(story, Command::MakeMoveNode("dialog-1", Vec2 { 100.0, 100.0 },
                                                     Vec2 { 400.0, 400.0 })));

    // The document changed — the caller still needs to know it is dirty — but
    // there is nothing to undo.
    CHECK_EQ(PositionOf(story, "dialog-1").x, 400.0);
    CHECK_EQ(history.Depth(), std::size_t(0));
    CHECK_FALSE(history.CanUndo());
}

// --- other commands ---------------------------------------------------------

TEST(UndoRestoresARemovedNodeAndItsEdges)
{
    Story story = MakeStory();
    History history;

    history.Apply(story, Command::MakeConnect(
        Edge { "edge-1", Endpoint { "start-1", "out" }, Endpoint { "dialog-1", "in" },
               json::Object {} }));
    CHECK_EQ(story.edges.size(), std::size_t(1));

    history.Apply(story, Command::MakeRemoveNode(story, "dialog-1"));
    CHECK(story.FindNode("dialog-1") == nullptr);
    CHECK_EQ(story.edges.size(), std::size_t(0));

    CHECK(history.Undo(story));
    CHECK(story.FindNode("dialog-1") != nullptr);
    CHECK_EQ(story.edges.size(), std::size_t(1));

    // The document must be valid again, not merely shaped right: an undo that
    // leaves a dangling edge is worse than one that does nothing.
    CHECK_EQ(Validate(story).size(), std::size_t(0));
}

TEST(UndoOfAnAddedNodeRemovesIt)
{
    Story story = MakeStory();
    History history;

    Node added;
    added.id = "branch-1";
    added.type = "branch";

    history.Apply(story, Command::MakeAddNode(added));
    CHECK_EQ(story.nodes.size(), std::size_t(3));

    CHECK(history.Undo(story));
    CHECK_EQ(story.nodes.size(), std::size_t(2));
    CHECK(story.FindNode("branch-1") == nullptr);

    CHECK(history.Redo(story));
    CHECK(story.FindNode("branch-1") != nullptr);
}

TEST(UndoOfAPropertyThatDidNotExistRemovesTheKey)
{
    Story story = MakeStory();
    History history;

    history.Apply(story, Command::MakeSetProperty(story, "dialog-1", "speaker",
                                                  json::Value("Narrator")));
    CHECK_EQ(story.FindNode("dialog-1")->data["speaker"].AsString(),
             std::string("Narrator"));

    CHECK(history.Undo(story));

    // Removed, not set to null. A key left behind as null is a property the
    // inspector shows as present and empty, which is a different document.
    CHECK_FALSE(story.FindNode("dialog-1")->data.Has("speaker"));
}

TEST(UndoOfAPropertyRestoresItsPreviousValue)
{
    Story story = MakeStory();
    story.nodes[1].data.Set("speaker", json::Value("Narrator"));

    History history;
    history.Apply(story, Command::MakeSetProperty(story, "dialog-1", "speaker",
                                                  json::Value("Guard")));
    CHECK_EQ(story.FindNode("dialog-1")->data["speaker"].AsString(),
             std::string("Guard"));

    CHECK(history.Undo(story));
    CHECK_EQ(story.FindNode("dialog-1")->data["speaker"].AsString(),
             std::string("Narrator"));
}

TEST(UndoOfADisconnectPutsTheEdgeBackWhereItWas)
{
    Story story = MakeStory();
    History history;

    history.Apply(story, Command::MakeConnect(
        Edge { "edge-1", Endpoint { "start-1", "out" }, Endpoint { "dialog-1", "in" },
               json::Object {} }), At(0));
    history.Apply(story, Command::MakeConnect(
        Edge { "edge-2", Endpoint { "start-1", "out" }, Endpoint { "dialog-1", "out" },
               json::Object {} }), At(1000));
    history.Apply(story, Command::MakeDisconnect(story, "edge-1"), At(2000));
    CHECK_EQ(story.edges.size(), std::size_t(1));

    CHECK(history.Undo(story));
    CHECK_EQ(story.edges.size(), std::size_t(2));

    // Order matters: the file format writes edges in list order, so an undo
    // followed by a save must produce the file the user had before the edit.
    CHECK_EQ(story.edges[0].id, std::string("edge-1"));
    CHECK_EQ(story.edges[1].id, std::string("edge-2"));
}

// --- edits that change nothing ----------------------------------------------
//
// A step that undoes to the state the user is already looking at is an undo
// that appears to do nothing. Both of these are ordinary: a click that did not
// move a node, and a field focused and blurred without being typed into.

TEST(SettingAPropertyToItsCurrentValueIsRefused)
{
    Story story = MakeStory();
    story.nodes[1].data.Set("speaker", json::Value("Narrator"));

    History history;
    CHECK_FALSE(history.Apply(story, Command::MakeSetProperty(
        story, "dialog-1", "speaker", json::Value("Narrator"))));
    CHECK_FALSE(history.CanUndo());
}

TEST(SettingAPropertyToADifferentValueIsRecorded)
{
    Story story = MakeStory();
    story.nodes[1].data.Set("speaker", json::Value("Narrator"));

    History history;
    CHECK(history.Apply(story, Command::MakeSetProperty(
        story, "dialog-1", "speaker", json::Value("Guard"))));
    CHECK_EQ(history.Depth(), std::size_t(1));
}

TEST(SettingAPropertyThatDidNotExistToNullIsRecorded)
{
    // Null is not "no key". Adding a null property is a change to the
    // document, and an undo has to remove the key again.
    Story story = MakeStory();

    History history;
    CHECK(history.Apply(story, Command::MakeSetProperty(
        story, "dialog-1", "speaker", json::Value(nullptr))));
    CHECK(story.FindNode("dialog-1")->data.Has("speaker"));

    CHECK(history.Undo(story));
    CHECK_FALSE(story.FindNode("dialog-1")->data.Has("speaker"));
}
