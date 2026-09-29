#include "app/Bridge.h"

#include "core/ProjectIO.h"

#include <utility>

namespace storynode {
namespace {

/// Read a string field from a message, or an empty string.
std::string StringField(const json::Value& message, const std::string& key)
{
    return message[key].AsString();
}

double NumberField(const json::Value& message, const std::string& key, double fallback)
{
    return message[key].AsDouble(fallback);
}

/// Build a message object with a type and one payload key.
json::Value MakeMessage(const std::string& type)
{
    json::Value message(json::Object {});
    message.Set("type", json::Value(type));
    return message;
}

} // namespace

Bridge::Bridge(DocumentSession& session, Sender sender)
    : _session(session), _sender(std::move(sender))
{
}

void Bridge::Send(const std::string& json)
{
    if (_sender)
    {
        _sender(json);
    }
}

void Bridge::SendDocument()
{
    if (!_pageReady)
    {
        // Sending before the page is listening is not an error, but it is
        // wasted work: the message is dropped by the web view. The page asks
        // again when it is ready.
        return;
    }

    json::Value message = MakeMessage("document");
    // The document is sent as its serialised form, parsed by the page. That
    // keeps one serialiser authoritative rather than two that can disagree.
    message.Set("document", json::Parse(Serialize(_session.Document())).value);

    // Dirty travels with the document rather than in a message of its own.
    // The title bar and the save prompt both read it, and a page that had to
    // assemble it from a separate message could render a snapshot and a dirty
    // flag that belong to different states.
    message.Set("dirty", json::Value(_session.IsDirty()));

    Send(message.Serialize());
}

void Bridge::SendValidation()
{
    if (!_pageReady)
    {
        return;
    }

    json::Value message = MakeMessage("validation");

    // Both kinds of problem go to the page in one list. Structural problems
    // say the document is broken; graph problems say the story is unfinished.
    // The interface shows them together because a user fixing a story cares
    // about both, and two channels would mean two places to look.
    std::vector<Problem> problems = Validate(_session.Document());
    for (Problem& problem : ValidateGraph(_session.Document()))
    {
        problems.push_back(std::move(problem));
    }

    json::Value list(json::Array {});
    for (const Problem& problem : problems)
    {
        json::Value entry(json::Object {});
        entry.Set("severity",
                  json::Value(problem.severity == Severity::Error ? "error" : "warning"));
        entry.Set("code", json::Value(problem.code));
        entry.Set("message", json::Value(problem.message));
        entry.Set("subjectId", json::Value(problem.subjectId));
        list.Push(std::move(entry));
    }
    message.Set("problems", std::move(list));

    Send(message.Serialize());
}

void Bridge::SendHistory()
{
    if (!_pageReady)
    {
        return;
    }

    // Depths, not just two booleans. The page shows how many steps are
    // available, and a count it derived from its own messages would be wrong
    // the moment the stack was trimmed or the host undid something itself.
    json::Value message = MakeMessage("history");
    message.Set("canUndo", json::Value(_session.CanUndo()));
    message.Set("canRedo", json::Value(_session.CanRedo()));
    message.Set("undoDepth", json::Value(static_cast<std::int64_t>(_session.UndoDepth())));
    message.Set("redoDepth", json::Value(static_cast<std::int64_t>(_session.RedoDepth())));
    Send(message.Serialize());
}

void Bridge::SendAll()
{
    SendDocument();
    SendValidation();
    SendHistory();
}

void Bridge::SendError(const std::string& text)
{
    json::Value message = MakeMessage("error");
    message.Set("message", json::Value(text));
    Send(message.Serialize());
}

bool Bridge::HandleMessage(const std::string& text)
{
    _messagesHandled += 1;
    _changed = false;

    const json::ParseResult parsed = json::Parse(text);
    if (!parsed.ok)
    {
        SendError("Malformed message from the page: " + parsed.Message());
        return false;
    }

    const json::Value& message = parsed.value;
    if (!message.IsObject())
    {
        SendError("Message from the page is not an object.");
        return false;
    }

    const std::string type = StringField(message, "type");
    if (type.empty())
    {
        SendError("Message from the page has no type.");
        return false;
    }

    Dispatch(type, message);
    return _changed;
}

bool Bridge::Dispatch(const std::string& type, const json::Value& message)
{
    if (type == "ready")               { HandleReady(); return false; }
    if (type == "replaceDocument")     { HandleReplaceDocument(message); return _changed; }
    if (type == "moveNode")            { HandleMoveNode(message); return _changed; }
    if (type == "addNode")             { HandleAddNode(message); return _changed; }
    if (type == "removeNode")          { HandleRemoveNode(message); return _changed; }
    if (type == "setProperty")         { HandleSetProperty(message); return _changed; }
    if (type == "connect")             { HandleConnect(message); return _changed; }
    if (type == "disconnect")          { HandleDisconnect(message); return _changed; }
    if (type == "undo")                { HandleUndo(); return _changed; }
    if (type == "redo")                { HandleRedo(); return _changed; }
    if (type == "requestDocument")     { HandleRequestDocument(); return false; }

    // An unknown type is reported rather than ignored. A page and a host that
    // disagree about the protocol should say so, not quietly do nothing while
    // the user wonders why a button does not work.
    SendError("Unknown message type \"" + type + "\".");
    return false;
}

void Bridge::HandleReady()
{
    _pageReady = true;
    SendAll();
}

void Bridge::HandleRequestDocument()
{
    SendAll();
}

void Bridge::HandleReplaceDocument(const json::Value& message)
{
    const json::Value* incoming = message.Find("document");
    if (!incoming || !incoming->IsObject())
    {
        SendError("replaceDocument needs a document object.");
        return;
    }

    // Round-trip through the reader rather than assigning the value directly.
    // That runs the same validation a file load does, so a document sent from
    // the page cannot put the editor into a state that a file could not.
    const LoadResult loaded = Deserialize(incoming->Serialize());

    // Refuse a document that failed to read, and leave the current one alone.
    //
    // Assigning unconditionally would mean a document with, say, a newer
    // format version silently wipes the user's open story and replaces it
    // with the empty shell the reader produced. Losing work is a far worse
    // outcome than refusing an edit, and the page can be told why.
    if (loaded.HasErrors())
    {
        std::string reason = "The document was refused:";
        for (const Problem& problem : loaded.problems)
        {
            if (problem.severity == Severity::Error)
            {
                reason += "\n  " + problem.message;
            }
        }
        SendError(reason);
        return;
    }

    // A replacement is not an edit and must not be undoable. Undoing it would
    // restore the previous document's commands to a stack that no longer has
    // the document they were built against — which is why Reset clears the
    // history rather than recording the swap as a step.
    _session.Reset(loaded.story);
    _changed = true;

    SendAll();
}

void Bridge::HandleMoveNode(const json::Value& message)
{
    const std::string id = StringField(message, "id");
    const Node* node = _session.Document().FindNode(id);
    if (!node)
    {
        SendError("Cannot move node \"" + id + "\": no such node.");
        return;
    }

    const Vec2 from = node->position;
    const Vec2 to { NumberField(message, "x", from.x), NumberField(message, "y", from.y) };

    // Built from the document's current position rather than from what the
    // page sent. The page could be a message behind, and recording its idea of
    // "before" would make the undo land somewhere the node never was.
    _session.Apply(Command::MakeMoveNode(id, from, to));
    _changed = true;

    SendAll();
}

void Bridge::HandleAddNode(const json::Value& message)
{
    const std::string type = StringField(message, "nodeType");
    if (type.empty())
    {
        SendError("addNode needs a nodeType.");
        return;
    }

    Node node;
    node.id = MakeUniqueId(_session.Document(), type);
    node.type = type;
    node.position.x = NumberField(message, "x", 0.0);
    node.position.y = NumberField(message, "y", 0.0);

    // A new node gets an input and an output so that it can be wired up
    // immediately. The node type work in a later wave replaces this with
    // per-type port declarations.
    node.ports.push_back(Port { "in", "In", Port::Kind::Input, Port::DataType::Flow, false });
    node.ports.push_back(Port { "out", "Out", Port::Kind::Output, Port::DataType::Flow, false });

    _session.Apply(Command::MakeAddNode(std::move(node)));
    _changed = true;

    SendAll();
}

void Bridge::HandleRemoveNode(const json::Value& message)
{
    const std::string id = StringField(message, "id");

    // The command captures the node, its index and every edge attached to it,
    // because after the removal the document no longer knows any of them and
    // an undo has nothing to restore from.
    const Command command = Command::MakeRemoveNode(_session.Document(), id);
    if (command.kind == Command::Kind::None)
    {
        SendError("Cannot remove node \"" + id + "\": no such node.");
        return;
    }

    // Edges go with the node. Leaving them would produce a document that
    // fails validation the instant it is touched.
    _session.Apply(command);
    _changed = true;

    SendAll();
}

void Bridge::HandleSetProperty(const json::Value& message)
{
    const std::string id = StringField(message, "id");
    const std::string key = StringField(message, "key");

    if (!_session.Document().FindNode(id))
    {
        SendError("Cannot set a property on node \"" + id + "\": no such node.");
        return;
    }
    if (key.empty())
    {
        SendError("setProperty needs a key.");
        return;
    }

    const json::Value* value = message.Find("value");
    if (!value)
    {
        SendError("setProperty needs a value.");
        return;
    }

    _session.Apply(Command::MakeSetProperty(_session.Document(), id, key, *value));
    _changed = true;

    SendAll();
}

void Bridge::HandleConnect(const json::Value& message)
{
    const std::string fromNode = StringField(message, "fromNode");
    const std::string fromPort = StringField(message, "fromPort");
    const std::string toNode = StringField(message, "toNode");
    const std::string toPort = StringField(message, "toPort");

    const Port* from = _session.Document().FindPort(fromNode, fromPort);
    const Port* to = _session.Document().FindPort(toNode, toPort);

    if (!from || !to)
    {
        SendError("Cannot connect: one of the ports does not exist.");
        return;
    }
    if (from->kind != Port::Kind::Output || to->kind != Port::Kind::Input)
    {
        SendError("Cannot connect: edges run from an output to an input.");
        return;
    }

    Edge edge;
    edge.id = MakeUniqueId(_session.Document(), "edge");
    edge.from = Endpoint { fromNode, fromPort };
    edge.to = Endpoint { toNode, toPort };

    _session.Apply(Command::MakeConnect(std::move(edge)));
    _changed = true;

    SendAll();
}

void Bridge::HandleDisconnect(const json::Value& message)
{
    const std::string id = StringField(message, "id");

    const Command command = Command::MakeDisconnect(_session.Document(), id);
    if (command.kind == Command::Kind::None)
    {
        SendError("Cannot remove edge \"" + id + "\": no such edge.");
        return;
    }

    _session.Apply(command);
    _changed = true;

    SendAll();
}

void Bridge::HandleUndo()
{
    if (!_session.Undo())
    {
        // Not an error. A key repeat on Ctrl+Z, or a second press after the
        // stack ran out, is ordinary; the page is told the new depths and
        // stops offering the command.
        SendHistory();
        return;
    }

    _changed = true;
    SendAll();
}

void Bridge::HandleRedo()
{
    if (!_session.Redo())
    {
        SendHistory();
        return;
    }

    _changed = true;
    SendAll();
}

} // namespace storynode
