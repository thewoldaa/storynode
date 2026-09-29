// ---------------------------------------------------------------------------
// Tests for the document model and its validation.
//
// Validate() is the gate that every load and every save passes through, so
// the cases here are the ones a user actually hits: a duplicate id, an edge
// pointing at a port that was renamed, two edges into one input.
// ---------------------------------------------------------------------------

#include "TestFramework.h"

#include "core/Model.h"

using namespace storynode;

namespace {

/// A story with a start node and a dialog node, joined. The shape most tests
/// here start from and then break in one specific way.
Story MakeTwoNodeStory()
{
    Story story;
    story.formatVersion = kFormatVersion;
    story.id = "story-1";
    story.title = "Test";

    Node start;
    start.id = "start-1";
    start.type = "start";
    start.ports.push_back(Port { "out", "Start", Port::Kind::Output, Port::DataType::Flow, false });

    Node dialog;
    dialog.id = "dialog-1";
    dialog.type = "dialog";
    dialog.ports.push_back(Port { "in", "In", Port::Kind::Input, Port::DataType::Flow, false });
    dialog.ports.push_back(Port { "out", "Next", Port::Kind::Output, Port::DataType::Flow, false });

    story.nodes.push_back(std::move(start));
    story.nodes.push_back(std::move(dialog));

    Edge edge;
    edge.id = "edge-1";
    edge.from = Endpoint { "start-1", "out" };
    edge.to = Endpoint { "dialog-1", "in" };
    story.edges.push_back(std::move(edge));

    return story;
}

/// True when `problems` contains an entry with the given code.
bool HasCode(const std::vector<Problem>& problems, const std::string& code)
{
    for (const Problem& problem : problems)
    {
        if (problem.code == code)
        {
            return true;
        }
    }
    return false;
}

/// The problem with the given code, or a default. Callers check HasCode first.
Problem FindByCode(const std::vector<Problem>& problems, const std::string& code)
{
    for (const Problem& problem : problems)
    {
        if (problem.code == code)
        {
            return problem;
        }
    }
    return {};
}

} // namespace

// --- lookup -----------------------------------------------------------------

TEST(FindNodeReturnsNullForMissing)
{
    const Story story = MakeTwoNodeStory();
    CHECK(story.FindNode("start-1") != nullptr);
    CHECK(story.FindNode("nope") == nullptr);
    CHECK(story.HasNode("dialog-1"));
    CHECK_FALSE(story.HasNode("nope"));
}

TEST(FindPortReturnsNullForMissing)
{
    const Story story = MakeTwoNodeStory();
    CHECK(story.FindPort("dialog-1", "in") != nullptr);
    CHECK(story.FindPort("dialog-1", "nope") == nullptr);
    CHECK(story.FindPort("nope", "in") == nullptr);
}

TEST(EdgesAtFindsBothDirections)
{
    const Story story = MakeTwoNodeStory();
    CHECK_EQ(story.EdgesAt(Endpoint { "start-1", "out" }).size(), std::size_t(1));
    CHECK_EQ(story.EdgesAt(Endpoint { "dialog-1", "in" }).size(), std::size_t(1));
    CHECK_EQ(story.EdgesAt(Endpoint { "dialog-1", "out" }).size(), std::size_t(0));
}

// --- id generation ----------------------------------------------------------

TEST(MakeUniqueIdStartsAtOne)
{
    const Story story = MakeEmptyStory("Test");
    // MakeEmptyStory already created "start-1".
    CHECK_EQ(MakeUniqueId(story, "node"), std::string("node-1"));
}

TEST(MakeUniqueIdSkipsTaken)
{
    Story story = MakeEmptyStory("Test");
    Node a;
    a.id = "node-1";
    a.type = "dialog";
    story.nodes.push_back(a);

    CHECK_EQ(MakeUniqueId(story, "node"), std::string("node-2"));
}

TEST(MakeUniqueIdDoesNotCollideWithHigherNumbers)
{
    Story story = MakeEmptyStory("Test");
    Node a;
    a.id = "node-7";
    a.type = "dialog";
    story.nodes.push_back(a);

    // Must be 8, not 2. Reusing a number that is already in the file would
    // create a duplicate the moment the user adds another node.
    CHECK_EQ(MakeUniqueId(story, "node"), std::string("node-8"));
}

TEST(MakeUniqueIdConsidersEdgesToo)
{
    Story story = MakeEmptyStory("Test");
    Edge e;
    e.id = "edge-4";
    story.edges.push_back(e);

    CHECK_EQ(MakeUniqueId(story, "edge"), std::string("edge-5"));
}

TEST(MakeUniqueIdIgnoresIdsThatOnlyLookSimilar)
{
    Story story = MakeEmptyStory("Test");
    Node a;
    a.id = "node-x";   // not a counter id
    a.type = "dialog";
    story.nodes.push_back(a);

    CHECK_EQ(MakeUniqueId(story, "node"), std::string("node-1"));
}

