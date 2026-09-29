// ---------------------------------------------------------------------------
// The story document: Story, Node, Port, Edge.
//
// Plain types with no Windows dependency and no WebView2 dependency, so they
// build and run in a test process with no window and no message loop. That is
// what lets the model be tested directly and lets the exporters reuse it.
//
// Node behaviour is data, not code. A node declares its ports and its
// property values, and the editor renders it from that declaration. Adding a
// node type is adding a declaration, not a class — which is what keeps the
// node-type work parallelisable across tasks.
// ---------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/json/JsonValue.h"

namespace storynode {

/// A connection point on a node. Ports are typed so that a validator can
/// reject an edge between incompatible endpoints before the user draws it.
struct Port
{
    enum class Kind
    {
        Input = 0,
        Output,
    };

    /// What may travel through the port. `Flow` is a plain continuation;
    /// the others exist so that a future node can require a specific kind of
    /// value and have the editor refuse a mismatched edge.
    enum class DataType
    {
        Flow = 0,
        Text,
        Number,
        Bool,
        Asset,
    };

    std::string id;                    ///< Unique within the node.
    std::string label;                 ///< Shown on the node.
    Kind kind = Kind::Input;
    DataType dataType = DataType::Flow;
    bool multiple = false;             ///< May several edges share this port?

    /// Keys present in the file that this version does not recognise.
    ///
    /// Carried through load and save unchanged so that a document written by
    /// a newer build is not silently stripped by an older one. Every level of
    /// the document has one of these for the same reason; see the note on
    /// Story::extra.
    json::Value extra { json::Object {} };

    /// Parse and serialise helpers, shared with the file format.
    static const char* KindName(Kind kind);
    static const char* DataTypeName(DataType type);
    static bool ParseKind(const std::string& text, Kind& out);
    static bool ParseDataType(const std::string& text, DataType& out);
};

/// A point in graph space. Not screen space: zoom and pan are presentation.
struct Vec2
{
    double x = 0.0;
    double y = 0.0;
};

/// One node in the graph.
struct Node
{
    std::string id;                    ///< Unique within the story.
    std::string type;                  ///< "dialog", "branch", "end", ...
    Vec2 position;
    Vec2 size { 220.0, 120.0 };
    std::vector<Port> ports;

    /// Type-specific properties, kept as a JSON object rather than a struct
    /// so that a node type added later does not require a change to the
    /// document model — and so that properties this version does not know
    /// about survive a save.
    json::Value data { json::Object {} };

    /// Keys present in the file that this version does not recognise. See the
    /// note on Story::extra.
    json::Value extra { json::Object {} };

    /// True when `type` is empty. Such a node cannot be saved.
    bool IsValid() const { return !id.empty() && !type.empty(); }
};

/// One end of an edge: a node and one of its ports.
struct Endpoint
{
    std::string nodeId;
    std::string portId;

    /// Keys present in the file that this version does not recognise.
    ///
    /// Every level of the document carries one of these, including this one.
    /// Without it, a key added to an endpoint by a newer build — a condition,
    /// a delay, a port qualifier — is deleted the first time this build saves
    /// the file. That is silent data loss on a level a reader would not think
    /// to check, because the level above it is handled correctly.
    json::Value extra { json::Object {} };

    bool operator==(const Endpoint& other) const
    {
        return nodeId == other.nodeId && portId == other.portId;
    }
};

/// A connection between two ports.
struct Edge
{
    std::string id;      ///< Unique within the story.
    Endpoint from;       ///< An output port.
    Endpoint to;         ///< An input port.

    /// Keys present in the file that this version does not recognise. See the
    /// note on Story::extra.
    json::Value extra { json::Object {} };
};

/// Severity of a validation problem.
enum class Severity
{
    Warning = 0,  ///< The document is usable; something is probably wrong.
    Error,        ///< The document cannot be saved or exported as it stands.
};

/// One thing wrong with a document.
///
/// Schema validation and graph validation produce the same shape so the
/// interface has one way to display a problem, and so a caller can collect
/// both kinds into one list.
struct Problem
{
    Severity severity = Severity::Error;

    /// Stable machine-readable identifier, for tests and for the interface to
    /// key on. Not shown to users.
    std::string code;

    /// What is wrong, in a sentence, naming the thing by id.
    std::string message;

    /// The node or edge the problem is about, when there is one. Empty for
    /// document-level problems.
    std::string subjectId;

    /// Where in the source file, when the problem came from parsing. Unused
    /// for graph validation.
    json::Location location;
};

/// A story document.
struct Story
{
    /// Format version. Written on save and checked on load, so a file from a
    /// future version is refused with a clear message rather than opened and
    /// silently damaged.
    int formatVersion = 1;

    std::string id;
    std::string title;

    /// Document-level metadata, kept as free-form JSON for the same reason
    /// node data is: unknown keys survive.
    json::Value metadata { json::Object {} };

    std::vector<Node> nodes;
    std::vector<Edge> edges;

    /// Keys present in the file that this version does not recognise, kept so
    /// that load → save is lossless.
    ///
    /// Every level of the document carries one of these. Without it, opening
    /// a file written by a newer build and saving would silently delete
    /// whatever that build added — the worst possible failure, because it
    /// destroys data without telling anyone and only shows up when the user
    /// goes back to the newer version.
    json::Value extra { json::Object {} };

    // -- lookup -------------------------------------------------------------
    //
    // Linear. A story holds hundreds of nodes, not millions, and the cost of
    // maintaining an index exceeds the cost of the scan it replaces.

    const Node* FindNode(const std::string& nodeId) const;
    Node* FindNode(const std::string& nodeId);

    const Edge* FindEdge(const std::string& edgeId) const;
    Edge* FindEdge(const std::string& edgeId);

    /// The port with `portId` on node `nodeId`, or nullptr.
    const Port* FindPort(const std::string& nodeId, const std::string& portId) const;

    /// Edges attached to a port, in either direction.
    std::vector<const Edge*> EdgesAt(const Endpoint& endpoint) const;

    /// True when a node with `nodeId` exists.
    bool HasNode(const std::string& nodeId) const { return FindNode(nodeId) != nullptr; }
};

/// The current format version. Files declaring a higher one are refused.
inline constexpr int kFormatVersion = 1;

/// The file extension for a story document, without the dot.
inline constexpr const char* kProjectExtension = "snproj";

// --- document construction --------------------------------------------------

/// An id that is unique within `story`, of the form `<prefix>-<n>`.
///
/// Sequential rather than random so that a hand-edited file stays readable
/// and a diff stays small. The cost is that ids are guessable, which does not
/// matter for a local document format.
std::string MakeUniqueId(const Story& story, const std::string& prefix);

/// A story with one start node and nothing else.
Story MakeEmptyStory(const std::string& title);

// --- validation -------------------------------------------------------------

/// Check the document's structure: ids are present and unique, ports exist,
/// edges connect an output to an input, and no edge is duplicated.
///
/// Returns every problem found, not the first. A user fixing one error to
/// discover the next is a bad loop, and a validator that stops early cannot
/// be used to drive a list in the interface.
std::vector<Problem> Validate(const Story& story);

/// Check that the graph is playable: every node reachable from a start node,
/// no dead ends, no choices leading nowhere.
///
/// Separate from Validate because an unfinished story is a valid document,
/// just not a valid export. Warnings here do not block saving.
std::vector<Problem> ValidateGraph(const Story& story);

} // namespace storynode
