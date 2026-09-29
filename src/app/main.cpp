// ---------------------------------------------------------------------------
// The editor application.
//
// A Win32 window hosting a WebView2 control that renders the interface. The
// document, the bridge and the file handling live elsewhere; this file is
// window lifecycle, the WebView2 host, and the menu.
//
// The one piece of judgment worth recording: the page is loaded with
// NavigateToString from an embedded resource rather than from a file or a
// local server. No temporary file to write, nothing to go missing at runtime,
// and no HTTP server to open a port.
// ---------------------------------------------------------------------------

#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <wrl.h>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "WebView2.h"

#include "app/Bridge.h"
#include "app/LayoutReport.h"
#include "app/Verify.h"
#include "core/ProjectIO.h"
#include "resource.h"

using namespace Microsoft::WRL;
using storynode::Story;

namespace {

const wchar_t* kWindowClass = L"StoryNodeMainWindow";
const wchar_t* kWindowTitle = L"StoryNode";

HWND g_window = nullptr;
HMENU g_menu = nullptr;

Story g_document;
std::unique_ptr<storynode::Bridge> g_bridge;

ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webview;

/// The file the document was loaded from, or empty for a new document.
std::wstring g_path;

/// True when there are changes that are not on disk.
bool g_dirty = false;

/// True once the page has sent its ready message. Nothing is sent to the page
/// before this, because ExecuteScript during navigation is dropped silently.
bool g_pageReady = false;

/// Verification mode: run, measure the page, report, exit.
storynode::CommandLine g_options;

/// Set once the page has reported its layout in verification mode.
bool g_gotLayoutReport = false;

/// A watchdog for verification mode. A page that fails to load would
/// otherwise leave the process waiting forever, and CI would report a timeout
/// rather than the real problem.
const UINT_PTR kVerifyTimeoutId = 1;

// --- paths ------------------------------------------------------------------

/// Where WebView2 keeps its browser profile.
///
/// Left to itself it puts the profile beside the executable, which drops a
/// large cache folder into whatever directory the program was run from. Under
/// the user's local app data is where it belongs.
std::wstring UserDataFolder()
{
    wchar_t local[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH))
    {
        return {};
    }
    std::wstring path = local;
    path += L"\\StoryNode";
    CreateDirectoryW(path.c_str(), nullptr);   // fails harmlessly if it exists
    return path;
}

// --- string conversion ------------------------------------------------------
//
// The document is UTF-8 throughout, because JSON is UTF-8 and the page is
// UTF-8. Win32 wants UTF-16. These two functions are the only place that
// conversion happens.

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

// --- sending to the page ----------------------------------------------------

void SendToPage(const std::string& json)
{
    if (!g_webview || !g_pageReady)
    {
        return;
    }

    // The message is passed as a JSON string literal so that it arrives at
    // receive() as text rather than as an object the host would have to
    // re-encode. Escaping is done by the JSON serialiser, which already has
    // to be correct for the document format.
    storynode::json::Value argument(json);
    const std::wstring script =
        L"window.storynode.receive(" + Widen(argument.Serialize()) + L");";

    g_webview->ExecuteScript(script.c_str(), nullptr);
}

// --- verification -----------------------------------------------------------

/// Write the layout report and exit.
///
/// The report goes to both the file and stdout. The file is the artifact a
/// human reads afterwards; stdout is what makes a CI failure explain itself,
/// because CTest only shows a test's output and would otherwise report
/// "Failed" with no reason.
void FinishVerification(const std::string& report)
{
    const std::string text = storynode::FormatLayoutReport(report);

    if (!g_options.reportPath.empty())
    {
        std::ofstream file(g_options.reportPath, std::ios::binary | std::ios::trunc);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    // A WIN32 subsystem binary has no console of its own, but when CTest
    // spawns it the child inherits CTest's stdout handle, so this reaches the
    // test log.
    HANDLE stdoutHandle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (stdoutHandle && stdoutHandle != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(stdoutHandle, text.data(), static_cast<DWORD>(text.size()),
                  &written, nullptr);
    }
    OutputDebugStringA(text.c_str());

    PostQuitMessage(storynode::LayoutReportPassed(text) ? 0 : 1);
}

/// Ask the page to measure itself and report back.
void RequestLayoutReport()
{
    if (!g_webview || !g_pageReady)
    {
        return;
    }
    g_webview->ExecuteScript(storynode::LayoutProbeScript(), nullptr);
}

// --- the interface resource -------------------------------------------------

std::wstring LoadInterfaceHtml()
{
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_UI_HTML), RT_RCDATA);
    if (!resource)
    {
        return L"<h3>Interface resource missing</h3>";
    }

    HGLOBAL block = LoadResource(nullptr, resource);
    if (!block)
    {
        return L"<h3>Interface resource could not be loaded</h3>";
    }

    const DWORD size = SizeofResource(nullptr, resource);
    const char* data = static_cast<const char*>(LockResource(block));
    if (!data || size == 0)
    {
        return L"<h3>Interface resource is empty</h3>";
    }

    return Widen(std::string(data, size));
}

