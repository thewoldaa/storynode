// ---------------------------------------------------------------------------
// Tests for the message bridge.
//
// The bridge is driven directly, with a capturing sender instead of a web
// view. That is the payoff for keeping WebView2 types out of the protocol: a
// test can send a message and read the reply with no window, no message loop
// and no runtime installed.
//
// The handshake has its own test because getting it wrong is invisible: a
// host that sends before the page is listening sees no error, just a page
// that loads with no data.
// ---------------------------------------------------------------------------

#include "TestFramework.h"

#include "app/Bridge.h"
#include "core/ProjectIO.h"
#include "core/session/DocumentSession.h"

using namespace storynode;

namespace {

/// Collects everything the bridge sends, so a test can assert on it.
class CapturingSender
{
public:
    void operator()(const std::string& text) { messages.push_back(text); }

    /// The last message, parsed. Tests that check one reply use this.
    json::Value Last() const
    {
        if (messages.empty())
        {
            return json::Value();
        }
        const json::ParseResult parsed = json::Parse(messages.back());
        return parsed.ok ? parsed.value : json::Value();
    }

    /// The last message whose type field equals `type`, or null.
    json::Value LastOfType(const std::string& type) const
    {
        for (auto it = messages.rbegin(); it != messages.rend(); ++it)
        {
            const json::ParseResult parsed = json::Parse(*it);
            if (parsed.ok && parsed.value["type"].AsString() == type)
            {
                return parsed.value;
            }
        }
        return json::Value();
    }

    int CountOfType(const std::string& type) const
    {
        int count = 0;
        for (const std::string& text : messages)
        {
            const json::ParseResult parsed = json::Parse(text);
            if (parsed.ok && parsed.value["type"].AsString() == type)
            {
                count += 1;
            }
        }
        return count;
    }

    void Clear() { messages.clear(); }

    std::vector<std::string> messages;
};

/// A story with a start node and a dialog node, which is what every test here
/// edits. The start node comes from MakeEmptyStory.
Story MakeStory()
{
    Story story = MakeEmptyStory("Test");

    Node dialog;
    dialog.id = "dialog-1";
    dialog.type = "dialog";
    dialog.position = Vec2 { 400.0, 120.0 };
    dialog.ports.push_back(Port { "in", "In", Port::Kind::Input, Port::DataType::Flow, false });
    dialog.ports.push_back(Port { "out", "Next", Port::Kind::Output, Port::DataType::Flow, false });
    story.nodes.push_back(std::move(dialog));

    return story;
}

/// A bridge over a two-node story, with the page already ready.
struct Fixture
{
    /// The session the bridge edits, and the document inside it.
    ///
    /// Tests read `story` because that is what they assert on, and it is a
    /// reference into the session rather than a copy: a test that asserted on
    /// a copy would pass while the session held something else.
    DocumentSession session;
    Story& story;
    CapturingSender sender;
    std::unique_ptr<Bridge> bridge;

    Fixture() : session(MakeStory()), story(session.Document())
    {
        bridge.reset(new Bridge(session, [this](const std::string& text) {
            sender(text);
        }));
    }

    void MakeReady()
    {
        bridge->HandleMessage(R"({"type":"ready"})");
        sender.Clear();
    }

    /// Sends a message and reports whether it changed the document.
    ///
    /// The host uses this to decide whether the document is unsaved. A bridge
    /// that reported a change for every message would mark a freshly opened
    /// document dirty the moment the page said hello.
    bool Send(const std::string& text) { return bridge->HandleMessage(text); }
};

} // namespace

// --- handshake --------------------------------------------------------------

TEST(BridgeSendsNothingBeforeThePageIsReady)
{
    Fixture fixture;

    // The page has not said it is listening. Anything sent now is dropped by
    // the web view, so the bridge must not pretend it was delivered.
    fixture.bridge->SendDocument();
    fixture.bridge->SendValidation();

    CHECK_EQ(fixture.sender.messages.size(), std::size_t(0));
    CHECK_FALSE(fixture.bridge->PageReady());
}

