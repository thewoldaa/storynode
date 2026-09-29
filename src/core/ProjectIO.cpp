#include "core/ProjectIO.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace storynode {
namespace {

// --- reading helpers --------------------------------------------------------

/// Record a problem and return a reference to a null value, so a caller can
/// keep reading a malformed document instead of bailing out. The point is to
/// collect every problem in one pass.
const json::Value& ReportProblem(std::vector<Problem>& problems, Severity severity,
                                 std::string code, std::string message,
                                 std::string subjectId = {})
{
    static const json::Value null_value;

    Problem p;
    p.severity = severity;
    p.code = std::move(code);
    p.message = std::move(message);
    p.subjectId = std::move(subjectId);
    problems.push_back(std::move(p));

    return null_value;
}

/// Read a required string field.
///
/// `code` names the problem so that a caller can report a specific, stable
/// identifier. This matters because the same condition can be caught here or
/// by Validate(), and a caller keying on the code must not have to know which
/// layer got there first: a missing node id is "node.id.missing" whether the
/// reader noticed it or the validator did.
std::string ReadString(const json::Value& owner, const std::string& key,
                       const std::string& where, std::vector<Problem>& problems,
                       const std::string& subjectId, bool required,
                       const std::string& code = "field.missing")
{
    const json::Value* found = owner.Find(key);
    if (!found)
    {
        if (required)
        {
            ReportProblem(problems, Severity::Error, code,
                          where + " is missing the required field \"" + key + "\".",
                          subjectId);
        }
        return {};
    }
    if (!found->IsString())
    {
        ReportProblem(problems, Severity::Error, "field.type",
                      where + " field \"" + key + "\" must be a string.",
                      subjectId);
        return {};
    }
    return found->AsString();
}

double ReadNumber(const json::Value& owner, const std::string& key,
                  const std::string& where, std::vector<Problem>& problems,
                  const std::string& subjectId, double fallback)
{
    const json::Value* found = owner.Find(key);
    if (!found)
    {
        return fallback;
    }
    if (!found->IsNumber())
    {
        ReportProblem(problems, Severity::Error, "field.type",
                      where + " field \"" + key + "\" must be a number.",
                      subjectId);
        return fallback;
    }
    return found->AsDouble();
}

bool ReadBool(const json::Value& owner, const std::string& key,
              const std::string& where, std::vector<Problem>& problems,
              const std::string& subjectId, bool fallback)
{
    const json::Value* found = owner.Find(key);
    if (!found)
    {
        return fallback;
    }
    if (!found->IsBool())
    {
        ReportProblem(problems, Severity::Error, "field.type",
                      where + " field \"" + key + "\" must be true or false.",
                      subjectId);
        return fallback;
    }
    return found->AsBool();
}

/// The keys this version knows about, per level.
///
/// Anything not listed is preserved in `extra` rather than discarded. Keeping
/// the list next to the reader is deliberate: a field added to the reader but
/// forgotten here would be written twice, once from the struct and once from
/// `extra`, so the two must be edited together.
const std::vector<std::string>& StoryKeys()
{
    static const std::vector<std::string> keys = {
        "formatVersion", "id", "title", "metadata", "nodes", "edges",
    };
    return keys;
}

const std::vector<std::string>& NodeKeys()
{
    static const std::vector<std::string> keys = {
        "id", "type", "position", "size", "ports", "data",
    };
    return keys;
}

const std::vector<std::string>& PortKeys()
{
    static const std::vector<std::string> keys = {
        "id", "label", "kind", "dataType", "multiple",
    };
    return keys;
}

const std::vector<std::string>& EdgeKeys()
{
    static const std::vector<std::string> keys = {
        "id", "from", "to",
    };
    return keys;
}

const std::vector<std::string>& EndpointKeys()
{
    static const std::vector<std::string> keys = {
        "nodeId", "portId",
    };
    return keys;
}

/// Copy every member of `source` whose key is not in `known` into `extra`.
void CollectExtra(const json::Value& source, const std::vector<std::string>& known,
                  json::Value& extra)
{
    if (!source.IsObject())
    {
        return;
    }
    extra = json::Value(json::Object {});
    for (const auto& member : source.AsObject())
    {
        bool isKnown = false;
        for (const std::string& key : known)
        {
            if (member.first == key)
            {
                isKnown = true;
                break;
            }
        }
        if (!isKnown)
        {
            extra.Set(member.first, member.second);
        }
    }
}

/// Read `extra` back into an object being serialised, skipping any key that
/// the writer already produced.
void ApplyExtra(const json::Value& extra, const std::vector<std::string>& written,
                json::Value& target)
{
    if (!extra.IsObject())
    {
        return;
    }
    for (const auto& member : extra.AsObject())
    {
        bool alreadyWritten = false;
        for (const std::string& key : written)
        {
            if (member.first == key)
            {
                alreadyWritten = true;
                break;
            }
        }
        if (!alreadyWritten)
        {
            target.Set(member.first, member.second);
        }
    }
}

// --- reading ----------------------------------------------------------------

Port ReadPort(const json::Value& source, const std::string& nodeId, int index,
              std::vector<Problem>& problems)
{
    const std::string where = "Port " + std::to_string(index) +
                              " on node \"" + nodeId + "\"";

    Port port;
    port.id = ReadString(source, "id", where, problems, nodeId, true, "port.id.missing");
    port.label = ReadString(source, "label", where, problems, nodeId, false);
    port.multiple = ReadBool(source, "multiple", where, problems, nodeId, false);

    const std::string kindText = ReadString(source, "kind", where, problems, nodeId, true,
                                            "port.kind.missing");
    if (!kindText.empty() && !Port::ParseKind(kindText, port.kind))
    {
        ReportProblem(problems, Severity::Error, "port.kind.unknown",
                      where + " has an unknown kind \"" + kindText +
                          "\". Expected \"input\" or \"output\".",
                      nodeId);
    }

    const std::string typeText = ReadString(source, "dataType", where, problems, nodeId, false);
    if (!typeText.empty() && !Port::ParseDataType(typeText, port.dataType))
    {
        ReportProblem(problems, Severity::Error, "port.dataType.unknown",
                      where + " has an unknown data type \"" + typeText + "\".",
                      nodeId);
    }

    CollectExtra(source, PortKeys(), port.extra);
    return port;
}

Node ReadNode(const json::Value& source, int index, std::vector<Problem>& problems)
{
    const std::string where = "Node " + std::to_string(index);

    Node node;
    node.id = ReadString(source, "id", where, problems, {}, true, "node.id.missing");
    node.type = ReadString(source, "type", where, problems, node.id, true, "node.type.missing");

    // A node with no id still needs a subject for its own problems, so the
    // position is used until one is read.
    const std::string subject = node.id.empty() ? where : node.id;

    if (const json::Value* position = source.Find("position"))
    {
        if (!position->IsObject())
        {
            ReportProblem(problems, Severity::Error, "node.position.type",
                          where + " field \"position\" must be an object.", subject);
        }
        else
        {
            node.position.x = ReadNumber(*position, "x", where + " position",
                                         problems, subject, 0.0);
            node.position.y = ReadNumber(*position, "y", where + " position",
                                         problems, subject, 0.0);
        }
    }

    if (const json::Value* size = source.Find("size"))
    {
        if (!size->IsObject())
        {
            ReportProblem(problems, Severity::Error, "node.size.type",
                          where + " field \"size\" must be an object.", subject);
        }
        else
        {
            node.size.x = ReadNumber(*size, "x", where + " size", problems, subject, 220.0);
            node.size.y = ReadNumber(*size, "y", where + " size", problems, subject, 120.0);
        }
    }

    if (const json::Value* ports = source.Find("ports"))
    {
        if (!ports->IsArray())
        {
            ReportProblem(problems, Severity::Error, "node.ports.type",
                          where + " field \"ports\" must be an array.", subject);
        }
        else
        {
            int portIndex = 0;
            for (const json::Value& portValue : ports->AsArray())
            {
                if (!portValue.IsObject())
                {
                    ReportProblem(problems, Severity::Error, "port.type",
                                  where + " port " + std::to_string(portIndex) +
                                      " must be an object.",
                                  subject);
                }
                else
                {
                    node.ports.push_back(ReadPort(portValue, subject, portIndex, problems));
                }
                portIndex += 1;
            }
        }
    }

    if (const json::Value* data = source.Find("data"))
    {
        if (!data->IsObject())
        {
            ReportProblem(problems, Severity::Error, "node.data.type",
                          where + " field \"data\" must be an object.", subject);
        }
        else
        {
            node.data = *data;
        }
    }

    CollectExtra(source, NodeKeys(), node.extra);
    return node;
}

Endpoint ReadEndpoint(const json::Value& source, const std::string& edgeId,
                      const std::string& which, std::vector<Problem>& problems)
{
    Endpoint endpoint;
    if (!source.IsObject())
    {
        ReportProblem(problems, Severity::Error, "edge.endpoint.type",
                      "Edge \"" + edgeId + "\" field \"" + which +
                          "\" must be an object.",
                      edgeId);
        return endpoint;
    }

    const std::string where = "Edge \"" + edgeId + "\" " + which + " endpoint";
    endpoint.nodeId = ReadString(source, "nodeId", where, problems, edgeId, true,
                                 "edge.endpoint.missingNodeId");
    endpoint.portId = ReadString(source, "portId", where, problems, edgeId, true,
                                 "edge.endpoint.missingPortId");

    CollectExtra(source, EndpointKeys(), endpoint.extra);
    return endpoint;
}

Edge ReadEdge(const json::Value& source, int index, std::vector<Problem>& problems)
{
    const std::string where = "Edge " + std::to_string(index);

    Edge edge;
    edge.id = ReadString(source, "id", where, problems, {}, true, "edge.id.missing");
    const std::string subject = edge.id.empty() ? where : edge.id;

    if (const json::Value* from = source.Find("from"))
    {
        edge.from = ReadEndpoint(*from, subject, "from", problems);
    }
    else
    {
        ReportProblem(problems, Severity::Error, "field.missing",
                      where + " is missing the required field \"from\".", subject);
    }

    if (const json::Value* to = source.Find("to"))
    {
        edge.to = ReadEndpoint(*to, subject, "to", problems);
    }
    else
    {
        ReportProblem(problems, Severity::Error, "field.missing",
                      where + " is missing the required field \"to\".", subject);
    }

    CollectExtra(source, EdgeKeys(), edge.extra);
    return edge;
}

// --- writing ----------------------------------------------------------------

json::Value WriteEndpoint(const Endpoint& endpoint)
{
    json::Value out(json::Object {});
    out.Set("nodeId", json::Value(endpoint.nodeId));
    out.Set("portId", json::Value(endpoint.portId));
    ApplyExtra(endpoint.extra, EndpointKeys(), out);
    return out;
}

json::Value WritePort(const Port& port)
{
    json::Value out(json::Object {});
    out.Set("id", json::Value(port.id));
    if (!port.label.empty())
    {
        out.Set("label", json::Value(port.label));
    }
    out.Set("kind", json::Value(Port::KindName(port.kind)));
    out.Set("dataType", json::Value(Port::DataTypeName(port.dataType)));
    if (port.multiple)
    {
        out.Set("multiple", json::Value(true));
    }
    ApplyExtra(port.extra, PortKeys(), out);
    return out;
}

json::Value WriteNode(const Node& node)
{
    json::Value position(json::Object {});
    position.Set("x", json::Value(node.position.x));
    position.Set("y", json::Value(node.position.y));

    json::Value size(json::Object {});
    size.Set("x", json::Value(node.size.x));
    size.Set("y", json::Value(node.size.y));

    json::Value ports(json::Array {});
    for (const Port& port : node.ports)
    {
        ports.Push(WritePort(port));
    }

    json::Value out(json::Object {});
    out.Set("id", json::Value(node.id));
    out.Set("type", json::Value(node.type));
    out.Set("position", std::move(position));
    out.Set("size", std::move(size));
    out.Set("ports", std::move(ports));
    out.Set("data", node.data);
    ApplyExtra(node.extra, NodeKeys(), out);
    return out;
}

json::Value WriteEdge(const Edge& edge)
{
    json::Value out(json::Object {});
    out.Set("id", json::Value(edge.id));
    out.Set("from", WriteEndpoint(edge.from));
    out.Set("to", WriteEndpoint(edge.to));
    ApplyExtra(edge.extra, EdgeKeys(), out);
    return out;
}

} // namespace

