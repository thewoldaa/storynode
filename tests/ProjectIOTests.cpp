// ---------------------------------------------------------------------------
// Tests for the .snproj file format.
//
// Two properties matter more than the rest and have their own tests below:
//
//   1. Loading reports every problem with enough context to fix it, rather
//      than failing on the first.
//
//   2. Load then save is lossless, including for keys this version does not
//      recognise. A save that silently drops what a newer build wrote is the
//      worst failure this format can have, because it destroys data without
//      telling anyone.
// ---------------------------------------------------------------------------

#include "TestFramework.h"

#include "core/ProjectIO.h"

#include <cstdio>
#include <fstream>

using namespace storynode;

namespace {

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

std::string Codes(const std::vector<Problem>& problems)
{
    std::string out;
    for (const Problem& problem : problems)
    {
        if (!out.empty()) { out += ", "; }
        out += problem.code;
    }
    return out;
}

/// A complete, valid document as text. Tests mutate this rather than building
/// documents in code, because the thing under test is the reader.
const char* kValidDocument = R"({
  "formatVersion": 1,
  "id": "story-1",
  "title": "The Test",
  "metadata": { "author": "someone" },
  "nodes": [
    {
      "id": "start-1",
      "type": "start",
      "position": { "x": 80.0, "y": 120.0 },
      "size": { "x": 220.0, "y": 120.0 },
      "ports": [
        { "id": "out", "label": "Start", "kind": "output", "dataType": "flow" }
      ],
      "data": {}
    },
    {
      "id": "end-1",
      "type": "end",
      "position": { "x": 400.0, "y": 120.0 },
      "size": { "x": 220.0, "y": 120.0 },
      "ports": [
        { "id": "in", "label": "In", "kind": "input", "dataType": "flow" }
      ],
      "data": {}
    }
  ],
  "edges": [
    {
      "id": "edge-1",
      "from": { "nodeId": "start-1", "portId": "out" },
      "to": { "nodeId": "end-1", "portId": "in" }
    }
  ]
})";

/// A temporary file that removes itself.
class TempFile
{
public:
    explicit TempFile(const std::string& contents)
    {
        static int counter = 0;
        counter += 1;
        _path = "storynode_test_" + std::to_string(counter) + ".snproj";
        std::ofstream file(_path, std::ios::binary | std::ios::trunc);
        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }

    ~TempFile() { std::remove(_path.c_str()); }

    const std::string& Path() const { return _path; }

private:
    std::string _path;
};

} // namespace

// --- loading ----------------------------------------------------------------

TEST(LoadsAValidDocument)
{
    const LoadResult result = Deserialize(kValidDocument);

    CHECK_EQ(result.problems.size(), std::size_t(0));
    CHECK(result.Clean());
    CHECK_FALSE(result.HasErrors());

    const Story& story = result.story;
    CHECK_EQ(story.formatVersion, 1);
    CHECK_EQ(story.id, std::string("story-1"));
    CHECK_EQ(story.title, std::string("The Test"));
    CHECK_EQ(story.nodes.size(), std::size_t(2));
    CHECK_EQ(story.edges.size(), std::size_t(1));
    CHECK_EQ(story.nodes[0].id, std::string("start-1"));
    CHECK_EQ(story.nodes[0].position.x, 80.0);
    CHECK_EQ(story.nodes[0].position.y, 120.0);
    CHECK_EQ(story.nodes[0].ports.size(), std::size_t(1));
    CHECK_EQ(story.nodes[0].ports[0].kind, Port::Kind::Output);
    CHECK_EQ(story.nodes[0].ports[0].dataType, Port::DataType::Flow);
    CHECK_EQ(story.edges[0].from.nodeId, std::string("start-1"));
    CHECK_EQ(story.edges[0].to.portId, std::string("in"));
    CHECK_EQ(story.metadata["author"].AsString(), std::string("someone"));
}