TEST(BridgeSendsTheDocumentOnReady)
{
    Fixture fixture;
    fixture.Send(R"({"type":"ready"})");

    CHECK(fixture.bridge->PageReady());
    CHECK_EQ(fixture.sender.CountOfType("document"), 1);
    CHECK_EQ(fixture.sender.CountOfType("validation"), 1);

    // The document must actually be in there, not an empty shell.
    const json::Value message = fixture.sender.LastOfType("document");
    CHECK_EQ(message["document"]["nodes"].Size(), std::size_t(2));
    CHECK_EQ(message["document"]["title"].AsString(), std::string("Test"));
}

TEST(BridgeRepeatsTheHandshakeWithoutDuplicating)
{
    Fixture fixture;
    fixture.MakeReady();

    // A page that reloads sends ready again and must get the document again.
    fixture.Send(R"({"type":"ready"})");
    CHECK_EQ(fixture.sender.CountOfType("document"), 1);
}

// --- malformed input --------------------------------------------------------

TEST(BridgeReportsMalformedJson)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send("this is not json");

    const json::Value message = fixture.sender.LastOfType("error");
    CHECK_FALSE(message.IsNull());
    CHECK(message["message"].AsString().find("Malformed") != std::string::npos);
}

TEST(BridgeReportsAMessageWithNoType)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send(R"({"x":1})");

    CHECK_FALSE(fixture.sender.LastOfType("error").IsNull());
}

TEST(BridgeReportsAnUnknownType)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send(R"({"type":"teleportNode"})");

    const json::Value message = fixture.sender.LastOfType("error");
    CHECK(message["message"].AsString().find("teleportNode") != std::string::npos);
}

TEST(BridgeSurvivesAMalformedMessage)
{
    // A bad message must not take the editor down. The bridge keeps working.
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send("{{{");
    fixture.Send(R"({"type":"ready"})");

    CHECK(fixture.bridge->PageReady());
    CHECK(fixture.bridge->MessagesHandled() >= 2);
}

// --- moving a node ----------------------------------------------------------

TEST(BridgeMovesANode)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send(R"({"type":"moveNode","id":"dialog-1","x":640.5,"y":300.25})");

    const Node* node = fixture.story.FindNode("dialog-1");
    CHECK(node != nullptr);
    CHECK_EQ(node->position.x, 640.5);
    CHECK_EQ(node->position.y, 300.25);
    // The page is told the new state so the two cannot drift.
    CHECK_EQ(fixture.sender.CountOfType("document"), 1);
}

TEST(BridgeReportsMovingAMissingNode)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send(R"({"type":"moveNode","id":"ghost","x":1,"y":2})");

    CHECK_FALSE(fixture.sender.LastOfType("error").IsNull());
    CHECK_EQ(fixture.sender.CountOfType("document"), 0);
}

// --- adding and removing ----------------------------------------------------

TEST(BridgeAddsANode)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send(R"({"type":"addNode","nodeType":"branch","x":10,"y":20})");

    CHECK_EQ(fixture.story.nodes.size(), std::size_t(3));

    const Node& added = fixture.story.nodes.back();
    CHECK_EQ(added.type, std::string("branch"));
    CHECK_EQ(added.position.x, 10.0);
    // A new node gets ports so it can be wired immediately.
    CHECK_EQ(added.ports.size(), std::size_t(2));
    CHECK_EQ(added.id, std::string("branch-1"));
}

TEST(BridgeAddsNodesWithDistinctIds)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send(R"({"type":"addNode","nodeType":"branch","x":0,"y":0})");
    fixture.Send(R"({"type":"addNode","nodeType":"branch","x":0,"y":0})");

    CHECK(fixture.story.FindNode("branch-1") != nullptr);
    CHECK(fixture.story.FindNode("branch-2") != nullptr);
    CHECK_EQ(Validate(fixture.story).size(), std::size_t(0));
}

TEST(BridgeReportsAddingWithoutAType)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send(R"({"type":"addNode","x":0,"y":0})");

    CHECK_FALSE(fixture.sender.LastOfType("error").IsNull());
}

