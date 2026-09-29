// ---------------------------------------------------------------------------
// The message bridge between the page and the host.
//
// Every message is a JSON object with a "type" field. The page sends intent
// ("move this node"); the host applies it to the document and broadcasts a
// fresh snapshot. The page never patches its own copy optimistically, so the
// two sides cannot drift into disagreeing about the document.
//
// This class is deliberately free of WebView2 types. It is the dispatcher,
// not the transport: the host calls HandleMessage with text that arrived from
// the web view, and the bridge calls back with text to send. That separation
// is what lets the bridge be tested without a window, which the tests do.
// ---------------------------------------------------------------------------

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/Model.h"

namespace storynode {

/// What a message handler decided to do.
struct BridgeReply
{
    /// Text to send back to the page. Empty means send nothing.
    std::string reply;

    /// True when the document changed and the page needs a fresh snapshot.
    bool documentChanged = false;
};

/// Dispatches messages between the page and the document.
///
/// The host owns the document and the sender function; the bridge owns the
/// protocol. Adding a message type is a change here and in the page's script,
/// and never a change to the host's window or WebView2 code.
class Bridge
{
public:
    /// Sends text to the page. Supplied by the host, which is the only part
    /// that knows how to talk to the web view.
    using Sender = std::function<void(const std::string&)>;

    /// The bridge reads and writes this document directly. It is the host's
    /// document, not a copy, so a change here is visible to the rest of the
    /// application.
    Bridge(Story& document, Sender sender);

    /// Handle one message from the page.
    ///
    /// Returns true when the document was modified. The host needs this to
    /// decide whether the document is now unsaved: marking it dirty on every
    /// message would make a freshly opened document look modified the moment
    /// the page said hello.
    ///
    /// Never throws: a malformed message produces an error reply, because a
    /// crash here takes the whole editor down and a bad message is not worth
    /// that.
    bool HandleMessage(const std::string& text);

    /// Send the full document to the page.
    void SendDocument();

    /// Send the current validation results.
    void SendValidation();

    /// Send a message of the given type with a message field.
    void SendError(const std::string& message);

    /// Send an arbitrary object to the page. Used by the host for the ready
    /// handshake and by tests.
    void Send(const std::string& json);

    /// True once the page has sent its ready message. Nothing is sent before
    /// that: ExecuteScript calls made during navigation are dropped silently,
    /// and the page would appear to load with no data.
    bool PageReady() const { return _pageReady; }

    /// Number of messages received. For diagnostics and tests.
    int MessagesHandled() const { return _messagesHandled; }

private:
    /// The page script has loaded and can receive messages.
    void HandleReady();

    /// Replace the document. Sent as a full snapshot rather than a diff.
    void HandleReplaceDocument(const json::Value& message);

    /// Move a node. Sent once when a drag ends, not per mouse move.
    void HandleMoveNode(const json::Value& message);

    /// Add a node of the given type at the given position.
    void HandleAddNode(const json::Value& message);

    /// Remove a node and every edge attached to it.
    void HandleRemoveNode(const json::Value& message);

    /// Set one key in a node's properties.
    void HandleSetProperty(const json::Value& message);

    /// Connect two ports.
    void HandleConnect(const json::Value& message);

    /// Remove an edge.
    void HandleDisconnect(const json::Value& message);

    /// Ask for the document to be sent again.
    void HandleRequestDocument();

    /// Run the handlers and report whether they changed the document.
    ///
    /// Every mutating handler sets `_changed` rather than returning a value,
    /// so a handler cannot forget to report a change: the flag is set at the
    /// point of mutation, where the fact is unambiguous.
    bool Dispatch(const std::string& type, const json::Value& message);

    Story& _document;
    Sender _sender;
    bool _pageReady = false;
    bool _changed = false;
    int _messagesHandled = 0;
};

} // namespace storynode
