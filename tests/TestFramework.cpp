#include "TestFramework.h"

#include <iostream>

namespace storynode::test {
namespace {

std::string g_currentTest;

/// Failures seen so far in the test that is running, cleared between tests.
std::size_t g_failuresAtTestStart = 0;

} // namespace

std::vector<TestCase>& Registry()
{
    static std::vector<TestCase> registry;
    return registry;
}

std::vector<Failure>& Failures()
{
    static std::vector<Failure> failures;
    return failures;
}

const std::string& CurrentTest()
{
    return g_currentTest;
}

void ReportFailure(const char* file, int line, const std::string& message)
{
    Failure failure;
    failure.testName = g_currentTest;
    failure.file = file ? file : "";
    failure.line = line;
    failure.message = message;
    Failures().push_back(std::move(failure));
}

int RunAll()
{
    int failedTests = 0;
    std::vector<std::string> failedNames;

    for (const TestCase& test : Registry())
    {
        g_currentTest = test.name;
        g_failuresAtTestStart = Failures().size();

        try
        {
            test.body();
        }
        catch (const std::exception& e)
        {
            // An exception escaping a test is a failure of that test, not a
            // reason to abandon the run: the remaining tests still have
            // something to say.
            ReportFailure("<test body>", 0,
                          std::string("threw an exception: ") + e.what());
        }
        catch (...)
        {
            ReportFailure("<test body>", 0, "threw an unknown exception");
        }

        if (Failures().size() > g_failuresAtTestStart)
        {
            failedTests += 1;
            failedNames.push_back(test.name);
        }
    }

    const std::size_t total = Registry().size();

    if (!Failures().empty())
    {
        std::cout << "\nFailures:\n";
        for (const Failure& failure : Failures())
        {
            std::cout << "\n  " << failure.testName << "\n";
            if (failure.line > 0)
            {
                std::cout << "    " << failure.file << ":" << failure.line << "\n";
            }
            std::cout << "    " << failure.message << "\n";
        }
        std::cout << "\n";
    }

    std::cout << (total - static_cast<std::size_t>(failedTests)) << "/" << total
              << " tests passed";

    if (failedTests > 0)
    {
        std::cout << " (" << failedTests << " failed)";
    }
    std::cout << "\n";

    return failedTests;
}

} // namespace storynode::test

int main()
{
    return storynode::test::RunAll() == 0 ? 0 : 1;
}
