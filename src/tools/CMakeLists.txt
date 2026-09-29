# ---------------------------------------------------------------------------
# Developer tools.
#
# Not built by default and not shipped.
#
# probe_resources exists because a resource that is present in the binary but
# not findable by name is invisible otherwise: the application loads, finds
# nothing, and shows a blank window with no error anywhere. It links the same
# generated resource script the application does, so it answers "can the
# application find its assets" rather than "can some other binary find assets
# it does not have".
#
# It lives under src/ rather than in a top-level tools/ directory because it
# needs STORYNODE_UI_RC, which is set in src/CMakeLists.txt. A variable set in
# one directory is not visible to a sibling, so a sibling directory would
# silently build the probe without the resources and report every asset as
# missing — a diagnostic that lies is worse than no diagnostic.
# ---------------------------------------------------------------------------

option(STORYNODE_BUILD_TOOLS "Build the developer tools" OFF)

if(NOT STORYNODE_BUILD_TOOLS)
    return()
endif()

if(NOT WIN32)
    return()
endif()

if(NOT DEFINED STORYNODE_UI_RC OR NOT EXISTS "${STORYNODE_UI_RC}")
    message(WARNING
        "probe_resources needs the generated resource script, which only "
        "exists when the application is built. Configure with "
        "-DSTORYNODE_BUILD_APP=ON and the WebView2 SDK present.")
    return()
endif()

add_executable(probe_resources
    "${CMAKE_CURRENT_SOURCE_DIR}/probe-resources.cpp"
    "${STORYNODE_UI_RC}")

target_compile_definitions(probe_resources PRIVATE UNICODE _UNICODE)