// --- document lifecycle -----------------------------------------------------

void UpdateTitle()
{
    std::wstring title = kWindowTitle;
    title += L" - ";
    title += g_path.empty() ? L"Untitled" : PathFindFileNameW(g_path.c_str());
    if (g_dirty)
    {
        title += L" *";
    }
    SetWindowTextW(g_window, title.c_str());
}

void NewDocument()
{
    g_document = storynode::MakeEmptyStory("Untitled");
    g_path.clear();
    g_dirty = false;
    UpdateTitle();
    if (g_bridge)
    {
        g_bridge->SendDocument();
        g_bridge->SendValidation();
    }
}

/// Ask about unsaved changes. Returns false when the user cancels.
///
/// Three-way rather than yes/no: a user who has made a change and does not
/// want to keep it needs a way to say so that is not "cancel and lose the
/// command they just asked for".
bool ConfirmDiscard()
{
    if (!g_dirty)
    {
        return true;
    }

    const int answer = MessageBoxW(g_window,
        L"This document has unsaved changes.\n\nSave before continuing?",
        L"StoryNode", MB_YESNOCANCEL | MB_ICONWARNING);

    if (answer == IDCANCEL)
    {
        return false;
    }
    if (answer == IDNO)
    {
        return true;
    }

    // IDYES: save, then continue only if the save worked.
    return SendMessageW(g_window, WM_COMMAND, ID_FILE_SAVE, 0) != 0;
}

bool OpenDocument()
{
    if (!ConfirmDiscard())
    {
        return false;
    }

    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_window;
    ofn.lpstrFilter = L"StoryNode project\0*.snproj\0All files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"snproj";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&ofn))
    {
        return false;
    }

    const storynode::LoadResult result = storynode::LoadFromFile(Narrow(path));

    if (result.HasErrors())
    {
        std::wstring message = L"This file could not be opened:\n\n";
        for (const storynode::Problem& problem : result.problems)
        {
            if (problem.severity == storynode::Severity::Error)
            {
                message += Widen(problem.message);
                message += L"\n";
            }
        }
        MessageBoxW(g_window, message.c_str(), L"StoryNode", MB_OK | MB_ICONERROR);
        return false;
    }

    g_document = result.story;
    g_path = path;
    g_dirty = false;
    UpdateTitle();

    if (g_bridge)
    {
        g_bridge->SendDocument();
        g_bridge->SendValidation();
    }

    // Warnings do not stop the file from opening, but they are worth saying
    // out loud rather than leaving for the user to find.
    if (!result.problems.empty())
    {
        std::wstring message = L"The file opened with warnings:\n\n";
        for (const storynode::Problem& problem : result.problems)
        {
            message += Widen(problem.message);
            message += L"\n";
        }
        MessageBoxW(g_window, message.c_str(), L"StoryNode", MB_OK | MB_ICONINFORMATION);
    }

    return true;
}

/// Returns true when the document is on disk afterwards.
bool SaveDocument(bool forcePrompt)
{
    std::wstring target = g_path;

    if (target.empty() || forcePrompt)
    {
        wchar_t path[MAX_PATH] = {};
        if (!target.empty())
        {
            wcsncpy_s(path, target.c_str(), _TRUNCATE);
        }

        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = g_window;
        ofn.lpstrFilter = L"StoryNode project\0*.snproj\0All files\0*.*\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrDefExt = L"snproj";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

        if (!GetSaveFileNameW(&ofn))
        {
            return false;
        }
        target = path;
    }

    const std::string error = storynode::SaveToFile(g_document, Narrow(target));
    if (!error.empty())
    {
        MessageBoxW(g_window, Widen("Could not save:\n\n" + error).c_str(),
                    L"StoryNode", MB_OK | MB_ICONERROR);
        return false;
    }

    g_path = target;
    g_dirty = false;
    UpdateTitle();
    return true;
}

void ShowAbout()
{
    MessageBoxW(g_window,
        L"StoryNode\n\n"
        L"A visual editor for branching interactive fiction.\n\n"
        L"Interface rendered by WebView2.",
        L"About StoryNode", MB_OK | MB_ICONINFORMATION);
}

// --- WebView2 ---------------------------------------------------------------

