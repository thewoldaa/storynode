# ---------------------------------------------------------------------------
# Sources are globbed rather than listed.
#
# Deliberate, and the reason is the worktree harness: one worktree per task,
# and an explicit source list is a file every task must edit. Every task
# editing the same file means every task conflicts with every other task,
# which is exactly what the harness exists to prevent. Adding a source file
# inside a task's own territory must not require touching a shared file.
#
# CONFIGURE_DEPENDS makes the build re-run CMake when a file is added or
# removed, so the glob does not go stale.
# ---------------------------------------------------------------------------

# --- core ------------------------------------------------------------------
#
# No Windows headers, no WebView2. Builds and runs in a test process with no
# window and no message loop. This is what makes the model testable and what
# lets the parallel tasks in later waves be genuinely independent.

file(GLOB_RECURSE STORYNODE_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp")

add_library(storynode_core STATIC ${STORYNODE_CORE_SOURCES})

target_include_directories(storynode_core
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_options(storynode_core PRIVATE ${STORYNODE_WARNINGS})

# ---------------------------------------------------------------------------
# bridge
#
# The message protocol between the page and the host. Free of WebView2 types
# on purpose: the host calls HandleMessage with text that arrived from the web
# view and the bridge calls back with text to send, so the protocol can be
# tested without a window. It links the core and nothing platform-specific,
# which is why it builds on every platform even though the editor it serves is
# Windows-only.
# ---------------------------------------------------------------------------

file(GLOB STORYNODE_BRIDGE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/app/Bridge.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/app/LayoutReport.cpp")

if(STORYNODE_BRIDGE_SOURCES)
    add_library(storynode_bridge STATIC ${STORYNODE_BRIDGE_SOURCES})
    target_include_directories(storynode_bridge PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(storynode_bridge PUBLIC storynode_core)
    target_compile_options(storynode_bridge PRIVATE ${STORYNODE_WARNINGS})
endif()

# ---------------------------------------------------------------------------
# platform
#
# Win32 only: file dialogs, DPI, window chrome. Isolated so that core never
# needs a Windows header.
# ---------------------------------------------------------------------------

if(WIN32)
    file(GLOB_RECURSE STORYNODE_PLATFORM_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/*.cpp")

    if(STORYNODE_PLATFORM_SOURCES)
        add_library(storynode_platform STATIC ${STORYNODE_PLATFORM_SOURCES})
        target_include_directories(storynode_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(storynode_platform PUBLIC storynode_core)
        target_compile_options(storynode_platform PRIVATE ${STORYNODE_WARNINGS})
        target_link_libraries(storynode_platform PRIVATE comdlg32 shell32 ole32)
    endif()
endif()

# ---------------------------------------------------------------------------
# application
# ---------------------------------------------------------------------------

if(WIN32 AND STORYNODE_BUILD_APP AND STORYNODE_HAVE_WEBVIEW2)

    file(GLOB_RECURSE STORYNODE_APP_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/app/*.cpp")

    add_executable(storynode WIN32 ${STORYNODE_APP_SOURCES})

    target_include_directories(storynode PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/ui/assets"
        "${STORYNODE_WEBVIEW2_DIR}/build/native/include")

    target_link_libraries(storynode PRIVATE
        storynode_core
        "${STORYNODE_WEBVIEW2_DIR}/build/native/x64/WebView2LoaderStatic.lib"
        user32 gdi32 ole32 oleaut32 shlwapi version)

    if(TARGET storynode_bridge)
        target_link_libraries(storynode PRIVATE storynode_bridge)
    endif()

    if(TARGET storynode_platform)
        target_link_libraries(storynode PRIVATE storynode_platform)
    endif()

    target_compile_options(storynode PRIVATE ${STORYNODE_WARNINGS})

    # UNICODE and _UNICODE make the Win32 macros resolve to their wide forms:
    # RT_RCDATA, IDC_ARROW and friends are MAKEINTRESOURCE, which picks the
    # ANSI or wide variant based on these defines. Without them every
    # resource lookup passes an LPSTR where an LPCWSTR is expected, and the
    # errors point at the call site rather than at the missing define.
    #
    # The project is Unicode throughout: JSON is UTF-8, the page is UTF-8, and
    # Win32 is UTF-16. Nothing here wants an ANSI code page.
    target_compile_definitions(storynode PRIVATE UNICODE _UNICODE)

    # The HTML interface is compiled into the executable as a resource and
    # loaded with NavigateToString, so there is no temporary file to write and
    # no local HTTP server to run.
    set(STORYNODE_UI_RESOURCE "${CMAKE_CURRENT_SOURCE_DIR}/ui/assets/app.rc")
    if(EXISTS "${STORYNODE_UI_RESOURCE}")
        target_sources(storynode PRIVATE "${STORYNODE_UI_RESOURCE}")
    endif()

    # The resource compiler needs to find ui.html next to app.rc.
    set_property(TARGET storynode PROPERTY
        VS_GLOBAL_ResourceIncludePath "${CMAKE_CURRENT_SOURCE_DIR}/ui/assets")
endif()
