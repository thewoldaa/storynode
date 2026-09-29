#include "core/history/Command.h"

#include <algorithm>
#include <utility>

namespace storynode {
namespace {

/// Where `nodeId` sits in the node list, or `fallback` when it is not there.
std::size_t NodeIndexOf(const Story& story, const std::string& nodeId,
                        std::size_t fallback)
{
    for (std::size_t i = 0; i < story.nodes.size(); ++i)
    {
        if (story.nodes[i].id == nodeId)
        {
            return i;
        }
    }
    return fallback;
}

/// True when two positions are the same to the last bit.
///
/// Compared exactly rather than with a tolerance: a drag that ended where it
/// started is a no-op, and a drag that moved a hundredth of a pixel is not.
/// A tolerance would silently discard a small real move.
bool SamePosition(Vec2 a, Vec2 b)
{
    return a.x == b.x && a.y == b.y;
}

/// Insert `edge` at `index`, clamped to the end.
///
/// The clamp matters after an undo that follows a redo of a different command:
/// the recorded index was correct when the command ran, and the stack's order
/// guarantees it still is, but a clamp keeps a stale index from being
/// undefined behaviour if that ever stops being true.
void InsertEdgeAt(Story& story, std::size_t index, Edge edge)
{
    if (index > story.edges.size())
    {
        index = story.edges.size();
    }
    story.edges.insert(story.edges.begin() + static_cast<std::ptrdiff_t>(index),
                       std::move(edge));
}

} // namespace

// --- construction -----------------------------------------------------------

Command Command::MakeMoveNode(std::string nodeId, Vec2 from, Vec2 to)
{
    Command command;
    command.kind = Kind::MoveNode;
    command.move.nodeId = std::move(nodeId);
    command.move.from = from;
    command.move.to = to;
    return command;
}

Command Command::MakeAddNode(Node node)
{
    Command command;
    command.kind = Kind::AddNode;
    command.node = std::move(node);
    return command;
}

Command Command::MakeRemoveNode(const Story& story, const std::string& nodeId)
{
    Command command;
    command.kind = Kind::RemoveNode;

    const std::size_t index = NodeIndexOf(story, nodeId, story.nodes.size());
    if (index >= story.nodes.size())
    {
        command.kind = Kind::None;
        return command;
    }

    command.nodeIndex = index;
    command.node = story.nodes[index];

    // The edges attached to the node are captured with the indices they
    // occupied, in ascending order. Ascending is what makes the revert
    // correct: re-inserting the lowest index first leaves every higher index
    // referring to the position it had before the removal.
    for (std::size_t i = 0; i < story.edges.size(); ++i)
    {
        const Edge& edge = story.edges[i];
        if (edge.from.nodeId == nodeId || edge.to.nodeId == nodeId)
        {
            PlacedEdge placed;
            placed.index = i;
            placed.edge = edge;
            command.detachedEdges.push_back(std::move(placed));
        }
    }

    return command;
}

Command Command::MakeSetProperty(const Story& story, const std::string& nodeId,
                                 std::string key, json::Value value)
{
    Command command;
    command.kind = Kind::SetProperty;

    const Node* node = story.FindNode(nodeId);
    if (!node)
    {
        command.kind = Kind::None;
        return command;
    }

    command.property.nodeId = nodeId;
    command.property.key = std::move(key);

    const json::Value* existing = node->data.Find(command.property.key);
    command.property.hadBefore = existing != nullptr;
    if (existing)
    {
        command.property.before = *existing;
    }

    command.property.after = std::move(value);
    return command;
}

Command Command::MakeConnect(Edge edge)
{
    Command command;
    command.kind = Kind::Connect;
    command.edge = std::move(edge);
    return command;
}

Command Command::MakeDisconnect(const Story& story, const std::string& edgeId)
{
    Command command;
    command.kind = Kind::Disconnect;

    for (std::size_t i = 0; i < story.edges.size(); ++i)
    {
        if (story.edges[i].id == edgeId)
        {
            command.edgeIndex = i;
            command.edge = story.edges[i];
            return command;
        }
    }

    command.kind = Kind::None;
    return command;
}

// --- use --------------------------------------------------------------------

bool Command::IsNoop(const Story& story) const
{
    if (kind == Kind::MoveNode)
    {
        return SamePosition(move.from, move.to);
    }

    if (kind == Kind::SetProperty)
    {
        const Node* target = story.FindNode(property.nodeId);
        if (!target)
        {
            return false;
        }

        const json::Value* current = target->data.Find(property.key);
        if (!property.hadBefore || !current)
        {
            // The key was not there when the command was built. Writing it is
            // a change, whatever it holds — including null, which adds a key
            // the document did not have.
            return false;
        }

        // Compared by serialised form. json::Value has no equality operator,
        // and adding one would be a wider change than this needs; the two
        // values here are small, and a document is not compared this way in a
        // loop.
        return current->Serialize() == property.after.Serialize();
    }

    return false;
}

bool Command::Apply(Story& story) const
{
    switch (kind)
    {
    case Kind::None:
        return false;

    case Kind::MoveNode: {
        Node* target = story.FindNode(move.nodeId);
        if (!target)
        {
            return false;
        }
        target->position = move.to;
        return true;
    }

    case Kind::AddNode:
        // Appended rather than inserted at a recorded index. A new node has no
        // meaningful position in the list, and appending is what the document
        // does for a node that was never there.
        story.nodes.push_back(node);
        return true;

    case Kind::RemoveNode: {
        const std::size_t index = NodeIndexOf(story, node.id, story.nodes.size());
        if (index >= story.nodes.size())
        {
            return false;
        }

        story.nodes.erase(story.nodes.begin() + static_cast<std::ptrdiff_t>(index));

        // Descending, so that erasing a lower index does not shift a higher
        // one that has not been erased yet.
        for (std::size_t i = detachedEdges.size(); i > 0; --i)
        {
            const std::string& removedId = detachedEdges[i - 1].edge.id;
            for (auto it = story.edges.begin(); it != story.edges.end(); ++it)
            {
                if (it->id == removedId)
                {
                    story.edges.erase(it);
                    break;
                }
            }
        }
        return true;
    }

    case Kind::SetProperty: {
        Node* target = story.FindNode(property.nodeId);
        if (!target)
        {
            return false;
        }
        target->data.Set(property.key, property.after);
        return true;
    }

    case Kind::Connect:
        story.edges.push_back(edge);
        return true;

    case Kind::Disconnect: {
        for (auto it = story.edges.begin(); it != story.edges.end(); ++it)
        {
            if (it->id == edge.id)
            {
                story.edges.erase(it);
                return true;
            }
        }
        return false;
    }
    }

    return false;
}

bool Command::Revert(Story& story) const
{
    switch (kind)
    {
    case Kind::None:
        return false;

    case Kind::MoveNode: {
        Node* target = story.FindNode(move.nodeId);
        if (!target)
        {
            return false;
        }
        target->position = move.from;
        return true;
    }

    case Kind::AddNode: {
        for (auto it = story.nodes.begin(); it != story.nodes.end(); ++it)
        {
            if (it->id == node.id)
            {
                story.nodes.erase(it);
                return true;
            }
        }
        return false;
    }

    case Kind::RemoveNode: {
        if (story.HasNode(node.id))
        {
            // Something else already put a node with this id back. Refusing is
            // right: inserting a second one would produce a document that
            // fails validation, and a corrupt document is worse than an undo
            // that did nothing.
            return false;
        }

        std::size_t index = nodeIndex;
        if (index > story.nodes.size())
        {
            index = story.nodes.size();
        }
        story.nodes.insert(story.nodes.begin() + static_cast<std::ptrdiff_t>(index),
                           node);

        // Ascending, matching the order the indices were captured in.
        for (const PlacedEdge& placed : detachedEdges)
        {
            InsertEdgeAt(story, placed.index, placed.edge);
        }
        return true;
    }
    case Kind::SetProperty: {
        Node* target = story.FindNode(property.nodeId);
        if (!target)
        {
            return false;
        }
        if (property.hadBefore)
        {
            target->data.Set(property.key, property.before);
        }
        else
        {
            // The key was not there before, so the undo removes it rather than
            // writing null. A null property is a property the inspector would
            // show as present and empty, which is not the document the user
            // had.
            target->data.Remove(property.key);
        }
        return true;
    }

    case Kind::Connect: {
        for (auto it = story.edges.begin(); it != story.edges.end(); ++it)
        {
            if (it->id == edge.id)
            {
                story.edges.erase(it);
                return true;
            }
        }
        return false;
    }

    case Kind::Disconnect:
        InsertEdgeAt(story, edgeIndex, edge);
        return true;
    }

    return false;
}

} // namespace storynode
