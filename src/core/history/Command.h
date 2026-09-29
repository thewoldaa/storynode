// ---------------------------------------------------------------------------
// One reversible edit to a document.
//
// A command is a value, not a closure. It carries the data needed to apply it
// and the data needed to take it back, which is what lets an undo work after a
// save, after another undo, and after the document has been through the file
// format. A closure that mutates in place can do none of those, and an
// operation that edits the model directly cannot be undone at all without
// snapshotting the whole document.
//
// Commands are built from the document they are about to change, so the
// "before" state is captured at the moment the user asked for the edit rather
// than reconstructed later from a document that has moved on since.
// ---------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/Model.h"
#include "core/json/JsonValue.h"

namespace storynode {

/// One reversible edit to a document.
struct Command
{
    /// Which edit this is.
    ///
    /// `None` is what a factory returns when it cannot build the command at
    /// all — a move of a node that does not exist, a removal of an edge that
    /// is not there. Callers check for it rather than applying a command that
    /// would do nothing and then be reported as a change.
    enum class Kind
    {
        None = 0,
        MoveNode,
        AddNode,
        RemoveNode,
        SetProperty,
        Connect,
        Disconnect,
    };

    /// MoveNode: the node, and the two positions.
    struct Move
    {
        std::string nodeId;
        Vec2 from;   ///< Where the node was.
        Vec2 to;     ///< Where it is.
    };

    /// SetProperty: one key of one node's data, before and after.
    struct Property
    {
        std::string nodeId;
        std::string key;

        /// The value the key held. Only meaningful when `hadBefore` is set.
        json::Value before;

        /// False when the key did not exist, which is what an undo has to
        /// restore — setting the key back to null would leave the document
        /// with a key it did not have.
        bool hadBefore = false;

        json::Value after;
    };

    /// An edge and the index it occupied in the document's edge list.
    ///
    /// The index is recorded so that a revert restores the edge order as well
    /// as the edge. The file format writes edges in list order, so an undo
    /// followed by a save should produce the file the user had before the
    /// edit, not the same edges in a different order.
    struct PlacedEdge
    {
        std::size_t index = 0;
        Edge edge;
    };

    Kind kind = Kind::None;

    Move move;                             ///< MoveNode.

    Node node;                             ///< AddNode and RemoveNode.
    std::size_t nodeIndex = 0;             ///< RemoveNode: where the node sat.
    std::vector<PlacedEdge> detachedEdges; ///< RemoveNode: the edges that went with it.

    Property property;                     ///< SetProperty.

    Edge edge;                             ///< Connect and Disconnect.
    std::size_t edgeIndex = 0;             ///< Disconnect: where the edge sat.

    // -- construction ---------------------------------------------------------
    //
    // Named factories rather than a public constructor per kind. Each one
    // takes the document it is about to change, so a command cannot be built
    // against a document it does not match — and each returns Kind::None
    // rather than a half-built command when the edit does not apply.

    /// A move of `nodeId` from `from` to `to`.
    static Command MakeMoveNode(std::string nodeId, Vec2 from, Vec2 to);

    /// An insertion of `node`, at the end of the node list.
    static Command MakeAddNode(Node node);

    /// A removal of `nodeId` and of every edge attached to it.
    ///
    /// The node, its index, and the edges it took with it are all captured
    /// here, because after the removal the document no longer knows them.
    static Command MakeRemoveNode(const Story& story, const std::string& nodeId);

    /// An assignment of `value` to `key` on `nodeId`.
    static Command MakeSetProperty(const Story& story, const std::string& nodeId,
                                   std::string key, json::Value value);

    /// An insertion of `edge`.
    static Command MakeConnect(Edge edge);

    /// A removal of `edgeId`, remembering where it sat.
    static Command MakeDisconnect(const Story& story, const std::string& edgeId);

    // -- use ------------------------------------------------------------------

    /// True when applying this command would leave the document exactly as it
    /// is, so there is nothing to record and nothing to undo.
    ///
    /// Two cases. A move that ends where it started — the user pressed the
    /// button and let go without moving — and a property set to the value it
    /// already has, which the inspector sends when a field is focused and
    /// blurred without being typed into. Recording either would leave a step
    /// in the stack that undoes to the state the user is already looking at.
    bool IsNoop(const Story& story) const;

    /// Apply the edit. Returns false, and changes nothing, when the document
    /// does not contain what the command names.
    bool Apply(Story& story) const;

    /// Take the edit back. Returns false, and changes nothing, on the same
    /// terms as Apply.
    bool Revert(Story& story) const;
};

} // namespace storynode
