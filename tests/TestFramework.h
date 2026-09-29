// ---------------------------------------------------------------------------
// A minimal test framework.
//
// Written rather than pulled in because the whole surface this project needs
// is: register a test, assert, report which assertion failed and where. A
// dependency that has to be fetched would make the build require the network,
// and the tests are the part of the build that most needs to work offline.
//
// Usage:
//
//     TEST(JsonParsesAnEmptyObject)
//     {
//         const auto result = json::Parse("{}");
//         CHECK(result.ok);
//         CHECK(result.value.IsObject());
//     }
//
// Each test is a separate function registered at startup. main() runs them
// all, prints failures with file and line, and returns non-zero if any
// failed, which is what CTest reads.
// ---------------------------------------------------------------------------

#pragma once

#include <functional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace storynode::test {

struct TestCase
{
    std::string name;
    std::function<void()> body;
};

/// The registry. A function-local static rather than a global so that
/// initialisation order between translation units cannot matter.
std::vector<TestCase>& Registry();

/// Records the failure currently being reported, so CHECK can name the test.
struct Failure
{
    std::string testName;
    std::string file;
    int line = 0;
    std::string message;
};

/// Failures collected during the run, reported at the end.
std::vector<Failure>& Failures();

/// Called by CHECK when an assertion does not hold.
void ReportFailure(const char* file, int line, const std::string& message);

/// Turn a value into something printable, for assertion messages.
///
/// The enum overload matters: an `enum class` has no operator<<, so
/// CHECK_EQ on one fails to compile with an error that points at the
/// framework rather than at the assertion. Comparing a Severity or a
/// Port::Kind is exactly what the model tests need to do, so the numeric
/// value is printed instead.
template <typename T>
std::enable_if_t<std::is_enum<T>::value, std::string> Describe(const T& value)
{
    return std::to_string(static_cast<long long>(value));
}

template <typename T>
std::enable_if_t<!std::is_enum<T>::value, std::string> Describe(const T& value)
{
    std::ostringstream os;
    os << value;
    return os.str();
}

inline std::string Describe(bool value)
{
    return value ? "true" : "false";
}

inline std::string Describe(const std::string& value)
{
    return "\"" + value + "\"";
}

inline std::string Describe(const char* value)
{
    return value ? std::string("\"") + value + "\"" : std::string("(null)");
}

/// Runs every registered test. Returns the number that failed.
int RunAll();

/// The name of the test currently running.
const std::string& CurrentTest();

} // namespace storynode::test

// --- registration -----------------------------------------------------------

namespace storynode::test {
namespace detail {

struct Registrar
{
    Registrar(const char* name, std::function<void()> body)
    {
        Registry().push_back(TestCase { name, std::move(body) });
    }
};

} // namespace detail
} // namespace storynode::test

/// Declare a test. The name becomes a C++ identifier, so it must be unique
/// across the whole test binary.
#define TEST(name)                                                             \
    static void name();                                                        \
    static const ::storynode::test::detail::Registrar name##_registrar(#name,  \
                                                                       &name); \
    static void name()

// --- assertions -------------------------------------------------------------

/// Assert that `expr` holds. Reports file and line on failure and keeps
/// going, so one broken assertion does not hide the rest of the test.
#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            ::storynode::test::ReportFailure(__FILE__, __LINE__,               \
                                             "CHECK failed: " #expr);          \
        }                                                                      \
    } while (false)

/// Assert that two values are equal, printing both when they are not.
#define CHECK_EQ(actual, expected)                                             \
    do {                                                                       \
        const auto& check_eq_a = (actual);                                     \
        const auto& check_eq_b = (expected);                                   \
        if (!(check_eq_a == check_eq_b)) {                                     \
            ::storynode::test::ReportFailure(                                  \
                __FILE__, __LINE__,                                            \
                std::string("CHECK_EQ failed: " #actual " == " #expected) +    \
                    "\n      actual:   " + ::storynode::test::Describe(check_eq_a) + \
                    "\n      expected: " + ::storynode::test::Describe(check_eq_b)); \
        }                                                                      \
    } while (false)

/// Assert that `expr` does not hold.
#define CHECK_FALSE(expr)                                                      \
    do {                                                                       \
        if ((expr)) {                                                          \
            ::storynode::test::ReportFailure(__FILE__, __LINE__,               \
                                             "CHECK_FALSE failed: " #expr);    \
        }                                                                      \
    } while (false)
