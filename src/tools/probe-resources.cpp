// A throwaway probe: does FindResourceW find the embedded interface assets?
//
// Built and run from the command line, linked against nothing but the Win32
// libraries, so it cannot be confused by anything else in the application.

#include <windows.h>
#include <cstdio>

int wmain()
{
    const wchar_t* names[] = {
        L"UI.HTML",
        L"ui.html",
        L"CANVAS/CANVAS.JS",
        L"CANVAS\\CANVAS.JS",
        L"STYLES/THEME.CSS",
    };

    for (const wchar_t* name : names)
    {
        HRSRC r = FindResourceW(nullptr, name, RT_RCDATA);
        if (r)
        {
            const DWORD size = SizeofResource(nullptr, r);
            wprintf(L"  FOUND    %-22s %lu bytes\n", name, size);
        }
        else
        {
            wprintf(L"  missing  %-22s (error %lu)\n", name, GetLastError());
        }
    }

    // Also enumerate what is actually there, so a name mismatch is visible
    // rather than inferred.
    wprintf(L"\n  enumerating RT_RCDATA:\n");
    EnumResourceNamesW(nullptr, RT_RCDATA,
        [](HMODULE, LPCWSTR, LPWSTR name, LONG_PTR) -> BOOL {
            wprintf(L"    %s\n", name);
            return TRUE;
        }, 0);

    return 0;
}