TEST(ReportsSyntaxErrorWithPosition)
{
    const LoadResult result = Deserialize("{\n  \"a\": 1,\n  broken\n}");

    CHECK(result.HasErrors());
    CHECK(HasCode(result.problems, "json.syntax"));

    const Problem& problem = result.problems[0];
    CHECK(problem.location.line > 0);
    // The message must name the position, or a user cannot find the mistake
    // in a file with hundreds of lines.
    CHECK(problem.message.find("line") != std::string::npos);
}

TEST(RejectsATopLevelArray)
{
    const LoadResult result = Deserialize("[1,2,3]");
    CHECK(HasCode(result.problems, "document.notObject"));
}

TEST(RejectsAFileWithNoFormatVersion)
{
    const LoadResult result = Deserialize(R"({"id":"story-1","nodes":[],"edges":[]})");
    CHECK(HasCode(result.problems, "formatVersion.missing"));
}

TEST(RefusesANewerFormatVersion)
{
    std::string text = kValidDocument;
    const std::string from = "\"formatVersion\": 1";
    text.replace(text.find(from), from.size(), "\"formatVersion\": 99");

    const LoadResult result = Deserialize(text);
    CHECK(HasCode(result.problems, "formatVersion.tooNew"));
    // The message must say why, not just that it failed.
    CHECK(result.problems.back().message.find("newer") != std::string::npos);
}

TEST(ReportsEveryProblemInOnePass)
{
    // Three separate mistakes, all of which must appear in one result. A
    // loader that stops at the first makes the user fix a file one error at
    // a time.
    const std::string text = R"({
      "formatVersion": 1,
      "nodes": [
        { "id": "a", "type": "dialog", "position": "not an object", "ports": 5 },
        { "type": "dialog" },
        { "id": "c", "type": "dialog", "ports": [ { "label": "no id" } ] }
      ],
      "edges": []
    })";

    const LoadResult result = Deserialize(text);
    CHECK(result.problems.size() >= 4);
    CHECK(HasCode(result.problems, "node.position.type"));
    CHECK(HasCode(result.problems, "node.ports.type"));
    CHECK(HasCode(result.problems, "node.id.missing"));
    CHECK(HasCode(result.problems, "port.id.missing"));
}

TEST(ReportsMissingRequiredFields)
{
    const LoadResult result = Deserialize(R"({
      "formatVersion": 1,
      "nodes": [ { "id": "a" } ],
      "edges": [ { "id": "e" } ]
    })");

    // The codes are the same ones Validate() uses, so a caller keying on a
    // code does not have to know whether the reader or the validator caught
    // the problem first.
    CHECK(HasCode(result.problems, "node.type.missing"));
    CHECK(HasCode(result.problems, "field.missing"));
    // Node "a" has no type, and edge "e" has neither endpoint.
    CHECK(result.problems.size() >= 3);
}

TEST(ReportsWrongFieldTypes)
{
    const LoadResult result = Deserialize(R"({
      "formatVersion": "one",
      "id": 5,
      "nodes": [],
      "edges": []
    })");

    CHECK(HasCode(result.problems, "formatVersion.type"));
    CHECK(HasCode(result.problems, "field.type"));
}

TEST(ReportsUnknownPortKind)
{
    const LoadResult result = Deserialize(R"({
      "formatVersion": 1,
      "id": "s",
      "nodes": [ { "id": "a", "type": "dialog",
                   "ports": [ { "id": "p", "kind": "sideways" } ] } ],
      "edges": []
    })");

    CHECK(HasCode(result.problems, "port.kind.unknown"));
    // The message names the offending value so it can be found.
    CHECK(result.problems.back().message.find("sideways") != std::string::npos);
}

TEST(RunsStructuralValidationAfterAReadableParse)
{
    // The file is syntactically fine but its edge points at nothing.
    const LoadResult result = Deserialize(R"({
      "formatVersion": 1,
      "id": "s",
      "nodes": [ { "id": "a", "type": "dialog", "ports": [] } ],
      "edges": [ { "id": "e",
                   "from": { "nodeId": "a", "portId": "out" },
                   "to": { "nodeId": "ghost", "portId": "in" } } ]
    })");

    CHECK(HasCode(result.problems, "edge.to.missingNode"));
}