TEST(BridgeRemovesANodeAndItsEdges)
{
    Fixture fixture;
    fixture.MakeReady();

    // Wire the dialog to itself, then remove the dialog.
    fixture.Send(R"({"type":"connect","fromNode":"dialog-1","fromPort":"out",)"
                 R"("toNode":"dialog-1","toPort":"in"})");
    CHECK_EQ(fixture.story.edges.size(), std::size_t(1));

    fixture.Send(R"({"type":"removeNode","id":"dialog-1"})");

    CHECK_EQ(fixture.story.nodes.size(), std::size_t(1));
    // Leaving the edge behind would produce a document that fails validation
    // the moment anything touches it.
    CHECK_EQ(fixture.story.edges.size(), std::size_t(0));
    CHECK_EQ(Validate(fixture.story).size(), std::size_t(0));
}

TEST(BridgeReportsRemovingAMissingNode)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send(R"({"type":"removeNode","id":"ghost"})");

    CHECK_FALSE(fixture.sender.LastOfType("error").IsNull());
}

// --- properties -------------------------------------------------------------

TEST(BridgeSetsAProperty)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send(R"({"type":"setProperty","id":"dialog-1","key":"speaker",)"
                 R"("value":"Narrator"})");

    const Node* node = fixture.story.FindNode("dialog-1");
    CHECK(node != nullptr);
    CHECK_EQ(node->data["speaker"].AsString(), std::string("Narrator"));
}

TEST(BridgeSetsPropertiesOfEveryJsonType)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send(R"({"type":"setProperty","id":"dialog-1","key":"count","value":3})");
    fixture.Send(R"({"type":"setProperty","id":"dialog-1","key":"visible","value":true})");
    fixture.Send(R"({"type":"setProperty","id":"dialog-1","key":"list","value":[1,2]})");
    fixture.Send(R"({"type":"setProperty","id":"dialog-1","key":"nothing","value":null})");

    const Node* node = fixture.story.FindNode("dialog-1");
    CHECK_EQ(node->data["count"].AsInt(), std::int64_t(3));
    CHECK_EQ(node->data["visible"].AsBool(), true);
    CHECK_EQ(node->data["list"].Size(), std::size_t(2));
    CHECK(node->data["nothing"].IsNull());
}

TEST(BridgeReportsSettingAPropertyOnAMissingNode)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send(R"({"type":"setProperty","id":"ghost","key":"a","value":1})");

    CHECK_FALSE(fixture.sender.LastOfType("error").IsNull());
}

TEST(BridgeReportsSettingAPropertyWithNoKey)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send(R"({"type":"setProperty","id":"dialog-1","value":1})");

    CHECK_FALSE(fixture.sender.LastOfType("error").IsNull());
}

// --- connecting -------------------------------------------------------------

TEST(BridgeConnectsTwoPorts)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send(R"({"type":"connect","fromNode":"start-1","fromPort":"out",)"
                 R"("toNode":"dialog-1","toPort":"in"})");

    CHECK_EQ(fixture.story.edges.size(), std::size_t(1));
    CHECK_EQ(fixture.story.edges[0].id, std::string("edge-1"));
    CHECK_EQ(fixture.story.edges[0].from.nodeId, std::string("start-1"));
    CHECK_EQ(Validate(fixture.story).size(), std::size_t(0));
}

TEST(BridgeRefusesConnectingAMissingPort)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send(R"({"type":"connect","fromNode":"start-1","fromPort":"nope",)"
                 R"("toNode":"dialog-1","toPort":"in"})");

    CHECK_EQ(fixture.story.edges.size(), std::size_t(0));
    CHECK_FALSE(fixture.sender.LastOfType("error").IsNull());
}

TEST(BridgeRefusesConnectingAnInputToAnInput)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send(R"({"type":"connect","fromNode":"dialog-1","fromPort":"in",)"
                 R"("toNode":"dialog-1","toPort":"in"})");

    CHECK_EQ(fixture.story.edges.size(), std::size_t(0));
    CHECK_FALSE(fixture.sender.LastOfType("error").IsNull());
}