void CreateWebView(HWND window)
{
    const std::wstring userData = UserDataFolder();

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr,
        userData.empty() ? nullptr : userData.c_str(),
        nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [window](HRESULT, ICoreWebView2Environment* environment) -> HRESULT {
                if (!environment)
                {
                    MessageBoxW(window,
                        L"The WebView2 runtime is not available.\n\n"
                        L"It is preinstalled on Windows 11. On Windows 10 it can be "
                        L"installed from Microsoft's WebView2 page.",
                        L"StoryNode", MB_OK | MB_ICONERROR);
                    PostQuitMessage(1);
                    return S_OK;
                }

                environment->CreateCoreWebView2Controller(window,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [window](HRESULT, ICoreWebView2Controller* controller) -> HRESULT {
                            if (!controller)
                            {
                                MessageBoxW(window, L"Could not create the web view.",
                                            L"StoryNode", MB_OK | MB_ICONERROR);
                                PostQuitMessage(1);
                                return S_OK;
                            }

                            g_controller = controller;
                            g_controller->get_CoreWebView2(&g_webview);

                            ICoreWebView2Settings* settings = nullptr;
                            if (SUCCEEDED(g_webview->get_Settings(&settings)) && settings)
                            {
                                // A context menu and a status bar would look
                                // like a browser, and this is not one.
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_IsStatusBarEnabled(FALSE);
                                settings->put_AreDevToolsEnabled(FALSE);
                                settings->put_IsZoomControlEnabled(FALSE);
                                settings->Release();
                            }

                            RECT bounds = {};
                            GetClientRect(window, &bounds);
                            g_controller->put_Bounds(bounds);

                            EventRegistrationToken token;
                            g_webview->add_WebMessageReceived(
                                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args)
                                        -> HRESULT {
                                        LPWSTR raw = nullptr;
                                        if (SUCCEEDED(args->TryGetWebMessageAsString(&raw)) && raw)
                                        {
                                            const std::string text = Narrow(raw);

                                            // In verification mode the page's
                                            // layout report is the point of
                                            // running, so it is intercepted
                                            // before the bridge sees it.
                                            if (g_options.verify &&
                                                text.find("\"layoutReport\"") != std::string::npos)
                                            {
                                                g_gotLayoutReport = true;
                                                FinishVerification(text);
                                                CoTaskMemFree(raw);
                                                return S_OK;
                                            }

                                            if (g_bridge)
                                            {
                                                // The bridge reports whether
                                                // the document actually
                                                // changed. Marking it dirty on
                                                // every message would make a
                                                // freshly opened document look
                                                // modified the moment the page
                                                // said hello.
                                                const bool changed =
                                                    g_bridge->HandleMessage(Narrow(raw));

                                                // The ready message is the
                                                // host's cue that the page can
                                                // receive. Before it, anything
                                                // sent is dropped by the web
                                                // view.
                                                if (g_bridge->PageReady() && !g_pageReady)
                                                {
                                                    g_pageReady = true;

                                                    // The page is listening,
                                                    // so it is now safe to ask
                                                    // it to measure itself.
                                                    if (g_options.verify)
                                                    {
                                                        RequestLayoutReport();
                                                    }
                                                }

                                                if (changed)
                                                {
                                                    g_dirty = true;
                                                    UpdateTitle();
                                                }
                                            }
                                            CoTaskMemFree(raw);
                                        }
                                        return S_OK;
                                    }).Get(), &token);

                            g_webview->NavigateToString(LoadInterfaceHtml().c_str());
                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());

    if (FAILED(hr))
    {
        MessageBoxW(window,
            L"Could not start WebView2.\n\n"
            L"The runtime is preinstalled on Windows 11. On Windows 10 it can be "
            L"installed from Microsoft's WebView2 page.",
            L"StoryNode", MB_OK | MB_ICONERROR);
        PostQuitMessage(1);
    }
}

