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

Bridge::Bridge(Story& document, Sender sender)
    : _document(document), _sender(std::move(sender))
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
    message.Set("document", json::Parse(Serialize(_document)).value);
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
    std::vector<Problem> problems = Validate(_document);
    for (Problem& problem : ValidateGraph(_document))
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
    SendDocument();
    SendValidation();
}

void Bridge::HandleRequestDocument()
{
    SendDocument();
    SendValidation();
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

    _document = loaded.story;
    _changed = true;

    SendDocument();
    SendValidation();
}

void Bridge::HandleMoveNode(const json::Value& message)
{
    const std::string id = StringField(message, "id");
    Node* node = _document.FindNode(id);
    if (!node)
    {
        SendError("Cannot move node \"" + id + "\": no such node.");
        return;
    }

    node->position.x = NumberField(message, "x", node->position.x);
    node->position.y = NumberField(message, "y", node->position.y);
    _changed = true;

    SendDocument();
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
    node.id = MakeUniqueId(_document, type);
    node.type = type;
    node.position.x = NumberField(message, "x", 0.0);
    node.position.y = NumberField(message, "y", 0.0);

    // A new node gets an input and an output so that it can be wired up
    // immediately. The node type work in a later wave replaces this with
    // per-type port declarations.
    node.ports.push_back(Port { "in", "In", Port::Kind::Input, Port::DataType::Flow, false });
    node.ports.push_back(Port { "out", "Out", Port::Kind::Output, Port::DataType::Flow, false });

    _document.nodes.push_back(std::move(node));
    _changed = true;

    SendDocument();
    SendValidation();
}

void Bridge::HandleRemoveNode(const json::Value& message)
{
    const std::string id = StringField(message, "id");

    std::size_t removedEdges = 0;
    for (auto it = _document.edges.begin(); it != _document.edges.end();)
    {
        if (it->from.nodeId == id || it->to.nodeId == id)
        {
            it = _document.edges.erase(it);
            removedEdges += 1;
        }
        else
        {
            ++it;
        }
    }

    bool removedNode = false;
    for (auto it = _document.nodes.begin(); it != _document.nodes.end(); ++it)
    {
        if (it->id == id)
        {
            _document.nodes.erase(it);
            removedNode = true;
            break;
        }
    }

    if (!removedNode)
    {
        SendError("Cannot remove node \"" + id + "\": no such node.");
        return;
    }

    // Edges go with the node. Leaving them would produce a document that
    // fails validation the instant it is touched.
    (void)removedEdges;
    _changed = true;

    SendDocument();
    SendValidation();
}

void Bridge::HandleSetProperty(const json::Value& message)
{
    const std::string id = StringField(message, "id");
    const std::string key = StringField(message, "key");

    Node* node = _document.FindNode(id);
    if (!node)
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

    node->data.Set(key, *value);
    _changed = true;

    SendDocument();
    SendValidation();
}

void Bridge::HandleConnect(const json::Value& message)
{
    const std::string fromNode = StringField(message, "fromNode");
    const std::string fromPort = StringField(message, "fromPort");
    const std::string toNode = StringField(message, "toNode");
    const std::string toPort = StringField(message, "toPort");

    const Port* from = _document.FindPort(fromNode, fromPort);
    const Port* to = _document.FindPort(toNode, toPort);

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
    edge.id = MakeUniqueId(_document, "edge");
    edge.from = Endpoint { fromNode, fromPort };
    edge.to = Endpoint { toNode, toPort };
    _document.edges.push_back(std::move(edge));
    _changed = true;

    SendDocument();
    SendValidation();
}

void Bridge::HandleDisconnect(const json::Value& message)
{
    const std::string id = StringField(message, "id");

    for (auto it = _document.edges.begin(); it != _document.edges.end(); ++it)
    {
        if (it->id == id)
        {
            _document.edges.erase(it);
            _changed = true;
            SendDocument();
            SendValidation();
            return;
        }
    }

    SendError("Cannot remove edge \"" + id + "\": no such edge.");
}

} // namespace storynode
