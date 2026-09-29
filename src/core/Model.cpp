#include "core/Model.h"

#include <algorithm>
#include <set>

namespace storynode {
namespace {

/// Node types that end a playthrough. Reaching one is a legitimate ending,
/// not a dead end.
bool IsTerminalType(const std::string& type)
{
    return type == "end";
}

/// Node types that begin a playthrough.
bool IsStartType(const std::string& type)
{
    return type == "start";
}

/// Append `<prefix>-<n>` for the smallest n not already taken.
///
/// Scans the existing ids rather than keeping a counter, because the counter
/// would have to survive a load and would then disagree with a hand-edited
/// file. The scan is over a few hundred strings and happens on node creation,
/// not per frame.
std::string NextId(const std::vector<std::string>& taken, const std::string& prefix)
{
    const std::string head = prefix + "-";
    int highest = 0;

    for (const std::string& id : taken)
    {
        if (id.size() <= head.size() || id.compare(0, head.size(), head) != 0)
        {
            continue;
        }
        const std::string digits = id.substr(head.size());
        if (digits.empty() ||
            !std::all_of(digits.begin(), digits.end(),
                         [](unsigned char c) { return c >= '0' && c <= '9'; }))
        {
            continue;
        }
        try
        {
            const int n = std::stoi(digits);
            if (n > highest)
            {
                highest = n;
            }
        }
        catch (...)
        {
            // An id that overflows an int is not a counter this function
            // produced, so it is ignored rather than treated as an error.
        }
    }

    return head + std::to_string(highest + 1);
}

Problem MakeProblem(Severity severity, std::string code, std::string message,
                    std::string subjectId = {})
{
    Problem p;
    p.severity = severity;
    p.code = std::move(code);
    p.message = std::move(message);
    p.subjectId = std::move(subjectId);
    return p;
}

} // namespace

// --- Port -------------------------------------------------------------------

const char* Port::KindName(Kind kind)
{
    switch (kind)
    {
    case Kind::Input:  return "input";
    case Kind::Output: return "output";
    }
    return "input";
}

const char* Port::DataTypeName(DataType type)
{
    switch (type)
    {
    case DataType::Flow:   return "flow";
    case DataType::Text:   return "text";
    case DataType::Number: return "number";
    case DataType::Bool:   return "bool";
    case DataType::Asset:  return "asset";
    }
    return "flow";
}

bool Port::ParseKind(const std::string& text, Kind& out)
{
    if (text == "input")  { out = Kind::Input;  return true; }
    if (text == "output") { out = Kind::Output; return true; }
    return false;
}

bool Port::ParseDataType(const std::string& text, DataType& out)
{
    if (text == "flow")   { out = DataType::Flow;   return true; }
    if (text == "text")   { out = DataType::Text;   return true; }
    if (text == "number") { out = DataType::Number; return true; }
    if (text == "bool")   { out = DataType::Bool;   return true; }
    if (text == "asset")  { out = DataType::Asset;  return true; }
    return false;
}

// --- Story lookup -----------------------------------------------------------

const Node* Story::FindNode(const std::string& nodeId) const
{
    for (const Node& node : nodes)
    {
        if (node.id == nodeId)
        {
            return &node;
        }
    }
    return nullptr;
}

Node* Story::FindNode(const std::string& nodeId)
{
    for (Node& node : nodes)
    {
        if (node.id == nodeId)
        {
            return &node;
        }
    }
    return nullptr;
}

const Edge* Story::FindEdge(const std::string& edgeId) const
{
    for (const Edge& edge : edges)
    {
        if (edge.id == edgeId)
        {
            return &edge;
        }
    }
    return nullptr;
}

Edge* Story::FindEdge(const std::string& edgeId)
{
    for (Edge& edge : edges)
    {
        if (edge.id == edgeId)
        {
            return &edge;
        }
    }
    return nullptr;
}

const Port* Story::FindPort(const std::string& nodeId, const std::string& portId) const
{
    const Node* node = FindNode(nodeId);
    if (!node)
    {
        return nullptr;
    }
    for (const Port& port : node->ports)
    {
        if (port.id == portId)
        {
            return &port;
        }
    }
    return nullptr;
}

std::vector<const Edge*> Story::EdgesAt(const Endpoint& endpoint) const
{
    std::vector<const Edge*> result;
    for (const Edge& edge : edges)
    {
        if ((edge.from.nodeId == endpoint.nodeId && edge.from.portId == endpoint.portId) ||
            (edge.to.nodeId == endpoint.nodeId && edge.to.portId == endpoint.portId))
        {
            result.push_back(&edge);
        }
    }
    return result;
}

// --- construction -----------------------------------------------------------

std::string MakeUniqueId(const Story& story, const std::string& prefix)
{
    std::vector<std::string> taken;
    taken.reserve(story.nodes.size() + story.edges.size());

    for (const Node& node : story.nodes)
    {
        taken.push_back(node.id);
    }
    for (const Edge& edge : story.edges)
    {
        taken.push_back(edge.id);
    }

    return NextId(taken, prefix);
}

Story MakeEmptyStory(const std::string& title)
{
    Story story;
    story.formatVersion = kFormatVersion;
    story.title = title;
    story.id = "story-1";

    Node start;
    start.id = "start-1";
    start.type = "start";
    start.position = Vec2 { 80.0, 120.0 };
    start.ports.push_back(Port { "out", "Start", Port::Kind::Output, Port::DataType::Flow, false });
    story.nodes.push_back(std::move(start));

    return story;
}

// --- validation -------------------------------------------------------------

std::vector<Problem> Validate(const Story& story)
{
    std::vector<Problem> problems;

    if (story.formatVersion != kFormatVersion)
    {
        problems.push_back(MakeProblem(
            Severity::Error, "formatVersion.mismatch",
            "Document format version is " + std::to_string(story.formatVersion) +
                ", this build writes version " + std::to_string(kFormatVersion) + "."));
    }

    if (story.id.empty())
    {
        problems.push_back(MakeProblem(
            Severity::Error, "story.id.missing", "The story has no id."));
    }

    // -- node ids ------------------------------------------------------------

    std::set<std::string> nodeIds;
    for (const Node& node : story.nodes)
    {
        if (node.id.empty())
        {
            problems.push_back(MakeProblem(
                Severity::Error, "node.id.missing",
                "A node has no id. Every node needs one so that edges and "
                "undo records can name it."));
            continue;
        }

        if (!nodeIds.insert(node.id).second)
        {
            problems.push_back(MakeProblem(
                Severity::Error, "node.id.duplicate",
                "Two nodes share the id \"" + node.id + "\".", node.id));
        }

        if (node.type.empty())
        {
            problems.push_back(MakeProblem(
                Severity::Error, "node.type.missing",
                "Node \"" + node.id + "\" has no type.", node.id));
        }

        if (!node.data.IsObject())
        {
            problems.push_back(MakeProblem(
                Severity::Error, "node.data.notObject",
                "Node \"" + node.id + "\" has properties that are not an object.",
                node.id));
        }

        // -- port ids, per node ---------------------------------------------

        std::set<std::string> portIds;
        for (const Port& port : node.ports)
        {
            if (port.id.empty())
            {
                problems.push_back(MakeProblem(
                    Severity::Error, "port.id.missing",
                    "Node \"" + node.id + "\" has a port with no id.", node.id));
                continue;
            }
            if (!portIds.insert(port.id).second)
            {
                problems.push_back(MakeProblem(
                    Severity::Error, "port.id.duplicate",
                    "Node \"" + node.id + "\" has two ports with the id \"" +
                        port.id + "\".", node.id));
            }
        }
    }

    // -- edge ids ------------------------------------------------------------

    std::set<std::string> edgeIds;
    for (const Edge& edge : story.edges)
    {
        if (edge.id.empty())
        {
            problems.push_back(MakeProblem(
                Severity::Error, "edge.id.missing",
                "An edge has no id. Every edge needs one so that it can be "
                "removed and undone."));
            continue;
        }
        if (!edgeIds.insert(edge.id).second)
        {
            problems.push_back(MakeProblem(
                Severity::Error, "edge.id.duplicate",
                "Two edges share the id \"" + edge.id + "\".", edge.id));
        }
    }

    // -- edges ---------------------------------------------------------------
    //
    // Each edge is checked against the node and port tables built above, so a
    // single pass reports every dangling reference rather than stopping at
    // the first.

    std::set<std::string> connectionKeys;

    for (const Edge& edge : story.edges)
    {
        const Node* fromNode = story.FindNode(edge.from.nodeId);
        const Node* toNode = story.FindNode(edge.to.nodeId);

        if (!fromNode)
        {
            problems.push_back(MakeProblem(
                Severity::Error, "edge.from.missingNode",
                "Edge \"" + edge.id + "\" starts at a node that does not exist (\"" +
                    edge.from.nodeId + "\").", edge.id));
        }
        if (!toNode)
        {
            problems.push_back(MakeProblem(
                Severity::Error, "edge.to.missingNode",
                "Edge \"" + edge.id + "\" ends at a node that does not exist (\"" +
                    edge.to.nodeId + "\").", edge.id));
        }

        if (fromNode)
        {
            const Port* port = story.FindPort(edge.from.nodeId, edge.from.portId);
            if (!port)
            {
                problems.push_back(MakeProblem(
                    Severity::Error, "edge.from.missingPort",
                    "Edge \"" + edge.id + "\" starts at port \"" + edge.from.portId +
                        "\", which node \"" + edge.from.nodeId + "\" does not have.",
                    edge.id));
            }
            else if (port->kind != Port::Kind::Output)
            {
                problems.push_back(MakeProblem(
                    Severity::Error, "edge.from.notOutput",
                    "Edge \"" + edge.id + "\" starts at \"" + edge.from.portId +
                        "\", which is an input. Edges run from an output to an input.",
                    edge.id));
            }
        }

        if (toNode)
        {
            const Port* port = story.FindPort(edge.to.nodeId, edge.to.portId);
            if (!port)
            {
                problems.push_back(MakeProblem(
                    Severity::Error, "edge.to.missingPort",
                    "Edge \"" + edge.id + "\" ends at port \"" + edge.to.portId +
                        "\", which node \"" + edge.to.nodeId + "\" does not have.",
                    edge.id));
            }
            else if (port->kind != Port::Kind::Input)
            {
                problems.push_back(MakeProblem(
                    Severity::Error, "edge.to.notInput",
                    "Edge \"" + edge.id + "\" ends at \"" + edge.to.portId +
                        "\", which is an output. Edges run from an output to an input.",
                    edge.id));
            }
        }

        // -- duplicate connection -------------------------------------------

        const std::string key = edge.from.nodeId + "\x1f" + edge.from.portId + "\x1f" +
                                edge.to.nodeId + "\x1f" + edge.to.portId;
        if (!connectionKeys.insert(key).second)
        {
            problems.push_back(MakeProblem(
                Severity::Error, "edge.duplicate",
                "Edge \"" + edge.id + "\" repeats a connection that already exists.",
                edge.id));
        }
    }

    // -- single-edge inputs --------------------------------------------------
    //
    // An input port that does not accept several edges may have at most one.
    // Checked after the per-edge pass so the message can name both edges.

    for (const Node& node : story.nodes)
    {
        for (const Port& port : node.ports)
        {
            if (port.kind != Port::Kind::Input || port.multiple)
            {
                continue;
            }
            std::vector<std::string> attached;
            for (const Edge& edge : story.edges)
            {
                if (edge.to.nodeId == node.id && edge.to.portId == port.id)
                {
                    attached.push_back(edge.id);
                }
            }
            if (attached.size() > 1)
            {
                std::string names;
                for (std::size_t i = 0; i < attached.size(); ++i)
                {
                    if (i > 0) { names += ", "; }
                    names += "\"" + attached[i] + "\"";
                }
                problems.push_back(MakeProblem(
                    Severity::Error, "port.input.multipleEdges",
                    "Port \"" + port.id + "\" on node \"" + node.id +
                        "\" takes one connection but has " +
                        std::to_string(attached.size()) + ": " + names + ".",
                    node.id));
            }
        }
    }

    return problems;
}

std::vector<Problem> ValidateGraph(const Story& story)
{
    std::vector<Problem> problems;

    if (story.nodes.empty())
    {
        return problems;
    }

    // -- reachability --------------------------------------------------------

    std::vector<std::string> roots;
    for (const Node& node : story.nodes)
    {
        if (IsStartType(node.type))
        {
            roots.push_back(node.id);
        }
    }

    if (roots.empty())
    {
        // Without a start node the story cannot be played at all, which is a
        // graph-level problem rather than a structural one: the document is
        // still valid and still saves.
        problems.push_back(MakeProblem(
            Severity::Warning, "graph.noStart",
            "The story has no start node, so there is nowhere to begin."));
    }

    std::set<std::string> reached;
    std::vector<std::string> frontier = roots;
    for (const std::string& id : frontier)
    {
        reached.insert(id);
    }

    while (!frontier.empty())
    {
        const std::string current = frontier.back();
        frontier.pop_back();

        for (const Edge& edge : story.edges)
        {
            if (edge.from.nodeId != current)
            {
                continue;
            }
            if (reached.insert(edge.to.nodeId).second)
            {
                frontier.push_back(edge.to.nodeId);
            }
        }
    }

    for (const Node& node : story.nodes)
    {
        if (!reached.count(node.id))
        {
            problems.push_back(MakeProblem(
                Severity::Warning, "graph.unreachable",
                "Node \"" + node.id + "\" cannot be reached from the start.",
                node.id));
        }
    }

    // -- dead ends -----------------------------------------------------------
    //
    // A node with no outgoing edge ends the playthrough. That is correct for
    // an end node and a mistake everywhere else.

    for (const Node& node : story.nodes)
    {
        if (IsTerminalType(node.type) || IsStartType(node.type))
        {
            continue;
        }

        bool hasOutgoing = false;
        for (const Edge& edge : story.edges)
        {
            if (edge.from.nodeId == node.id)
            {
                hasOutgoing = true;
                break;
            }
        }

        if (!hasOutgoing)
        {
            problems.push_back(MakeProblem(
                Severity::Warning, "graph.deadEnd",
                "Node \"" + node.id + "\" has no outgoing connection, so the "
                "story stops there.", node.id));
        }
    }

    return problems;
}

} // namespace storynode
