// ---------------------------------------------------------------------------
// Tests for the JSON value tree.
//
// The parser and serialiser are the foundation everything else sits on: the
// file format, the bridge protocol, and the schema validator all go through
// them. A bug here surfaces as a corrupted document, so the coverage is
// deliberately thorough on the cases that are easy to get subtly wrong —
// number round-tripping, escaping, and error positions.
// ---------------------------------------------------------------------------

#include "TestFramework.h"

#include "core/json/JsonValue.h"

using namespace storynode;
using namespace storynode::json;

// --- parsing basics ---------------------------------------------------------

TEST(ParsesNull)
{
    const ParseResult r = Parse("null");
    CHECK(r.ok);
    CHECK(r.value.IsNull());
}

TEST(ParsesBooleans)
{
    const ParseResult t = Parse("true");
    CHECK(t.ok);
    CHECK(t.value.IsBool());
    CHECK(t.value.AsBool());

    const ParseResult f = Parse("false");
    CHECK(f.ok);
    CHECK(f.value.IsBool());
    CHECK_FALSE(f.value.AsBool());
}

TEST(ParsesStrings)
{
    const ParseResult r = Parse("\"hello\"");
    CHECK(r.ok);
    CHECK(r.value.IsString());
    CHECK_EQ(r.value.AsString(), std::string("hello"));
}

TEST(ParsesEmptyContainers)
{
    const ParseResult a = Parse("[]");
    CHECK(a.ok);
    CHECK(a.value.IsArray());
    CHECK_EQ(a.value.Size(), std::size_t(0));

    const ParseResult o = Parse("{}");
    CHECK(o.ok);
    CHECK(o.value.IsObject());
    CHECK_EQ(o.value.Size(), std::size_t(0));
}

TEST(ParsesNestedStructure)
{
    const ParseResult r = Parse(R"({"a":[1,2,{"b":true}],"c":{"d":null}})");
    CHECK(r.ok);
    CHECK(r.value.IsObject());
    CHECK(r.value.Has("a"));
    CHECK(r.value.Has("c"));

    const Value& a = r.value["a"];
    CHECK(a.IsArray());
    CHECK_EQ(a.Size(), std::size_t(3));
    CHECK_EQ(a.AsArray()[0].AsInt(), std::int64_t(1));
    CHECK_EQ(a.AsArray()[1].AsInt(), std::int64_t(2));
    CHECK(a.AsArray()[2].IsObject());
    CHECK(a.AsArray()[2]["b"].AsBool());

    CHECK(r.value["c"]["d"].IsNull());
}

TEST(IgnoresWhitespaceBetweenTokens)
{
    const ParseResult r = Parse("  {\n\t\"a\" : 1 ,\r\n \"b\" : 2 }  ");
    CHECK(r.ok);
    CHECK_EQ(r.value["a"].AsInt(), std::int64_t(1));
    CHECK_EQ(r.value["b"].AsInt(), std::int64_t(2));
}

// --- numbers ----------------------------------------------------------------
//
// The format is meant to be read and edited by hand, so a save must not
// change a number's spelling. These tests pin that down.

TEST(IntegerStaysInteger)
{
    const ParseResult r = Parse("42");
    CHECK(r.ok);
    CHECK(r.value.IsInteger());
    CHECK_EQ(r.value.Serialize(), std::string("42"));
}

TEST(DoubleWithZeroFractionKeepsItsPoint)
{
    const ParseResult r = Parse("1.0");
    CHECK(r.ok);
    CHECK_FALSE(r.value.IsInteger());
    // Serialising 1.0 as "1" would turn a float into an int on the next load.
    CHECK_EQ(r.value.Serialize(), std::string("1.0"));
}

TEST(NegativeNumbers)
{
    const ParseResult i = Parse("-17");
    CHECK(i.ok);
    CHECK_EQ(i.value.AsInt(), std::int64_t(-17));

    const ParseResult d = Parse("-0.5");
    CHECK(d.ok);
    CHECK_EQ(d.value.AsDouble(), -0.5);
}

TEST(ExponentIsNotInteger)
{
    const ParseResult r = Parse("1e3");
    CHECK(r.ok);
    CHECK_FALSE(r.value.IsInteger());
    CHECK_EQ(r.value.AsDouble(), 1000.0);
}

TEST(DoubleRoundTripsExactly)
{
    // A value that cannot be represented in fewer than 17 significant
    // digits. Writing it with less precision would make the file change
    // every time it is saved.
    const double original = 0.1 + 0.2;
    Value v(original);
    const std::string text = v.Serialize();

    const ParseResult back = Parse(text);
    CHECK(back.ok);
    CHECK_EQ(back.value.AsDouble(), original);
}