TEST(BridgeDisconnects)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send(R"({"type":"connect","fromNode":"start-1","fromPort":"out",)"
                 R"("toNode":"dialog-1","toPort":"in"})");
    fixture.Send(R"({"type":"disconnect","id":"edge-1"})");

    CHECK_EQ(fixture.story.edges.size(), std::size_t(0));
}

TEST(BridgeReportsDisconnectingAMissingEdge)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send(R"({"type":"disconnect","id":"ghost"})");

    CHECK_FALSE(fixture.sender.LastOfType("error").IsNull());
}

// --- validation -------------------------------------------------------------

TEST(BridgeReportsValidationProblems)
{
    Fixture fixture;
    fixture.MakeReady();

    // A dead end is a warning, and it must reach the page so the interface
    // can show it.
    fixture.Send(R"({"type":"requestDocument"})");

    const json::Value message = fixture.sender.LastOfType("validation");
    CHECK_FALSE(message.IsNull());
    CHECK(message["problems"].IsArray());
    CHECK(message["problems"].Size() > 0);
}

TEST(BridgeValidationIsCleanForACompleteDocument)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send(R"({"type":"connect","fromNode":"start-1","fromPort":"out",)"
                 R"("toNode":"dialog-1","toPort":"in"})");

    // Still a dead end at the dialog, so there is a warning, but no errors.
    const json::Value message = fixture.sender.LastOfType("validation");
    for (const json::Value& problem : message["problems"].AsArray())
    {
        CHECK_EQ(problem["severity"].AsString(), std::string("warning"));
    }
}

// --- replacing the document -------------------------------------------------

TEST(BridgeReplacesTheDocument)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send(R"({"type":"replaceDocument","document":{)"
                 R"("formatVersion":1,"id":"other","title":"Replaced",)"
                 R"("nodes":[{"id":"n1","type":"start","ports":[]}],"edges":[]}})");

    CHECK_EQ(fixture.story.title, std::string("Replaced"));
    CHECK_EQ(fixture.story.nodes.size(), std::size_t(1));
    CHECK_EQ(fixture.sender.CountOfType("document"), 1);
}

TEST(BridgeRefusesAReplacementThatIsNotADocument)
{
    Fixture fixture;
    fixture.MakeReady();
    const std::string originalTitle = fixture.story.title;

    fixture.Send(R"({"type":"replaceDocument","document":"not an object"})");

    CHECK_FALSE(fixture.sender.LastOfType("error").IsNull());
    // The document must be untouched, not half-replaced.
    CHECK_EQ(fixture.story.title, originalTitle);
}

TEST(BridgeRefusesAReplacementWithANewerFormatVersion)
{
    Fixture fixture;
    fixture.MakeReady();
    const std::string originalTitle = fixture.story.title;

    // A document from a future build must be refused here for the same reason
    // a file is refused on load: accepting it would let the editor rewrite
    // parts it does not understand.
    fixture.Send(R"({"type":"replaceDocument","document":{)"
                 R"("formatVersion":99,"id":"other","title":"From The Future",)"
                 R"("nodes":[],"edges":[]}})");

    CHECK_EQ(fixture.story.title, originalTitle);
}

// --- the changed flag -------------------------------------------------------
//
// The host marks the document unsaved when this flag is set. Reporting a
// change for every message would make a freshly opened document look modified
// the moment the page said hello, and a user would be asked to save work they
// had not done.

TEST(ReadyDoesNotReportAChange)
{
    Fixture fixture;
    CHECK_FALSE(fixture.Send(R"({"type":"ready"})"));
}

TEST(RequestingTheDocumentDoesNotReportAChange)
{
    Fixture fixture;
    fixture.MakeReady();
    CHECK_FALSE(fixture.Send(R"({"type":"requestDocument"})"));
}

TEST(MalformedInputDoesNotReportAChange)
{
    Fixture fixture;
    fixture.MakeReady();
    CHECK_FALSE(fixture.Send("not json"));
    CHECK_FALSE(fixture.Send(R"({"type":"nonsense"})"));
}