TEST(MakeEmptyStoryHasAStartNode)
{
    const Story story = MakeEmptyStory("Untitled");
    CHECK_EQ(story.formatVersion, kFormatVersion);
    CHECK_EQ(story.title, std::string("Untitled"));
    CHECK_EQ(story.nodes.size(), std::size_t(1));
    CHECK_EQ(story.nodes[0].type, std::string("start"));
    CHECK_EQ(story.edges.size(), std::size_t(0));
}

// --- structural validation --------------------------------------------------

TEST(ValidStoryReportsNothing)
{
    const Story story = MakeTwoNodeStory();
    const std::vector<Problem> problems = Validate(story);
    CHECK_EQ(problems.size(), std::size_t(0));
}

TEST(DetectsDuplicateNodeId)
{
    Story story = MakeTwoNodeStory();
    Node copy = story.nodes[1];
    story.nodes.push_back(copy);

    const std::vector<Problem> problems = Validate(story);
    CHECK(HasCode(problems, "node.id.duplicate"));
}

TEST(DetectsMissingNodeId)
{
    Story story = MakeTwoNodeStory();
    story.nodes[1].id.clear();

    CHECK(HasCode(Validate(story), "node.id.missing"));
}

TEST(DetectsMissingNodeType)
{
    Story story = MakeTwoNodeStory();
    story.nodes[1].type.clear();

    CHECK(HasCode(Validate(story), "node.type.missing"));
}

TEST(DetectsDuplicatePortId)
{
    Story story = MakeTwoNodeStory();
    story.nodes[1].ports.push_back(
        Port { "in", "Duplicate", Port::Kind::Input, Port::DataType::Flow, false });

    CHECK(HasCode(Validate(story), "port.id.duplicate"));
}

TEST(DetectsEdgeToMissingNode)
{
    Story story = MakeTwoNodeStory();
    story.edges[0].to.nodeId = "ghost";

    CHECK(HasCode(Validate(story), "edge.to.missingNode"));
}

TEST(DetectsEdgeToMissingPort)
{
    Story story = MakeTwoNodeStory();
    story.edges[0].to.portId = "renamed";

    CHECK(HasCode(Validate(story), "edge.to.missingPort"));
}

TEST(DetectsEdgeFromAnInputPort)
{
    Story story = MakeTwoNodeStory();
    // Reverse the edge so it starts at what is actually an input.
    story.edges[0].from = Endpoint { "dialog-1", "in" };
    story.edges[0].to = Endpoint { "start-1", "out" };

    CHECK(HasCode(Validate(story), "edge.from.notOutput"));
    CHECK(HasCode(Validate(story), "edge.to.notInput"));
}

TEST(DetectsDuplicateConnection)
{
    Story story = MakeTwoNodeStory();
    Edge duplicate = story.edges[0];
    duplicate.id = "edge-2";
    story.edges.push_back(duplicate);

    CHECK(HasCode(Validate(story), "edge.duplicate"));
}

TEST(DetectsSeveralEdgesIntoASingleInput)
{
    Story story = MakeTwoNodeStory();

    Node other;
    other.id = "dialog-2";
    other.type = "dialog";
    other.ports.push_back(Port { "out", "Next", Port::Kind::Output, Port::DataType::Flow, false });
    story.nodes.push_back(std::move(other));

    Edge second;
    second.id = "edge-2";
    second.from = Endpoint { "dialog-2", "out" };
    second.to = Endpoint { "dialog-1", "in" };   // same input as edge-1
    story.edges.push_back(std::move(second));

    CHECK(HasCode(Validate(story), "port.input.multipleEdges"));
}

TEST(AllowsSeveralEdgesIntoAMultipleInput)
{
    Story story = MakeTwoNodeStory();
    story.nodes[1].ports[0].multiple = true;   // the "in" port

    Node other;
    other.id = "dialog-2";
    other.type = "dialog";
    other.ports.push_back(Port { "out", "Next", Port::Kind::Output, Port::DataType::Flow, false });
    story.nodes.push_back(std::move(other));

    Edge second;
    second.id = "edge-2";
    second.from = Endpoint { "dialog-2", "out" };
    second.to = Endpoint { "dialog-1", "in" };
    story.edges.push_back(std::move(second));

    CHECK_FALSE(HasCode(Validate(story), "port.input.multipleEdges"));
}

