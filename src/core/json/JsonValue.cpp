#include "core/json/JsonValue.h"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace storynode::json {
namespace {

const Value& NullValue()
{
    static const Value null_value;
    return null_value;
}

const Array& EmptyArray()
{
    static const Array empty;
    return empty;
}

const Object& EmptyObject()
{
    static const Object empty;
    return empty;
}

// --- escaping ---------------------------------------------------------------

void AppendEscaped(std::string& out, const std::string& s)
{
    out.push_back('"');
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20)
            {
                // Control characters must be escaped; JSON has no raw form
                // for them and a literal newline inside a string is invalid.
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            }
            else
            {
                // Bytes above 0x7F pass through unescaped. The document is
                // UTF-8 and JSON strings are UTF-8, so re-encoding them would
                // be a bug, not a safety measure.
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    out.push_back('"');
}

// --- number formatting ------------------------------------------------------

/// Format a double so that it round-trips exactly.
///
/// 17 significant digits is the shortest precision that guarantees every
/// double survives a write-read cycle. Using fewer produces a file that
/// changes value when saved, which is the kind of bug that surfaces months
/// later as a node that drifted a pixel.
std::string FormatDouble(double d)
{
    if (std::isnan(d) || std::isinf(d))
    {
        // JSON has no representation for these. Writing 0 is wrong but
        // writing invalid JSON is worse; a validator rejects them upstream.
        return "0";
    }

    // Integers written as doubles keep a trailing .0 so the type survives
    // the round trip and a reader can tell 1.0 from 1.
    if (d == static_cast<double>(static_cast<std::int64_t>(d)) &&
        std::abs(d) < 9.0e15)
    {
        std::ostringstream os;
        os.precision(1);
        os << std::fixed << d;
        return os.str();
    }

    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.17g", d);
    return buf;
}

// --- parser -----------------------------------------------------------------

class Parser
{
public:
    explicit Parser(const std::string& text) : _text(text) {}

    bool Run(Value& out)
    {
        SkipWhitespace();
        if (!ParseValue(out, 0))
        {
            return false;
        }
        SkipWhitespace();
        if (_pos != _text.size())
        {
            return Fail("unexpected trailing content");
        }
        return true;
    }

    const std::string& Error() const { return _error; }
    Location Where() const { return _location; }

private:
    // Depth limit. A file that nests thousands of levels deep is either
    // corrupt or hostile, and without a limit it exhausts the stack. 200 is
    // far past any real document.
    static constexpr int kMaxDepth = 200;

    bool Fail(const std::string& message)
    {
        if (_error.empty())
        {
            _error = message;
            _location = CurrentLocation();
        }
        return false;
    }

    Location CurrentLocation() const
    {
        Location loc;
        loc.line = 1;
        loc.column = 1;
        for (std::size_t i = 0; i < _pos && i < _text.size(); ++i)
        {
            if (_text[i] == '\n')
            {
                loc.line += 1;
                loc.column = 1;
            }
            else
            {
                loc.column += 1;
            }
        }
        return loc;
    }

    bool AtEnd() const { return _pos >= _text.size(); }
    char Peek() const { return _pos < _text.size() ? _text[_pos] : '\0'; }