// --- LoadResult -------------------------------------------------------------

bool LoadResult::HasErrors() const
{
    for (const Problem& problem : problems)
    {
        if (problem.severity == Severity::Error)
        {
            return true;
        }
    }
    return false;
}

// --- Deserialize ------------------------------------------------------------

LoadResult Deserialize(const std::string& text)
{
    LoadResult result;

    const json::ParseResult parsed = json::Parse(text);
    if (!parsed.ok)
    {
        Problem p;
        p.severity = Severity::Error;
        p.code = "json.syntax";
        // The position goes in the message, not only in the Location field.
        // The message is what reaches the user; a caller that forgets to
        // format the location would leave them with "not valid JSON" and no
        // way to find the mistake in a file of several hundred lines.
        p.message = "The file is not valid JSON: " + parsed.Message() + ".";
        p.location = parsed.location;
        result.problems.push_back(std::move(p));
        return result;
    }

    if (!parsed.value.IsObject())
    {
        Problem p;
        p.severity = Severity::Error;
        p.code = "document.notObject";
        p.message = "The file's top level must be a JSON object.";
        result.problems.push_back(std::move(p));
        return result;
    }

    const json::Value& root = parsed.value;
    Story& story = result.story;

    // -- version -------------------------------------------------------------
    //
    // Checked before anything else, and a file from a newer format is refused
    // rather than opened. Reading a document written by a future version and
    // saving it would rewrite the parts this version understands in the old
    // shape, producing a file that neither version handles correctly.

    if (const json::Value* version = root.Find("formatVersion"))
    {
        if (!version->IsNumber())
        {
            ReportProblem(result.problems, Severity::Error, "formatVersion.type",
                          "Field \"formatVersion\" must be a number.");
        }
        else if (!version->IsInteger())
        {
            // 1.9 is not version 1. Truncating it would silently accept a file
            // written by a version this build does not understand, which is
            // the one thing the version field exists to prevent.
            ReportProblem(result.problems, Severity::Error, "formatVersion.type",
                          "Field \"formatVersion\" must be a whole number.");
        }
        else
        {
            const std::int64_t declared = version->AsInt();

            // Everything is decided in 64 bits, and the narrowing to int
            // happens once, at the end, only after the value is known to be in
            // range.
            //
            // Narrowing first is how a file declaring 4294967297 becomes 1,
            // passes the "not newer than this build" check, and is then saved
            // back as version 1 — a silent downgrade of a file the build
            // cannot actually read.
            if (declared < 0)
            {
                ReportProblem(result.problems, Severity::Error, "formatVersion.invalid",
                              "Field \"formatVersion\" must not be negative.");
            }
            else if (declared > kFormatVersion)
            {
                // Refused here, before anything else is read, so a caller
                // cannot end up with a story that looks usable.
                //
                // The message names the number as it appeared in the file
                // rather than a truncated version of it, because the number is
                // the whole point of the message.
                ReportProblem(result.problems, Severity::Error, "formatVersion.tooNew",
                              "This file was written by a newer version of the editor "
                              "(format " + std::to_string(declared) +
                                  ", this build reads up to " +
                                  std::to_string(kFormatVersion) +
                                  "). Opening and saving it here would lose data.");
                return result;
            }
            else
            {
                story.formatVersion = static_cast<int>(declared);
            }
        }
    }
    else
    {
        ReportProblem(result.problems, Severity::Error, "formatVersion.missing",
                      "The file does not declare a format version.");
    }

    story.id = ReadString(root, "id", "The document", result.problems, {}, true);
    story.title = ReadString(root, "title", "The document", result.problems, {}, false);

    if (const json::Value* metadata = root.Find("metadata"))
    {
        if (!metadata->IsObject())
        {
            ReportProblem(result.problems, Severity::Error, "metadata.type",
                          "Field \"metadata\" must be an object.");
        }
        else
        {
            story.metadata = *metadata;
        }
    }

    // -- nodes ---------------------------------------------------------------

    if (const json::Value* nodes = root.Find("nodes"))
    {
        if (!nodes->IsArray())
        {
            ReportProblem(result.problems, Severity::Error, "nodes.type",
                          "Field \"nodes\" must be an array.");
        }
        else
        {
            int index = 0;
            for (const json::Value& nodeValue : nodes->AsArray())
            {
                if (!nodeValue.IsObject())
                {
                    ReportProblem(result.problems, Severity::Error, "node.type",
                                  "Node " + std::to_string(index) +
                                      " must be an object.");
                }
                else
                {
                    story.nodes.push_back(ReadNode(nodeValue, index, result.problems));
                }
                index += 1;
            }
        }
    }
    else
    {
        ReportProblem(result.problems, Severity::Error, "nodes.missing",
                      "The file has no \"nodes\" array.");
    }

    // -- edges ---------------------------------------------------------------

    if (const json::Value* edges = root.Find("edges"))
    {
        if (!edges->IsArray())
        {
            ReportProblem(result.problems, Severity::Error, "edges.type",
                          "Field \"edges\" must be an array.");
        }
        else
        {
            int index = 0;
            for (const json::Value& edgeValue : edges->AsArray())
            {
                if (!edgeValue.IsObject())
                {
                    ReportProblem(result.problems, Severity::Error, "edge.type",
                                  "Edge " + std::to_string(index) +
                                      " must be an object.");
                }
                else
                {
                    story.edges.push_back(ReadEdge(edgeValue, index, result.problems));
                }
                index += 1;
            }
        }
    }

    CollectExtra(root, StoryKeys(), story.extra);

    // -- cross-references ----------------------------------------------------
    //
    // Run only when reading produced nothing, so the user is not shown
    // "edge refers to a missing node" for every edge of a file whose nodes
    // failed to parse. One layer of problems at a time.

    if (result.problems.empty())
    {
        std::vector<Problem> structural = Validate(story);
        for (Problem& problem : structural)
        {
            result.problems.push_back(std::move(problem));
        }
    }

    return result;
}

