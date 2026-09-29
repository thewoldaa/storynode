#include "app/Verify.h"

// windows.h first: shellapi.h uses types (HDROP) that it does not define
// itself, so including it first fails with a wall of "missing type specifier"
// errors pointing at the Windows SDK rather than at this file.
#include <windows.h>
#include <shellapi.h>

namespace storynode {

CommandLine ParseCommandLine()
{
    CommandLine result;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
    {
        return result;
    }

    for (int i = 1; i < argc; ++i)
    {
        const std::wstring argument = argv[i];

        if (argument == L"--verify")
        {
            result.verify = true;
        }
        else if (argument == L"--report" && i + 1 < argc)
        {
            result.reportPath = argv[i + 1];
            i += 1;
        }
        else if (argument == L"--size" && i + 1 < argc)
        {
            const std::wstring value = argv[i + 1];
            const std::size_t x = value.find(L'x');
            if (x != std::wstring::npos)
            {
                try
                {
                    const int w = std::stoi(value.substr(0, x));
                    const int h = std::stoi(value.substr(x + 1));
                    // Clamp to something a window can actually be. A zero or
                    // negative size would make WebView2 skip layout entirely
                    // and the report would be all zeroes, which would look
                    // like a layout failure rather than a bad argument.
                    if (w >= 200 && h >= 200)
                    {
                        result.width = w;
                        result.height = h;
                    }
                }
                catch (...)
                {
                    // A malformed size falls back to the default rather than
                    // failing the run; the report says which size was used.
                }
            }
            i += 1;
        }
    }

    LocalFree(argv);
    return result;
}

} // namespace storynode
