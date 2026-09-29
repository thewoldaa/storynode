# ---------------------------------------------------------------------------
# StoryNode - a visual editor for branching interactive fiction.
#
# The Windows-only parts are guarded so that the core library and its tests
# configure and build on any platform. That is not portability for its own
# sake: it means the model, the file format and the exporters can be tested
# on a Linux CI runner, where the feedback is faster and the runner is
# cheaper than the Windows one.
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.25)

project(storynode
    VERSION 0.1.0
    DESCRIPTION "Visual editor for branching interactive fiction"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Language level
#
# C++17: structured bindings and std::optional are used throughout, and
# std::filesystem is available without linking a separate library on every
# toolchain this project targets.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Output layout
#
# Everything lands in one bin directory regardless of configuration, so the
# harness's per-task build directories and the CI build directory all put the
# executable somewhere predictable: <build>/bin/storynode.exe.
# ---------------------------------------------------------------------------

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

foreach(config IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/lib")
endforeach()

# ---------------------------------------------------------------------------
# Warnings
#
# Treated as errors on our own targets so a warning is fixed when it appears
# rather than accumulating. Third-party code is built without this, which is
# why it is applied per-target rather than globally.
# ---------------------------------------------------------------------------

if(MSVC)
    set(STORYNODE_WARNINGS /W4 /permissive- /WX)
    # 4100: unreferenced formal parameter. Fires on the Win32 message handlers
    # and on stub implementations, where the parameter is part of a required
    # signature.
    list(APPEND STORYNODE_WARNINGS /wd4100)
else()
    set(STORYNODE_WARNINGS -Wall -Wextra -Wpedantic -Werror)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

option(STORYNODE_BUILD_TESTS "Build the unit tests" ON)
option(STORYNODE_BUILD_APP "Build the editor application" ON)

# ---------------------------------------------------------------------------
# Dependencies
#
# WebView2 is only needed by the application, and it is fetched by
# scripts/fetch-deps.ps1 rather than vendored. When it is absent the core
# library still builds and the tests still run, so a contributor without the
# SDK can work on the model and the format.
# ---------------------------------------------------------------------------

set(STORYNODE_WEBVIEW2_DIR "${CMAKE_SOURCE_DIR}/third_party/WebView2.1.0.4129.50"
    CACHE PATH "Directory containing the extracted WebView2 SDK")

set(STORYNODE_HAVE_WEBVIEW2 OFF)
if(WIN32 AND STORYNODE_BUILD_APP)
    if(EXISTS "${STORYNODE_WEBVIEW2_DIR}/build/native/include/WebView2.h")
        set(STORYNODE_HAVE_WEBVIEW2 ON)
    else()
        message(WARNING
            "WebView2 SDK not found at ${STORYNODE_WEBVIEW2_DIR}.\n"
            "The editor application will not be built.\n"
            "Run: powershell -ExecutionPolicy Bypass -File scripts/fetch-deps.ps1")
    endif()
endif()

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------

add_subdirectory(src)

if(STORYNODE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
