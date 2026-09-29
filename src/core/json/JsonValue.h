// ---------------------------------------------------------------------------
// A JSON value tree with parse and serialize.
//
// Written rather than pulled in as a dependency, for three reasons:
//
//   1. Object key order is preserved. A .snproj file edited by hand should
//      not be reordered by a save, because that turns a one-line change into
//      an unreadable diff and makes the format hostile to version control.
//      Most JSON libraries sort keys or use a hash map.
//
//   2. Unknown keys survive a round trip. A file written by a newer version
//      of the editor, or by a tool that stores extra metadata, must not lose
//      that data when this version saves it. That requires a tree that keeps
//      everything, not a struct that only keeps what it knows about.
//
//   3. Parse errors carry a line and column. A schema validator that says
//      "expected ',' at line 42 column 7" is usable; one that says "parse
//      failed" is not.
//
// The tree is deliberately not optimised for size or speed. A story document
// is a few hundred kilobytes at most and is parsed on open, not per frame.
// ---------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace storynode::json {

class Value;

/// A JSON array. Order is the order in the file.
using Array = std::vector<Value>;

/// A JSON object, as an ordered list of key-value pairs.
///
/// A vector rather than a map so that serialisation preserves the order the
/// keys were read in, and so that two documents with the same keys in a
/// different order compare unequal — which is what a round-trip test wants to
/// check.
using Object = std::vector<std::pair<std::string, Value>>;

/// Where a parse or validation error was found.
struct Location
{
    int line = 0;   ///< 1-based. 0 when the position is not known.
    int column = 0; ///< 1-based. 0 when the position is not known.

    /// Human-readable form, for error messages: "line 12 column 4".
    /// Returns "unknown position" when line is 0.
    std::string ToString() const;
};

/// A JSON value: null, bool, number, string, array or object.
///
/// Numbers keep the distinction between integral and floating point as it
/// appeared in the source text, so that `1` does not come back as `1.0` and
/// `1.0` does not come back as `1`. That matters for round-trip fidelity in a
/// format people read and edit by hand.
class Value
{
public:
    enum class Type
    {
        Null = 0,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    Value();                          ///< Null.
    Value(std::nullptr_t);
    Value(bool b);
    Value(int i);
    Value(std::int64_t i);
    Value(double d);
    Value(const char* s);
    Value(std::string s);
    Value(Array a);
    Value(Object o);

    Type GetType() const { return _type; }

    bool IsNull() const { return _type == Type::Null; }
    bool IsBool() const { return _type == Type::Bool; }
    bool IsNumber() const { return _type == Type::Number; }
    bool IsString() const { return _type == Type::String; }
    bool IsArray() const { return _type == Type::Array; }
    bool IsObject() const { return _type == Type::Object; }

    /// True when the number was written without a decimal point or exponent.
    bool IsInteger() const;

    // -- readers ------------------------------------------------------------
    //
    // Each returns `fallback` when the value is of a different type. A
    // validator is the place that reports a type mismatch; these are for
    // reading a document that has already been validated.

    bool AsBool(bool fallback = false) const;
    std::int64_t AsInt(std::int64_t fallback = 0) const;
    double AsDouble(double fallback = 0.0) const;
    const std::string& AsString() const;
    std::string AsString(const std::string& fallback) const;

    /// The array, or an empty one. Never throws.
    const Array& AsArray() const;
    Array& AsArray();

    /// The object, or an empty one. Never throws.
    const Object& AsObject() const;
    Object& AsObject();

    // -- object access ------------------------------------------------------
    //
    // Lookup is linear. Objects here have a handful of keys, and a hash map
    // would cost more to build than the scan it replaces.

    /// The value for `key`, or nullptr when absent. The pointer is valid
    /// until this value is modified.
    const Value* Find(const std::string& key) const;
    Value* Find(const std::string& key);

    /// True when the object has `key`.
    bool Has(const std::string& key) const;

    /// The value for `key`, or a shared null when absent.
    const Value& operator[](const std::string& key) const;

    /// Insert or overwrite. Insertion order is preserved for new keys; an
    /// existing key keeps its position and takes the new value.
    void Set(std::string key, Value v);

    /// Remove `key` if present. Returns true when something was removed.
    bool Remove(const std::string& key);

    /// Append to an array. Has no effect when this is not an array.
    void Push(Value v);

    /// Number of array elements, or object members, or 0.
    std::size_t Size() const;

    // -- writing ------------------------------------------------------------

    /// Serialise. `indent` of 0 writes compact output on one line; a positive
    /// value writes that many spaces per level. Files written to disk use an
    /// indent so they diff readably.
    std::string Serialize(int indent = 0) const;

private:
    Type _type = Type::Null;
    bool _bool = false;
    bool _integral = false;
    double _number = 0.0;
    std::int64_t _integer = 0;
    std::string _string;
    Array _array;
    Object _object;
};

/// The outcome of a parse. Exactly one of `value` and `error` is meaningful,
/// and `ok` says which.
///
/// Declared after Value, not before: it holds a Value by value, and a member
/// of an incomplete type is an error rather than a forward declaration the
/// compiler can work around.
struct ParseResult
{
    bool ok = false;
    Value value;
    std::string error;
    Location location;

    /// Full message including the position, ready to show a user.
    std::string Message() const;
};

/// Parse `text` as JSON. The result carries either the value or the error.
ParseResult Parse(const std::string& text);

} // namespace storynode::json
