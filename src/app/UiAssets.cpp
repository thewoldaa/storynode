#include "app/UiAssets.h"

#include <windows.h>

#include <fstream>
#include <string>
#include <vector>

#include "ui_manifest.h"

namespace storynode::ui {
namespace {

std::wstring Widen(const std::string& utf8)
{
    if (utf8.empty())
    {
        return {};
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                           static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                        out.data(), needed);
    return out;
}

std::string Narrow(const std::wstring& utf16)
{
    if (utf16.empty())
    {
        return {};
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, utf16.data(),
                                           static_cast<int>(utf16.size()),
                                           nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, utf16.data(), static_cast<int>(utf16.size()),
                        out.data(), needed, nullptr, nullptr);
    return out;
}

/// Read one embedded asset by its path relative to src/ui/assets.
///
/// The name is the path, which is what the generated resource script uses. A
/// mismatch between the two is impossible because both come from the same
/// glob.
bool LoadAsset(const std::wstring& path, std::string& out)
{
    HRSRC resource = FindResourceW(nullptr, path.c_str(), RT_RCDATA);
    if (!resource)
    {
        return false;
    }

    HGLOBAL block = LoadResource(nullptr, resource);
    if (!block)
    {
        return false;
    }

    const DWORD size = SizeofResource(nullptr, resource);
    const char* data = static_cast<const char*>(LockResource(block));
    if (!data || size == 0)
    {
        return false;
    }

    out.assign(data, size);
    return true;
}

/// Find where to inject, and fail loudly rather than silently doing nothing.
///
/// A page that loads with no stylesheet or no script still opens and still
/// stays running, so a failed injection is invisible without a check. The
/// markers are in the shell page and the build does not produce a shell
/// without them, but a hand-edited ui.html could.
bool InjectBefore(std::string& document, const std::string& marker,
                  const std::string& content)
{
    const std::size_t position = document.find(marker);
    if (position == std::string::npos)
    {
        return false;
    }
    document.insert(position, content);
    return true;
}

/// Escape a closing tag inside an inlined script or stylesheet.
///
/// A literal "</script>" anywhere in the text — in a string, in a comment —
/// ends the element early and the rest of the file becomes markup. Splitting
/// it with a backslash is the standard fix: the JavaScript engine sees the
/// same string, and the HTML parser sees no closing tag.
std::string EscapeClosingTags(const std::string& text, const std::string& tag)
{
    const std::string needle = "</" + tag;
    std::string out;
    out.reserve(text.size());

    std::size_t position = 0;
    while (true)
    {
        const std::size_t found = text.find(needle, position);
        if (found == std::string::npos)
        {
            out.append(text, position, std::string::npos);
            break;
        }
        out.append(text, position, found - position);
        // "<\/script" is the same string to the engine and not a closing tag
        // to the parser.
        out += "<\\/";
        out.append(tag);
        position = found + needle.size();
    }

    return out;
}

std::wstring ErrorPage(const std::string& message)
{
    const std::string html =
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<style>body{background:#16181c;color:#d8dbe0;font:13px/1.6 sans-serif;padding:40px}"
        "h1{font-size:15px;color:#ff6b5e;margin:0 0 12px}"
        "p{color:#8b929e;margin:0 0 8px}code{font-family:Consolas,monospace;color:#ffa23a}</style>"
        "</head><body><h1>The interface could not be assembled</h1><p>" +
        message + "</p></body></html>";
    return Widen(html);
}

/// Write the assembled page to disk when STORYNODE_DUMP_UI names a path.
///
/// The hardest failure in this architecture is a page that loads and renders
/// nothing: the window opens, the process stays running, and there is no error
/// anywhere because the problem is a script that threw during parsing. Being
/// able to look at exactly what the host handed to the browser is the
/// difference between diagnosing that in a minute and guessing for an hour.
///
/// Opt-in through the environment rather than always on, so a normal run does
/// not write files beside the user's work.
void DumpIfRequested(const std::string& document, const char* stage)
{
    wchar_t path[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"STORYNODE_DUMP_UI", path, MAX_PATH))
    {
        return;
    }

    // The stage is appended so that a run which fails partway still leaves
    // something to look at, and the file name says how far it got.
    std::wstring target = path;
    target += L".";
    target += Widen(stage);
    target += L".html";

    std::ofstream file(target, std::ios::binary | std::ios::trunc);
    file.write(document.data(), static_cast<std::streamsize>(document.size()));
}

} // namespace

std::wstring BuildInterfaceDocument()
{
    std::string shell;
    if (!LoadAsset(kShellPath, shell))
    {
        return ErrorPage("The shell page <code>ui.html</code> is missing from the "
                         "executable. The build did not embed it.");
    }

    DumpIfRequested(shell, "1-shell");

    std::string styles;
    for (int i = 0; i < kStylesheetCount; ++i)
    {
        std::string css;
        if (!LoadAsset(kStylesheets[i], css))
        {
            return ErrorPage("The stylesheet <code>" +
                             Narrow(kStylesheets[i]) +
                             "</code> is missing from the executable.");
        }
        styles += "<style>\n";
        styles += EscapeClosingTags(css, "style");
        styles += "\n</style>\n";
    }

    DumpIfRequested(styles, "2-styles");

    std::string scripts;
    for (int i = 0; i < kScriptCount; ++i)
    {
        std::string js;
        if (!LoadAsset(kScripts[i], js))
        {
            return ErrorPage("The script <code>" +
                             Narrow(kScripts[i]) +
                             "</code> is missing from the executable.");
        }
        scripts += "<script>\n";
        scripts += EscapeClosingTags(js, "script");
        scripts += "\n</script>\n";
    }

    DumpIfRequested(scripts, "3-scripts");

    // Stylesheets go in the head, scripts at the end of the body. A script in
    // the head runs before the elements it wires up exist, and the area that
    // registered first would find nothing to attach to.
    if (!InjectBefore(shell, "</head>", styles))
    {
        return ErrorPage("The shell page has no <code>&lt;/head&gt;</code> to "
                         "insert the stylesheets into.");
    }
    if (!InjectBefore(shell, "</body>", scripts))
    {
        return ErrorPage("The shell page has no <code>&lt;/body&gt;</code> to "
                         "insert the scripts into.");
    }

    DumpIfRequested(shell, "4-assembled");

    return Widen(shell);
}

} // namespace storynode::ui