// --- window procedure -------------------------------------------------------

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_SIZE:
        if (g_controller)
        {
            RECT bounds = {};
            GetClientRect(window, &bounds);
            g_controller->put_Bounds(bounds);
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wparam))
        {
        case ID_FILE_NEW:
            if (ConfirmDiscard())
            {
                NewDocument();
            }
            return 0;

        case ID_FILE_OPEN:
            OpenDocument();
            return 0;

        case ID_FILE_SAVE:
            // SendMessage's return value is what ConfirmDiscard reads, so it
            // must be non-zero exactly when the file reached the disk.
            SetWindowLongPtrW(window, DWLP_MSGRESULT, SaveDocument(false) ? 1 : 0);
            return TRUE;

        case ID_FILE_SAVE_AS:
            SaveDocument(true);
            return 0;

        case ID_FILE_EXIT:
            PostMessageW(window, WM_CLOSE, 0, 0);
            return 0;

        case ID_VIEW_VALIDATE:
            if (g_bridge)
            {
                g_bridge->SendValidation();
            }
            return 0;

        case ID_HELP_ABOUT:
            ShowAbout();
            return 0;

        default:
            break;
        }
        break;

    case WM_TIMER:
        if (wparam == kVerifyTimeoutId && g_options.verify && !g_gotLayoutReport)
        {
            KillTimer(window, kVerifyTimeoutId);
            // The message goes through the same path as a real report, so the
            // failure is reported the same way and lands in the same place.
            FinishVerification(
                "{\"type\":\"layoutReport\",\"failure\":\"the page did not report its "
                "layout within 30 seconds\"}");
        }
        return 0;

    case WM_CLOSE:
        if (ConfirmDiscard())
        {
            DestroyWindow(window);
        }
        return 0;

    case WM_DESTROY:
        g_webview.Reset();
        g_controller.Reset();
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(window, message, wparam, lparam);
}

HMENU BuildMenu()
{
    HMENU menu = CreateMenu();

    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, ID_FILE_NEW, L"&New\tCtrl+N");
    AppendMenuW(file, MF_STRING, ID_FILE_OPEN, L"&Open...\tCtrl+O");
    AppendMenuW(file, MF_STRING, ID_FILE_SAVE, L"&Save\tCtrl+S");
    AppendMenuW(file, MF_STRING, ID_FILE_SAVE_AS, L"Save &As...\tCtrl+Shift+S");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ID_FILE_EXIT, L"E&xit");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");

    HMENU view = CreatePopupMenu();
    AppendMenuW(view, MF_STRING, ID_VIEW_VALIDATE, L"&Re-check Story");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"&View");

    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, ID_HELP_ABOUT, L"&About");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"&Help");

    return menu;
}

/// The accelerators the menu advertises. Without these the shortcuts in the
/// menu labels are decoration.
HACCEL BuildAccelerators()
{
    ACCEL entries[] = {
        { FVIRTKEY | FCONTROL, 'N', ID_FILE_NEW },
        { FVIRTKEY | FCONTROL, 'O', ID_FILE_OPEN },
        { FVIRTKEY | FCONTROL, 'S', ID_FILE_SAVE },
        { FVIRTKEY | FCONTROL | FSHIFT, 'S', ID_FILE_SAVE_AS },
    };
    return CreateAcceleratorTableW(entries, static_cast<int>(std::size(entries)));
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    g_options = storynode::ParseCommandLine();

    // Per-monitor DPI, declared before any window exists so that Windows does
    // not scale the window after the fact and leave the interface blurry.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // WebView2 needs COM, and the apartment must be single-threaded because
    // the control is created on this thread and the window messages it needs
    // arrive on this thread.
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(comResult))
    {
        MessageBoxW(nullptr, L"Could not initialise COM.", L"StoryNode",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APPICON));
    windowClass.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APPICON));
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClass;

    if (!RegisterClassExW(&windowClass))
    {
        MessageBoxW(nullptr, L"Could not register the window class.", L"StoryNode",
                    MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    g_menu = BuildMenu();

    g_window = CreateWindowExW(
        0, kWindowClass, kWindowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800,
        nullptr, g_menu, instance, nullptr);

    if (!g_window)
    {
        MessageBoxW(nullptr, L"Could not create the window.", L"StoryNode",
                    MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    g_document = storynode::MakeEmptyStory("Untitled");
    g_bridge.reset(new storynode::Bridge(g_document, SendToPage));
    UpdateTitle();

    ShowWindow(g_window, showCommand);
    UpdateWindow(g_window);

    CreateWebView(g_window);

    // In verification mode the window is kept off screen. It still has to
    // exist and still has to be laid out: WebView2 does not lay out a page in
    // a zero-sized or hidden window, so measuring one would report zeroes and
    // the check would pass for the wrong reason.
    if (g_options.verify)
    {
        ShowWindow(g_window, SW_SHOWNOACTIVATE);
        SetWindowPos(g_window, nullptr, -32000, -32000,
                     g_options.width, g_options.height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        SetTimer(g_window, kVerifyTimeoutId, 30000, nullptr);
    }

    HACCEL accelerators = BuildAccelerators();

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0))
    {
        if (accelerators && TranslateAcceleratorW(g_window, accelerators, &message))
        {
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (accelerators)
    {
        DestroyAcceleratorTable(accelerators);
    }
    g_bridge.reset();
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