TEST(RejectsLeadingZero)
{
    const ParseResult r = Parse("007");
    CHECK_FALSE(r.ok);
}

TEST(RejectsTrailingDecimalPoint)
{
    const ParseResult r = Parse("1.");
    CHECK_FALSE(r.ok);
}

TEST(RejectsBareMinus)
{
    const ParseResult r = Parse("-");
    CHECK_FALSE(r.ok);
}

// --- strings and escaping ---------------------------------------------------
//
// Every case here that involves a \u escape is written with an escaped string
// literal ("\\u0041") rather than a raw one (R"(\u0041)").
//
// This is not style. MSVC translates \uXXXX inside raw string literals as a
// universal-character-name before the parser ever sees the text, so
// R"("\u0041")" reaches the parser as the letter A. A test written that way
// passes while testing nothing at all, which is worse than a failing test.

TEST(UnescapesStandardSequences)
{
    const ParseResult r = Parse(R"("a\"b\\c\/d\be\ff\ng\rh\ti")");
    CHECK(r.ok);
    CHECK_EQ(r.value.AsString(), std::string("a\"b\\c/d\be\ff\ng\rh\ti"));
}

TEST(ParsesUnicodeEscape)
{
    const ParseResult r = Parse("\"\\u0041\\u00e9\\u4e2d\"");
    CHECK(r.ok);
    // A, e-acute in UTF-8, and the CJK character for "middle".
    CHECK_EQ(r.value.AsString(), std::string("A\xC3\xA9\xE4\xB8\xAD"));
}

TEST(ParsesSurrogatePair)
{
    // U+1F600, an emoji, encoded as a surrogate pair. Story text plausibly
    // contains one, and mishandling it corrupts the rest of the string.
    const ParseResult r = Parse("\"\\ud83d\\ude00\"");
    CHECK(r.ok);
    CHECK_EQ(r.value.AsString(), std::string("\xF0\x9F\x98\x80"));
}

TEST(RejectsUnpairedSurrogate)
{
    // A lone surrogate is not a character. Accepting it would produce invalid
    // UTF-8 that corrupts everything after it in the string.
    CHECK_FALSE(Parse("\"\\ud83d\"").ok);
    CHECK_FALSE(Parse("\"\\ude00\"").ok);
}

TEST(RejectsRawControlCharacterInString)
{
    const std::string withNewline = "\"a\nb\"";
    CHECK_FALSE(Parse(withNewline).ok);
}

TEST(EscapesOnWrite)
{
    Value v(std::string("line\nbreak\ttab \"quote\" back\\slash"));
    const std::string text = v.Serialize();

    // The output must contain no raw control characters, or it would not
    // parse back.
    for (unsigned char c : text)
    {
        CHECK(c >= 0x20);
    }

    const ParseResult back = Parse(text);
    CHECK(back.ok);
    CHECK_EQ(back.value.AsString(), v.AsString());
}

TEST(PassesThroughUtf8Unescaped)
{
    // UTF-8 bytes above 0x7F must not be re-encoded; doing so would corrupt
    // any non-ASCII text.
    Value v(std::string("caf\xC3\xA9 \xE4\xB8\xAD\xE6\x96\x87"));
    const ParseResult back = Parse(v.Serialize());
    CHECK(back.ok);
    CHECK_EQ(back.value.AsString(), v.AsString());
}

// --- objects ----------------------------------------------------------------

TEST(PreservesKeyOrder)
{
    const ParseResult r = Parse(R"({"zebra":1,"apple":2,"mango":3})");
    CHECK(r.ok);

    const Object& obj = r.value.AsObject();
    CHECK_EQ(obj.size(), std::size_t(3));
    CHECK_EQ(obj[0].first, std::string("zebra"));
    CHECK_EQ(obj[1].first, std::string("apple"));
    CHECK_EQ(obj[2].first, std::string("mango"));

    // And the order survives a write.
    CHECK_EQ(r.value.Serialize(), std::string(R"({"zebra":1,"apple":2,"mango":3})"));
}

TEST(RejectsDuplicateKeys)
{
    // Two entries with the same name mean the file says two different things
    // about one property. Silently keeping the last one would make the
    // document disagree with itself with no way to report it.
    const ParseResult r = Parse(R"({"a":1,"a":2})");
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("duplicate") != std::string::npos);
}