    void SkipWhitespace()
    {
        while (!AtEnd())
        {
            const char c = _text[_pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            {
                _pos += 1;
            }
            else
            {
                break;
            }
        }
    }

    bool Expect(char c)
    {
        if (Peek() != c)
        {
            std::string message = "expected '";
            message.push_back(c);
            message.push_back('\'');
            return Fail(message);
        }
        _pos += 1;
        return true;
    }

    bool ParseValue(Value& out, int depth)
    {
        if (depth > kMaxDepth)
        {
            return Fail("nesting is too deep");
        }
        if (AtEnd())
        {
            return Fail("unexpected end of input");
        }

        switch (Peek())
        {
        case '{': return ParseObject(out, depth);
        case '[': return ParseArray(out, depth);
        case '"': {
            std::string s;
            if (!ParseString(s)) { return false; }
            out = Value(std::move(s));
            return true;
        }
        case 't': return ParseLiteral("true", Value(true), out);
        case 'f': return ParseLiteral("false", Value(false), out);
        case 'n': return ParseLiteral("null", Value(nullptr), out);
        default:
            if (Peek() == '-' || (Peek() >= '0' && Peek() <= '9'))
            {
                return ParseNumber(out);
            }
            return Fail("unexpected character");
        }
    }

    bool ParseLiteral(const char* literal, Value v, Value& out)
    {
        const std::size_t len = std::char_traits<char>::length(literal);
        if (_text.compare(_pos, len, literal) != 0)
        {
            return Fail("invalid literal");
        }
        _pos += len;
        out = std::move(v);
        return true;
    }

    bool ParseObject(Value& out, int depth)
    {
        if (!Expect('{')) { return false; }

        Object obj;
        SkipWhitespace();
        if (Peek() == '}')
        {
            _pos += 1;
            out = Value(std::move(obj));
            return true;
        }

        for (;;)
        {
            SkipWhitespace();
            std::string key;
            if (!ParseString(key)) { return false; }

            SkipWhitespace();
            if (!Expect(':')) { return false; }

            SkipWhitespace();
            Value member;
            if (!ParseValue(member, depth + 1)) { return false; }

            // A duplicate key is rejected rather than silently taking the
            // last one. Two entries with the same name mean the file says two
            // different things about one property, and picking either is a
            // guess. A validator cannot report what the parser threw away.
            for (const auto& existing : obj)
            {
                if (existing.first == key)
                {
                    return Fail("duplicate key \"" + key + "\"");
                }
            }

            obj.emplace_back(std::move(key), std::move(member));

            SkipWhitespace();
            if (Peek() == ',')
            {
                _pos += 1;
                continue;
            }
            if (Peek() == '}')
            {
                _pos += 1;
                out = Value(std::move(obj));
                return true;
            }
            return Fail("expected ',' or '}'");
        }
    }

    bool ParseArray(Value& out, int depth)
    {
        if (!Expect('[')) { return false; }

        Array arr;
        SkipWhitespace();
        if (Peek() == ']')
        {
            _pos += 1;
            out = Value(std::move(arr));
            return true;
        }

        for (;;)
        {
            SkipWhitespace();
            Value element;
            if (!ParseValue(element, depth + 1)) { return false; }
            arr.push_back(std::move(element));

            SkipWhitespace();
            if (Peek() == ',')
            {
                _pos += 1;
                continue;
            }
            if (Peek() == ']')
            {
                _pos += 1;
                out = Value(std::move(arr));
                return true;
            }
            return Fail("expected ',' or ']'");
        }
    }

    bool ParseHex4(unsigned& value)
    {
        if (_pos + 4 > _text.size())
        {
            return Fail("truncated \\u escape");
        }
        value = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = _text[_pos + static_cast<std::size_t>(i)];
            unsigned digit = 0;
            if (c >= '0' && c <= '9')      { digit = static_cast<unsigned>(c - '0'); }
            else if (c >= 'a' && c <= 'f') { digit = static_cast<unsigned>(c - 'a' + 10); }
            else if (c >= 'A' && c <= 'F') { digit = static_cast<unsigned>(c - 'A' + 10); }
            else { return Fail("invalid \\u escape"); }
            value = (value << 4) | digit;
        }
        _pos += 4;
        return true;
    }