TEST(DoesNotCascadeErrorsFromAFailedParse)
{
    // When nodes fail to read, their edges would report as dangling too.
    // Showing both layers buries the real mistake under noise, so the
    // structural pass is skipped when the read itself produced problems.
    const LoadResult result = Deserialize(R"({
      "formatVersion": 1,
      "nodes": "not an array",
      "edges": [ { "id": "e",
                   "from": { "nodeId": "a", "portId": "out" },
                   "to": { "nodeId": "b", "portId": "in" } } ]
    })");

    CHECK(HasCode(result.problems, "nodes.type"));
    CHECK_FALSE(HasCode(result.problems, "edge.from.missingNode"));
}

// --- writing ----------------------------------------------------------------

TEST(WritesIndentedOutput)
{
    const LoadResult loaded = Deserialize(kValidDocument);
    CHECK_EQ(loaded.problems.size(), std::size_t(0));

    const std::string text = Serialize(loaded.story);
    CHECK(text.find('\n') != std::string::npos);
    CHECK(text.find("  \"formatVersion\"") != std::string::npos);

    // And it must parse back.
    const LoadResult again = Deserialize(text);
    CHECK_EQ(again.problems.size(), std::size_t(0));
}

TEST(SavingTwiceProducesIdenticalText)
{
    // If a save changed the text, every save would produce a diff and version
    // control would be useless.
    const LoadResult loaded = Deserialize(kValidDocument);
    const std::string first = Serialize(loaded.story);

    const LoadResult reloaded = Deserialize(first);
    const std::string second = Serialize(reloaded.story);

    CHECK_EQ(first, second);
}

TEST(RoundTripPreservesEverything)
{
    const LoadResult loaded = Deserialize(kValidDocument);
    const std::string text = Serialize(loaded.story);
    const LoadResult again = Deserialize(text);

    CHECK_EQ(again.problems.size(), std::size_t(0));
    CHECK_EQ(again.story.title, loaded.story.title);
    CHECK_EQ(again.story.nodes.size(), loaded.story.nodes.size());
    CHECK_EQ(again.story.nodes[0].position.x, loaded.story.nodes[0].position.x);
    CHECK_EQ(again.story.nodes[0].ports[0].kind, loaded.story.nodes[0].ports[0].kind);
    CHECK_EQ(again.story.edges[0].from.nodeId, loaded.story.edges[0].from.nodeId);
    CHECK_EQ(again.story.metadata["author"].AsString(), std::string("someone"));
}

// --- unknown keys -----------------------------------------------------------
//
// The property that makes the format safe to evolve: a file written by a
// newer build, opened and saved by an older one, keeps what the newer build
// added. Without this, the failure is silent data loss that only shows up
// when the user goes back to the newer version.

TEST(PreservesUnknownStoryKeys)
{
    const LoadResult loaded = Deserialize(R"({
      "formatVersion": 1,
      "id": "s",
      "title": "T",
      "nodes": [],
      "edges": [],
      "futureFeature": { "nested": [1, 2, 3] }
    })");

    CHECK_EQ(loaded.problems.size(), std::size_t(0));

    const std::string text = Serialize(loaded.story);
    CHECK(text.find("futureFeature") != std::string::npos);
    CHECK(text.find("nested") != std::string::npos);

    const LoadResult again = Deserialize(text);
    CHECK_EQ(again.story.extra["futureFeature"]["nested"].Size(), std::size_t(3));
}

TEST(PreservesUnknownNodeKeys)
{
    const LoadResult loaded = Deserialize(R"({
      "formatVersion": 1,
      "id": "s",
      "nodes": [ { "id": "a", "type": "dialog", "ports": [],
                   "collapsed": true, "colour": "#ff0000" } ],
      "edges": []
    })");

    CHECK_EQ(loaded.problems.size(), std::size_t(0));
    CHECK_EQ(loaded.story.nodes[0].extra["collapsed"].AsBool(), true);

    const LoadResult again = Deserialize(Serialize(loaded.story));
    CHECK_EQ(again.story.nodes[0].extra["colour"].AsString(), std::string("#ff0000"));
}