TEST(SetKeepsExistingKeyPosition)
{
    Value obj(Object {});
    obj.Set("first", Value(1));
    obj.Set("second", Value(2));
    obj.Set("first", Value(99)); // overwrite

    const Object& members = obj.AsObject();
    CHECK_EQ(members.size(), std::size_t(2));
    CHECK_EQ(members[0].first, std::string("first"));
    CHECK_EQ(members[0].second.AsInt(), std::int64_t(99));
}

TEST(RemoveReportsWhetherItRemoved)
{
    Value obj(Object {});
    obj.Set("a", Value(1));
    CHECK(obj.Remove("a"));
    CHECK_FALSE(obj.Remove("a"));
    CHECK_FALSE(obj.Has("a"));
}

TEST(MissingKeyReadsAsNullWithoutThrowing)
{
    const ParseResult r = Parse(R"({"a":1})");
    CHECK(r.ok);
    CHECK(r.value["absent"].IsNull());
    CHECK(r.value.Find("absent") == nullptr);
}

TEST(TypeMismatchReadsAsFallback)
{
    const ParseResult r = Parse(R"({"a":"text"})");
    CHECK(r.ok);
    // Asking for a number from a string gives the fallback rather than
    // throwing; a validator is what reports the mismatch.
    CHECK_EQ(r.value["a"].AsInt(7), std::int64_t(7));
    CHECK_EQ(r.value["a"].AsDouble(1.5), 1.5);
}

// --- arrays -----------------------------------------------------------------

TEST(PushBuildsAnArray)
{
    Value arr(Array {});
    arr.Push(Value(1));
    arr.Push(Value(std::string("two")));

    CHECK(arr.IsArray());
    CHECK_EQ(arr.Size(), std::size_t(2));
    CHECK_EQ(arr.AsArray()[0].AsInt(), std::int64_t(1));
    CHECK_EQ(arr.AsArray()[1].AsString(), std::string("two"));
}

TEST(RejectsTrailingComma)
{
    CHECK_FALSE(Parse("[1,2,]").ok);
    CHECK_FALSE(Parse(R"({"a":1,})").ok);
}

// --- errors -----------------------------------------------------------------
//
// A parse error without a position is nearly useless in a file the user can
// edit by hand, so the location is part of the contract.

TEST(ReportsErrorPosition)
{
    const ParseResult r = Parse("{\n  \"a\": 1,\n  \"b\": \n}");
    CHECK_FALSE(r.ok);
    CHECK(r.location.line > 0);
    CHECK(r.location.column > 0);
    // The message must be usable on its own.
    CHECK(r.Message().find("line") != std::string::npos);
}

TEST(RejectsTrailingContent)
{
    const ParseResult r = Parse("{} extra");
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("trailing") != std::string::npos);
}

TEST(RejectsUnterminatedString)
{
    CHECK_FALSE(Parse("\"abc").ok);
}

TEST(RejectsUnterminatedObject)
{
    CHECK_FALSE(Parse(R"({"a":1)").ok);
}

TEST(RejectsEmptyInput)
{
    CHECK_FALSE(Parse("").ok);
}

TEST(RejectsDeepNestingWithoutCrashing)
{
    // A file that nests thousands of levels deep is corrupt or hostile.
    // Without a depth limit this exhausts the stack and takes the process
    // down, which is a crash rather than an error message.
    std::string deep;
    for (int i = 0; i < 5000; ++i)
    {
        deep += "[";
    }
    const ParseResult r = Parse(deep);
    CHECK_FALSE(r.ok);
}

// --- round trip -------------------------------------------------------------

TEST(DocumentRoundTripsExactly)
{
    const std::string original =
        R"({"formatVersion":1,"id":"story-1","title":"A \"test\"","nodes":[],)"
        R"("edges":[],"metadata":{"author":"café"}})";

    const ParseResult first = Parse(original);
    CHECK(first.ok);

    // Parse, write, parse again: the second parse must produce text
    // identical to the first write, or saving a file twice would keep
    // changing it.
    const std::string written = first.value.Serialize();
    const ParseResult second = Parse(written);
    CHECK(second.ok);
    CHECK_EQ(second.value.Serialize(), written);
}

TEST(IndentedOutputIsStillValidJson)
{
    Value obj(Object {});
    obj.Set("a", Value(1));
    obj.Set("nested", Value(Object {}));
    obj.Find("nested")->Set("b", Value(std::string("x")));

    const std::string text = obj.Serialize(2);
    CHECK(text.find('\n') != std::string::npos);

    const ParseResult back = Parse(text);
    CHECK(back.ok);
    CHECK_EQ(back.value["a"].AsInt(), std::int64_t(1));
    CHECK_EQ(back.value["nested"]["b"].AsString(), std::string("x"));
}