    static void AppendUtf8(std::string& out, unsigned codepoint)
    {
        if (codepoint < 0x80)
        {
            out.push_back(static_cast<char>(codepoint));
        }
        else if (codepoint < 0x800)
        {
            out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
        else if (codepoint < 0x10000)
        {
            out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }

    bool ParseString(std::string& out)
    {
        if (!Expect('"')) { return false; }
        out.clear();

        while (true)
        {
            if (AtEnd())
            {
                return Fail("unterminated string");
            }

            const unsigned char c = static_cast<unsigned char>(_text[_pos]);
            if (c == '"')
            {
                _pos += 1;
                return true;
            }
            if (c < 0x20)
            {
                // A raw control character in a string is invalid JSON, and
                // accepting it would let a file with an embedded newline
                // round-trip into something that no longer parses.
                return Fail("raw control character in string");
            }
            if (c != '\\')
            {
                out.push_back(static_cast<char>(c));
                _pos += 1;
                continue;
            }

            _pos += 1;
            if (AtEnd())
            {
                return Fail("unterminated escape");
            }

            const char e = _text[_pos];
            _pos += 1;
            switch (e)
            {
            case '"':  out.push_back('"');  break;
            case '\\': out.push_back('\\'); break;
            case '/':  out.push_back('/');  break;
            case 'b':  out.push_back('\b'); break;
            case 'f':  out.push_back('\f'); break;
            case 'n':  out.push_back('\n'); break;
            case 'r':  out.push_back('\r'); break;
            case 't':  out.push_back('\t'); break;
            case 'u': {
                unsigned high = 0;
                if (!ParseHex4(high)) { return false; }

                // A surrogate pair encodes one code point above the BMP.
                // Handling it matters for any document containing an emoji or
                // a rare CJK character, which a story file plausibly does.
                if (high >= 0xD800 && high <= 0xDBFF)
                {
                    if (_pos + 1 < _text.size() && _text[_pos] == '\\' && _text[_pos + 1] == 'u')
                    {
                        _pos += 2;
                        unsigned low = 0;
                        if (!ParseHex4(low)) { return false; }
                        if (low < 0xDC00 || low > 0xDFFF)
                        {
                            return Fail("invalid low surrogate");
                        }
                        const unsigned combined =
                            0x10000 + ((high - 0xD800) << 10) + (low - 0xDC00);
                        AppendUtf8(out, combined);
                    }
                    else
                    {
                        return Fail("unpaired high surrogate");
                    }
                }
                else if (high >= 0xDC00 && high <= 0xDFFF)
                {
                    return Fail("unpaired low surrogate");
                }
                else
                {
                    AppendUtf8(out, high);
                }
                break;
            }
            default:
                return Fail("invalid escape");
            }
        }
    }

    bool ParseNumber(Value& out)
    {
        const std::size_t start = _pos;
        bool integral = true;

        if (Peek() == '-')
        {
            _pos += 1;
        }

        // Leading zeros are not allowed by JSON, and accepting them would
        // make "007" parse as 7 and then write back as "7", silently changing
        // a file that a user may have meant as a string.
        if (Peek() == '0')
        {
            _pos += 1;
        }
        else if (Peek() >= '1' && Peek() <= '9')
        {
            while (Peek() >= '0' && Peek() <= '9') { _pos += 1; }
        }
        else
        {
            return Fail("invalid number");
        }

        if (Peek() == '.')
        {
            integral = false;
            _pos += 1;
            if (!(Peek() >= '0' && Peek() <= '9'))
            {
                return Fail("expected digit after decimal point");
            }
            while (Peek() >= '0' && Peek() <= '9') { _pos += 1; }
        }

        if (Peek() == 'e' || Peek() == 'E')
        {
            integral = false;
            _pos += 1;
            if (Peek() == '+' || Peek() == '-') { _pos += 1; }
            if (!(Peek() >= '0' && Peek() <= '9'))
            {
                return Fail("expected digit in exponent");
            }
            while (Peek() >= '0' && Peek() <= '9') { _pos += 1; }
        }

        const std::string text = _text.substr(start, _pos - start);

        if (integral)
        {
            try
            {
                std::size_t consumed = 0;
                const std::int64_t i = std::stoll(text, &consumed);
                if (consumed == text.size())
                {
                    out = Value(i);
                    return true;
                }
            }
            catch (...)
            {
                // Out of range for int64: fall through and keep it as a
                // double, which is lossy but preserves the magnitude rather
                // than failing a document over an implausibly large number.
            }
        }

        try
        {
            out = Value(std::stod(text));
        }
        catch (...)
        {
            return Fail("number is out of range");
        }
        return true;
    }

    const std::string& _text;
    std::size_t _pos = 0;
    std::string _error;
    Location _location;
};

// --- serialisation ----------------------------------------------------------

void WriteIndent(std::string& out, int indent, int level)
{
    if (indent <= 0)
    {
        return;
    }
    out.push_back('\n');
    out.append(static_cast<std::size_t>(indent) * static_cast<std::size_t>(level), ' ');
}

void WriteValue(std::string& out, const Value& v, int indent, int level)
{
    switch (v.GetType())
    {
    case Value::Type::Null:
        out += "null";
        break;

    case Value::Type::Bool:
        out += v.AsBool() ? "true" : "false";
        break;

    case Value::Type::Number:
        out += v.IsInteger() ? std::to_string(v.AsInt()) : FormatDouble(v.AsDouble());
        break;

    case Value::Type::String:
        AppendEscaped(out, v.AsString());
        break;

    case Value::Type::Array: {
        const Array& arr = v.AsArray();
        if (arr.empty())
        {
            out += "[]";
            break;
        }
        out.push_back('[');
        for (std::size_t i = 0; i < arr.size(); ++i)
        {
            if (i > 0) { out.push_back(','); }
            WriteIndent(out, indent, level + 1);
            WriteValue(out, arr[i], indent, level + 1);
        }
        WriteIndent(out, indent, level);
        out.push_back(']');
        break;
    }

    case Value::Type::Object: {
        const Object& obj = v.AsObject();
        if (obj.empty())
        {
            out += "{}";
            break;
        }
        out.push_back('{');
        for (std::size_t i = 0; i < obj.size(); ++i)
        {
            if (i > 0) { out.push_back(','); }
            WriteIndent(out, indent, level + 1);
            AppendEscaped(out, obj[i].first);
            out.push_back(':');
            if (indent > 0) { out.push_back(' '); }
            WriteValue(out, obj[i].second, indent, level + 1);
        }
        WriteIndent(out, indent, level);
        out.push_back('}');
        break;
    }
    }
}

} // namespace

// --- Location ---------------------------------------------------------------

std::string Location::ToString() const
{
    if (line <= 0)
    {
        return "unknown position";
    }
    return "line " + std::to_string(line) + " column " + std::to_string(column);
}

std::string ParseResult::Message() const
{
    if (ok)
    {
        return {};
    }
    return error + " (" + location.ToString() + ")";
}

// --- Value ------------------------------------------------------------------

Value::Value() = default;
Value::Value(std::nullptr_t) : _type(Type::Null) {}
Value::Value(bool b) : _type(Type::Bool), _bool(b) {}

Value::Value(int i)
    : _type(Type::Number), _integral(true), _integer(static_cast<std::int64_t>(i))
{
    _number = static_cast<double>(i);
}

Value::Value(std::int64_t i)
    : _type(Type::Number), _integral(true), _integer(i)
{
    _number = static_cast<double>(i);
}

Value::Value(double d) : _type(Type::Number), _integral(false), _number(d) {}

Value::Value(const char* s) : _type(Type::String), _string(s ? s : "") {}

Value::Value(std::string s) : _type(Type::String), _string(std::move(s)) {}

Value::Value(Array a) : _type(Type::Array), _array(std::move(a)) {}

Value::Value(Object o) : _type(Type::Object), _object(std::move(o)) {}

bool Value::IsInteger() const
{
    return _type == Type::Number && _integral;
}

bool Value::AsBool(bool fallback) const
{
    return _type == Type::Bool ? _bool : fallback;
}

std::int64_t Value::AsInt(std::int64_t fallback) const
{
    if (_type != Type::Number)
    {
        return fallback;
    }
    if (_integral)
    {
        return _integer;
    }
    // A number written as 3.0 read as an int is 3. Truncation toward zero is
    // what a reader expects here, and a validator rejects genuinely
    // fractional values where an integer is required.
    return static_cast<std::int64_t>(_number);
}

double Value::AsDouble(double fallback) const
{
    if (_type != Type::Number)
    {
        return fallback;
    }
    return _integral ? static_cast<double>(_integer) : _number;
}

const std::string& Value::AsString() const
{
    static const std::string empty;
    return _type == Type::String ? _string : empty;
}

std::string Value::AsString(const std::string& fallback) const
{
    return _type == Type::String ? _string : fallback;
}

const Array& Value::AsArray() const
{
    return _type == Type::Array ? _array : EmptyArray();
}

Array& Value::AsArray()
{
    if (_type != Type::Array)
    {
        // Promote rather than throw. A validator reports the type mismatch;
        // a builder that assigns an array should not have to check first.
        *this = Value(Array{});
    }
    return _array;
}

const Object& Value::AsObject() const
{
    return _type == Type::Object ? _object : EmptyObject();
}

Object& Value::AsObject()
{
    if (_type != Type::Object)
    {
        *this = Value(Object{});
    }
    return _object;
}

const Value* Value::Find(const std::string& key) const
{
    if (_type != Type::Object)
    {
        return nullptr;
    }
    for (const auto& member : _object)
    {
        if (member.first == key)
        {
            return &member.second;
        }
    }
    return nullptr;
}

Value* Value::Find(const std::string& key)
{
    if (_type != Type::Object)
    {
        return nullptr;
    }
    for (auto& member : _object)
    {
        if (member.first == key)
        {
            return &member.second;
        }
    }
    return nullptr;
}

bool Value::Has(const std::string& key) const
{
    return Find(key) != nullptr;
}

const Value& Value::operator[](const std::string& key) const
{
    const Value* found = Find(key);
    return found ? *found : NullValue();
}

void Value::Set(std::string key, Value v)
{
    if (_type != Type::Object)
    {
        *this = Value(Object{});
    }
    for (auto& member : _object)
    {
        if (member.first == key)
        {
            // An existing key keeps its position so that editing a value does
            // not reorder the file.
            member.second = std::move(v);
            return;
        }
    }
    _object.emplace_back(std::move(key), std::move(v));
}

bool Value::Remove(const std::string& key)
{
    if (_type != Type::Object)
    {
        return false;
    }
    for (auto it = _object.begin(); it != _object.end(); ++it)
    {
        if (it->first == key)
        {
            _object.erase(it);
            return true;
        }
    }
    return false;
}

void Value::Push(Value v)
{
    if (_type != Type::Array)
    {
        *this = Value(Array{});
    }
    _array.push_back(std::move(v));
}

std::size_t Value::Size() const
{
    switch (_type)
    {
    case Type::Array:  return _array.size();
    case Type::Object: return _object.size();
    default:           return 0;
    }
}

std::string Value::Serialize(int indent) const
{
    std::string out;
    WriteValue(out, *this, indent, 0);
    if (indent > 0)
    {
        out.push_back('\n');
    }
    return out;
}

// --- Parse ------------------------------------------------------------------

ParseResult Parse(const std::string& text)
{
    ParseResult result;
    Parser parser(text);

    if (parser.Run(result.value))
    {
        result.ok = true;
        return result;
    }

    result.ok = false;
    result.error = parser.Error();
    result.location = parser.Where();
    result.value = Value();
    return result;
}

} // namespace storynode::json