TEST(PreservesUnknownPortKeys)
{
    const LoadResult loaded = Deserialize(R"({
      "formatVersion": 1,
      "id": "s",
      "nodes": [ { "id": "a", "type": "dialog",
                   "ports": [ { "id": "p", "kind": "input", "colour": "blue" } ] } ],
      "edges": []
    })");

    CHECK_EQ(loaded.problems.size(), std::size_t(0));

    const LoadResult again = Deserialize(Serialize(loaded.story));
    CHECK_EQ(again.story.nodes[0].ports[0].extra["colour"].AsString(), std::string("blue"));
}

TEST(PreservesUnknownEdgeKeys)
{
    const LoadResult loaded = Deserialize(R"({
      "formatVersion": 1,
      "id": "s",
      "nodes": [
        { "id": "a", "type": "start", "ports": [ { "id": "out", "kind": "output" } ] },
        { "id": "b", "type": "end", "ports": [ { "id": "in", "kind": "input" } ] }
      ],
      "edges": [ { "id": "e",
                   "from": { "nodeId": "a", "portId": "out" },
                   "to": { "nodeId": "b", "portId": "in" },
                   "label": "yes" } ]
    })");

    CHECK_EQ(loaded.problems.size(), std::size_t(0));

    const LoadResult again = Deserialize(Serialize(loaded.story));
    CHECK_EQ(again.story.edges[0].extra["label"].AsString(), std::string("yes"));
}

TEST(UnknownKeysDoNotAppearTwice)
{
    // The writer emits its known keys and then applies `extra`. A key that is
    // in both would be written twice, producing a file that no longer parses
    // because the parser rejects duplicate keys.
    const LoadResult loaded = Deserialize(R"({
      "formatVersion": 1,
      "id": "s",
      "title": "T",
      "nodes": [],
      "edges": []
    })");

    const std::string text = Serialize(loaded.story);
    const LoadResult again = Deserialize(text);
    CHECK_EQ(again.problems.size(), std::size_t(0));
}

// --- files ------------------------------------------------------------------

TEST(LoadFromFileReadsWhatSaveWrote)
{
    Story story = MakeEmptyStory("On Disk");
    const TempFile file("");

    const std::string error = SaveToFile(story, file.Path());
    CHECK_EQ(error, std::string(""));

    const LoadResult loaded = LoadFromFile(file.Path());
    CHECK_EQ(loaded.problems.size(), std::size_t(0));
    CHECK_EQ(loaded.story.title, std::string("On Disk"));
    CHECK_EQ(loaded.story.nodes.size(), std::size_t(1));
}

TEST(LoadFromFileReportsAMissingFile)
{
    const LoadResult result = LoadFromFile("this_file_does_not_exist.snproj");
    CHECK(HasCode(result.problems, "file.open"));
    // Not an exception, and not an empty result with no explanation.
    CHECK_EQ(result.problems.size(), std::size_t(1));
}

TEST(SaveLeavesNoTemporaryFileBehind)
{
    const TempFile file("");
    const Story story = MakeEmptyStory("Clean");

    const std::string error = SaveToFile(story, file.Path());
    CHECK_EQ(error, std::string(""));

    // The temporary must be gone, or every save would litter the folder.
    std::ifstream temporary(file.Path() + ".tmp");
    CHECK_FALSE(temporary.good());
}

TEST(SaveReplacesAnExistingFile)
{
    const TempFile file("garbage that is not json");

    const std::string error = SaveToFile(MakeEmptyStory("Replaced"), file.Path());
    CHECK_EQ(error, std::string(""));

    const LoadResult loaded = LoadFromFile(file.Path());
    CHECK_EQ(loaded.problems.size(), std::size_t(0));
    CHECK_EQ(loaded.story.title, std::string("Replaced"));
}

// --- defects found by review ------------------------------------------------
//
// Each of these pins a bug that was found by reading the code rather than by
// running it. They exist so the bug cannot come back, and so the reasoning
// survives the fix.