TEST(RefusedEditsDoNotReportAChange)
{
    Fixture fixture;
    fixture.MakeReady();

    // Each of these is rejected, so the document is untouched and the host
    // must not be told otherwise.
    CHECK_FALSE(fixture.Send(R"({"type":"moveNode","id":"ghost","x":1,"y":1})"));
    CHECK_FALSE(fixture.Send(R"({"type":"removeNode","id":"ghost"})"));
    CHECK_FALSE(fixture.Send(R"({"type":"setProperty","id":"ghost","key":"a","value":1})"));
    CHECK_FALSE(fixture.Send(R"({"type":"connect","fromNode":"a","fromPort":"b",)"
                             R"("toNode":"c","toPort":"d"})"));
    CHECK_FALSE(fixture.Send(R"({"type":"disconnect","id":"ghost"})"));
}

TEST(AcceptedEditsReportAChange)
{
    Fixture fixture;
    fixture.MakeReady();

    CHECK(fixture.Send(R"({"type":"moveNode","id":"dialog-1","x":10,"y":20})"));
    CHECK(fixture.Send(R"({"type":"addNode","nodeType":"branch","x":0,"y":0})"));
    CHECK(fixture.Send(R"({"type":"setProperty","id":"dialog-1","key":"speaker","value":"x"})"));
    CHECK(fixture.Send(R"({"type":"connect","fromNode":"start-1","fromPort":"out",)"
                       R"("toNode":"dialog-1","toPort":"in"})"));
    CHECK(fixture.Send(R"({"type":"disconnect","id":"edge-1"})"));
}

TEST(ARefusedReplacementDoesNotReportAChange)
{
    Fixture fixture;
    fixture.MakeReady();

    // The document is left alone, so the host must not be told it changed.
    CHECK_FALSE(fixture.Send(R"({"type":"replaceDocument","document":{)"
                             R"("formatVersion":99,"id":"x","title":"y",)"
                             R"("nodes":[],"edges":[]}})"));
}

TEST(AnAcceptedReplacementReportsAChange)
{
    Fixture fixture;
    fixture.MakeReady();

    CHECK(fixture.Send(R"({"type":"replaceDocument","document":{)"
                       R"("formatVersion":1,"id":"x","title":"y",)"
                       R"("nodes":[{"id":"n","type":"start","ports":[]}],"edges":[]}})"));
}

// --- undo and redo ----------------------------------------------------------

TEST(BridgeUndoesAMove)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send(R"({"type":"moveNode","id":"dialog-1","x":900,"y":900})");
    CHECK_EQ(fixture.story.FindNode("dialog-1")->position.x, 900.0);

    CHECK(fixture.Send(R"({"type":"undo"})"));
    CHECK_EQ(fixture.story.FindNode("dialog-1")->position.x, 400.0);
}

TEST(BridgeRedoesAMove)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send(R"({"type":"moveNode","id":"dialog-1","x":900,"y":900})");
    fixture.Send(R"({"type":"undo"})");
    CHECK(fixture.Send(R"({"type":"redo"})"));

    CHECK_EQ(fixture.story.FindNode("dialog-1")->position.x, 900.0);
}

TEST(BridgeUndoingWithNothingToUndoChangesNothing)
{
    Fixture fixture;
    fixture.MakeReady();

    // A key repeat, or a second press after the stack ran out. Not an error:
    // the page is told the depths and stops offering the command.
    CHECK_FALSE(fixture.Send(R"({"type":"undo"})"));
    CHECK_FALSE(fixture.Send(R"({"type":"redo"})"));
    CHECK(fixture.sender.LastOfType("error").IsNull());
}

TEST(BridgeUndoesARemovalAndItsEdges)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send(R"({"type":"connect","fromNode":"start-1","fromPort":"out",)"
                 R"("toNode":"dialog-1","toPort":"in"})");
    fixture.Send(R"({"type":"removeNode","id":"dialog-1"})");
    CHECK(fixture.story.FindNode("dialog-1") == nullptr);

    fixture.Send(R"({"type":"undo"})");

    CHECK(fixture.story.FindNode("dialog-1") != nullptr);
    CHECK_EQ(fixture.story.edges.size(), std::size_t(1));
    CHECK_EQ(Validate(fixture.story).size(), std::size_t(0));
}