// --- Serialize --------------------------------------------------------------

std::string Serialize(const Story& story)
{
    json::Value nodes(json::Array {});
    for (const Node& node : story.nodes)
    {
        nodes.Push(WriteNode(node));
    }

    json::Value edges(json::Array {});
    for (const Edge& edge : story.edges)
    {
        edges.Push(WriteEdge(edge));
    }

    json::Value root(json::Object {});
    root.Set("formatVersion", json::Value(static_cast<std::int64_t>(story.formatVersion)));
    root.Set("id", json::Value(story.id));
    root.Set("title", json::Value(story.title));
    root.Set("metadata", story.metadata);
    root.Set("nodes", std::move(nodes));
    root.Set("edges", std::move(edges));
    ApplyExtra(story.extra, StoryKeys(), root);

    // Two spaces. Wide enough to read, narrow enough that a deep document
    // does not march off the right edge of an editor.
    return root.Serialize(2);
}

// --- files ------------------------------------------------------------------

LoadResult LoadFromFile(const std::string& path)
{
    LoadResult result;

    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        Problem p;
        p.severity = Severity::Error;
        p.code = "file.open";
        p.message = "Cannot open \"" + path + "\".";
        result.problems.push_back(std::move(p));
        return result;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (file.bad())
    {
        Problem p;
        p.severity = Severity::Error;
        p.code = "file.read";
        p.message = "Failed while reading \"" + path + "\".";
        result.problems.push_back(std::move(p));
        return result;
    }

    result = Deserialize(buffer.str());
    return result;
}

std::string SaveToFile(const Story& story, const std::string& path)
{
    const std::string text = Serialize(story);

    // Write beside the target and rename over it.
    //
    // std::filesystem::rename, not std::rename. The C function refuses to
    // overwrite on Windows, so it has to be preceded by a delete — and that
    // delete is a window in which the original is gone and the replacement is
    // not in place yet. If the rename then fails, because a scanner has the
    // temporary open or the directory is read-only, both files are lost and
    // the user has nothing. std::filesystem::rename replaces the destination
    // in one step, which is the property this function's contract claims.
    const std::string temporary = path + ".tmp";

    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            return "Cannot write to \"" + temporary + "\".";
        }
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.flush();
        if (!file)
        {
            file.close();
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return "Failed while writing \"" + temporary + "\".";
        }
    }

    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error)
    {
        // The original is untouched, which is the point. Clean up the
        // temporary so a failed save does not leave litter beside the file,
        // and say what the system said rather than guessing.
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return "Cannot replace \"" + path + "\": " + error.message() + ".";
    }

    return {};
}

} // namespace storynode