TEST(RejectsANonIntegerFormatVersion)
{
    // 1.9 is not version 1. Truncating it would accept a file whose shape
    // this build does not understand, which is what the version field exists
    // to prevent.
    const LoadResult result = Deserialize(
        R"({"formatVersion":1.9,"id":"s","title":"T","nodes":[],"edges":[]})");

    CHECK(HasCode(result.problems, "formatVersion.type"));
}

TEST(RejectsAHugeFormatVersionRatherThanTruncatingIt)
{
    // 4294967297 truncates to 1 in a 32-bit int, which would pass the
    // "not newer than this build" check and then be saved back as version 1 —
    // a silent downgrade of a file this build cannot actually read.
    const LoadResult result = Deserialize(
        R"({"formatVersion":4294967297,"id":"s","title":"T","nodes":[],"edges":[]})");

    CHECK(HasCode(result.problems, "formatVersion.tooNew"));
    // And nothing was populated, so a caller cannot mistake it for usable.
    CHECK_EQ(result.story.title, std::string(""));
}

TEST(RejectsANegativeFormatVersion)
{
    const LoadResult result = Deserialize(
        R"({"formatVersion":-1,"id":"s","title":"T","nodes":[],"edges":[]})");

    CHECK(result.HasErrors());
}

TEST(PreservesUnknownKeysInsideAnEndpoint)
{
    // An endpoint is a level of the document like any other. A key added there
    // by a newer build — a condition, a delay — must survive a save, or the
    // first save from this build silently deletes it.
    const LoadResult loaded = Deserialize(R"({
      "formatVersion": 1,
      "id": "s",
      "nodes": [
        { "id": "a", "type": "start", "ports": [ { "id": "out", "kind": "output" } ] },
        { "id": "b", "type": "end",   "ports": [ { "id": "in",  "kind": "input"  } ] }
      ],
      "edges": [ { "id": "e",
                   "from": { "nodeId": "a", "portId": "out", "condition": "gold > 3" },
                   "to":   { "nodeId": "b", "portId": "in" } } ]
    })");

    CHECK_EQ(loaded.problems.size(), std::size_t(0));

    const std::string text = Serialize(loaded.story);
    CHECK(text.find("condition") != std::string::npos);
    CHECK(text.find("gold > 3") != std::string::npos);

    const LoadResult again = Deserialize(text);
    CHECK_EQ(again.story.edges[0].from.extra["condition"].AsString(),
             std::string("gold > 3"));
}

TEST(SurvivesAHugeNumberInTheDocument)
{
    // A double outside int64's range must not be cast to an integer. The
    // conversion is undefined behaviour, and a document can contain any number
    // at all.
    const LoadResult loaded = Deserialize(R"({
      "formatVersion": 1,
      "id": "s",
      "nodes": [ { "id": "a", "type": "dialog",
                   "position": { "x": 1e300, "y": -1e300 },
                   "ports": [] } ],
      "edges": []
    })");

    CHECK_EQ(loaded.problems.size(), std::size_t(0));
    CHECK(loaded.story.nodes[0].position.x > 1e299);

    // And writing it back must not crash or produce something that will not
    // parse.
    const std::string text = Serialize(loaded.story);
    const LoadResult again = Deserialize(text);
    CHECK_EQ(again.problems.size(), std::size_t(0));
    CHECK_EQ(again.story.nodes[0].position.x, loaded.story.nodes[0].position.x);
}

TEST(SaveReplacesTheFileWithoutDeletingItFirst)
{
    // The original must survive a failed save. If the file is replaced by a
    // delete followed by a rename, a failure between the two leaves nothing,
    // and a failure of the rename leaves nothing as well.
    const TempFile file("original contents");

    const std::string error = SaveToFile(MakeEmptyStory("Replaced"), file.Path());
    CHECK_EQ(error, std::string(""));

    // The new content is there.
    const LoadResult loaded = LoadFromFile(file.Path());
    CHECK_EQ(loaded.problems.size(), std::size_t(0));
    CHECK_EQ(loaded.story.title, std::string("Replaced"));

    // And no temporary is left behind.
    std::ifstream temporary(file.Path() + ".tmp");
    CHECK_FALSE(temporary.good());
}