TEST(BridgeUndoesAPropertyEdit)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send(R"({"type":"setProperty","id":"dialog-1","key":"speaker",)"
                 R"("value":"Narrator"})");
    fixture.Send(R"({"type":"undo"})");

    CHECK_FALSE(fixture.story.FindNode("dialog-1")->data.Has("speaker"));
}

TEST(ANewEditAfterAnUndoRemovesTheRedo)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send(R"({"type":"moveNode","id":"dialog-1","x":900,"y":900})");
    fixture.Send(R"({"type":"undo"})");

    // The edit from the undone state abandons the future it was going to redo.
    fixture.Send(R"({"type":"moveNode","id":"dialog-1","x":700,"y":700})");
    CHECK_FALSE(fixture.Send(R"({"type":"redo"})"));

    const json::Value history = fixture.sender.LastOfType("history");
    CHECK_EQ(history["canRedo"].AsBool(), false);
    CHECK_EQ(history["canUndo"].AsBool(), true);
}

// --- the history message ----------------------------------------------------
//
// The page must not guess whether undo and redo are available. After a save,
// after the stack is trimmed, and after the host applies an edit of its own,
// only the session knows — and a page that decided for itself would show an
// Undo button that does nothing.

TEST(BridgeSendsTheHistoryOnReady)
{
    Fixture fixture;
    fixture.Send(R"({"type":"ready"})");

    const json::Value history = fixture.sender.LastOfType("history");
    CHECK_FALSE(history.IsNull());
    CHECK_EQ(history["canUndo"].AsBool(), false);
    CHECK_EQ(history["canRedo"].AsBool(), false);
    CHECK_EQ(history["undoDepth"].AsInt(), std::int64_t(0));
}

TEST(BridgeReportsTheDepthsAfterAnEdit)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send(R"({"type":"moveNode","id":"dialog-1","x":900,"y":900})");

    const json::Value history = fixture.sender.LastOfType("history");
    CHECK_EQ(history["canUndo"].AsBool(), true);
    CHECK_EQ(history["undoDepth"].AsInt(), std::int64_t(1));
    CHECK_EQ(history["redoDepth"].AsInt(), std::int64_t(0));
}

TEST(BridgeReportsTheDepthsAfterAnUndo)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send(R"({"type":"moveNode","id":"dialog-1","x":900,"y":900})");
    fixture.Send(R"({"type":"undo"})");

    const json::Value history = fixture.sender.LastOfType("history");
    CHECK_EQ(history["canUndo"].AsBool(), false);
    CHECK_EQ(history["canRedo"].AsBool(), true);
    CHECK_EQ(history["redoDepth"].AsInt(), std::int64_t(1));
}

TEST(BridgeTellsThePageTheDocumentIsDirty)
{
    Fixture fixture;
    fixture.MakeReady();

    // A freshly opened document is not dirty, and the page is told so rather
    // than inferring it from having sent a message.
    CHECK_EQ(fixture.sender.LastOfType("document")["dirty"].AsBool(), false);

    fixture.Send(R"({"type":"moveNode","id":"dialog-1","x":900,"y":900})");
    CHECK_EQ(fixture.sender.LastOfType("document")["dirty"].AsBool(), true);

    fixture.Send(R"({"type":"undo"})");
    CHECK_EQ(fixture.sender.LastOfType("document")["dirty"].AsBool(), false);
}

TEST(BridgeDiscardsTheHistoryOnReplacement)
{
    Fixture fixture;
    fixture.MakeReady();

    fixture.Send(R"({"type":"moveNode","id":"dialog-1","x":900,"y":900})");
    fixture.Send(R"({"type":"replaceDocument","document":{)"
                 R"("formatVersion":1,"id":"x","title":"y",)"
                 R"("nodes":[{"id":"n","type":"start","ports":[]}],"edges":[]}})");

    // The old steps name nodes the new document does not have. An undo from
    // here must not be available.
    CHECK_FALSE(fixture.Send(R"({"type":"undo"})"));
    CHECK_EQ(fixture.sender.LastOfType("history")["canUndo"].AsBool(), false);
    CHECK_EQ(fixture.story.nodes.size(), std::size_t(1));
}