TEST(ReportsEveryProblemNotJustTheFirst)
{
    // A validator that stops at the first problem makes the user fix errors
    // one at a time, discovering the next each round. This is the property
    // that prevents that.
    Story story = MakeTwoNodeStory();
    story.nodes[0].id.clear();               // missing id
    story.nodes[1].type.clear();             // missing type
    story.edges[0].to.nodeId = "ghost";      // dangling edge

    const std::vector<Problem> problems = Validate(story);
    CHECK(problems.size() >= 3);
    CHECK(HasCode(problems, "node.id.missing"));
    CHECK(HasCode(problems, "node.type.missing"));
    CHECK(HasCode(problems, "edge.to.missingNode"));
}

TEST(RejectsAFutureFormatVersion)
{
    Story story = MakeTwoNodeStory();
    story.formatVersion = kFormatVersion + 1;

    const std::vector<Problem> problems = Validate(story);
    CHECK(HasCode(problems, "formatVersion.mismatch"));
    CHECK_EQ(FindByCode(problems, "formatVersion.mismatch").severity, Severity::Error);
}

TEST(ProblemsCarryTheSubjectId)
{
    Story story = MakeTwoNodeStory();
    story.nodes[1].type.clear();

    const Problem problem = FindByCode(Validate(story), "node.type.missing");
    CHECK_EQ(problem.subjectId, std::string("dialog-1"));
}

// --- graph validation -------------------------------------------------------

TEST(GraphAcceptsAConnectedStory)
{
    Story story = MakeTwoNodeStory();

    // Add the end node the dialog leads to, so nothing dangles.
    Node end;
    end.id = "end-1";
    end.type = "end";
    end.ports.push_back(Port { "in", "In", Port::Kind::Input, Port::DataType::Flow, false });
    story.nodes.push_back(std::move(end));

    Edge toEnd;
    toEnd.id = "edge-2";
    toEnd.from = Endpoint { "dialog-1", "out" };
    toEnd.to = Endpoint { "end-1", "in" };
    story.edges.push_back(std::move(toEnd));

    const std::vector<Problem> problems = ValidateGraph(story);
    CHECK_EQ(problems.size(), std::size_t(0));
}

TEST(GraphWarnsAboutUnreachableNodes)
{
    Story story = MakeTwoNodeStory();

    Node orphan;
    orphan.id = "orphan-1";
    orphan.type = "dialog";
    story.nodes.push_back(std::move(orphan));

    const std::vector<Problem> problems = ValidateGraph(story);
    CHECK(HasCode(problems, "graph.unreachable"));
    CHECK_EQ(FindByCode(problems, "graph.unreachable").subjectId, std::string("orphan-1"));
}

TEST(GraphWarnsAboutDeadEnds)
{
    const Story story = MakeTwoNodeStory();   // dialog has no outgoing edge

    const std::vector<Problem> problems = ValidateGraph(story);
    CHECK(HasCode(problems, "graph.deadEnd"));
}

TEST(GraphDoesNotWarnAboutEndNodesBeingDeadEnds)
{
    Story story = MakeTwoNodeStory();

    Node end;
    end.id = "end-1";
    end.type = "end";
    story.nodes.push_back(std::move(end));

    Edge toEnd;
    toEnd.id = "edge-2";
    toEnd.from = Endpoint { "dialog-1", "out" };
    toEnd.to = Endpoint { "end-1", "in" };
    story.edges.push_back(std::move(toEnd));

    const std::vector<Problem> problems = ValidateGraph(story);
    CHECK_FALSE(HasCode(problems, "graph.deadEnd"));
}

TEST(GraphWarnsWhenThereIsNoStartNode)
{
    Story story = MakeTwoNodeStory();
    story.nodes[0].type = "dialog";   // no longer a start

    CHECK(HasCode(ValidateGraph(story), "graph.noStart"));
}

TEST(GraphProblemsAreWarningsNotErrors)
{
    // An unfinished story is a valid document. Warnings must not block saving,
    // or a writer could not save work in progress.
    Story story = MakeTwoNodeStory();
    Node orphan;
    orphan.id = "orphan-1";
    orphan.type = "dialog";
    story.nodes.push_back(std::move(orphan));

    for (const Problem& problem : ValidateGraph(story))
    {
        CHECK_EQ(static_cast<int>(problem.severity), static_cast<int>(Severity::Warning));
    }
}

TEST(GraphHandlesACycleWithoutLooping)
{
    // A loop back to an earlier node is normal in a story, and reachability
    // must terminate on it rather than walking forever.
    Story story = MakeTwoNodeStory();

    Edge back;
    back.id = "edge-2";
    back.from = Endpoint { "dialog-1", "out" };
    back.to = Endpoint { "dialog-1", "in" };
    story.edges.push_back(std::move(back));

    const std::vector<Problem> problems = ValidateGraph(story);
    CHECK_FALSE(HasCode(problems, "graph.unreachable"));
}

TEST(GraphAcceptsAnEmptyStory)
{
    Story story;
    const std::vector<Problem> problems = ValidateGraph(story);
    CHECK_EQ(problems.size(), std::size_t(0));
}
