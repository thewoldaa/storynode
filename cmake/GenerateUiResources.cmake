# ---------------------------------------------------------------------------
# Generate the interface resource script and manifest from the asset tree.
#
# Why this exists
# ---------------
# The interface is split across files so that parallel tasks never edit the
# same one: the canvas owns its directory, the inspector owns its directory,
# and neither touches the other. But the page cannot load them the normal way.
# It is loaded with NavigateToString, which gives it an opaque origin, so a
# <script src> or a fetch to a sibling file is refused by the browser engine
# and there is no local server to ask instead.
#
# So the files are embedded as resources and inlined into the page by the host
# before it navigates. This script produces the two things that makes
# possible:
#
#   ui_resources.rc    one RCDATA entry per asset, named by its path
#   ui_manifest.h      the list of assets, so the host knows what to inline
#
# Both are generated from a glob rather than written by hand. That matters:
# a hand-written list would be a shared file that every task must edit, which
# is the conflict this whole arrangement exists to prevent. Adding a stylesheet
# inside a task's own directory must not require touching anything shared.
#
# Ordering
# --------
# Stylesheets and scripts are inlined in sorted path order. Within a task's own
# directory that is the task's business, and it can control it by naming files.
# Across directories the order does not matter, because every area registers
# itself with the shell rather than assuming it loads first.
# ---------------------------------------------------------------------------

if(NOT DEFINED STORYNODE_UI_ASSETS_DIR)
    message(FATAL_ERROR
        "STORYNODE_UI_ASSETS_DIR is not set. It must be the directory holding "
        "ui.html and the per-area asset directories.")
endif()
if(NOT DEFINED STORYNODE_UI_GENERATED_DIR)
    message(FATAL_ERROR
        "STORYNODE_UI_GENERATED_DIR is not set. The generated resource script "
        "and manifest are written there.")
endif()

file(GLOB_RECURSE asset_files CONFIGURE_DEPENDS
    "${STORYNODE_UI_ASSETS_DIR}/*.css"
    "${STORYNODE_UI_ASSETS_DIR}/*.js"
    "${STORYNODE_UI_ASSETS_DIR}/*.html")

list(SORT asset_files)

set(shell_path "")
set(stylesheet_paths "")
set(script_paths "")

foreach(asset IN LISTS asset_files)
    file(RELATIVE_PATH relative "${STORYNODE_UI_ASSETS_DIR}" "${asset}")
    # Forward slashes in the resource name regardless of platform, because the
    # name is matched against a string the host builds from a path, and the
    # host uses forward slashes.
    string(REPLACE "\\" "/" relative "${relative}")

    if(relative STREQUAL "ui.html")
        set(shell_path "${relative}")
    elseif(relative MATCHES "\\.css$")
        list(APPEND stylesheet_paths "${relative}")
    elseif(relative MATCHES "\\.js$")
        list(APPEND script_paths "${relative}")
    endif()
endforeach()

if(shell_path STREQUAL "")
    message(FATAL_ERROR
        "No ui.html under ${STORYNODE_UI_ASSETS_DIR}.\n"
        "The interface needs a shell page to inline the other files into.")
endif()

# --- the resource script ----------------------------------------------------
#
# Two things about this file are not obvious and both cost an afternoon if got
# wrong. Both are the reason a resource can be present in the binary and still
# be unfindable at runtime, which presents as a blank window with no error.
#
# 1. THE RESOURCE NAME MUST NOT BE QUOTED.
#
#    rc.exe does not treat quotes as delimiters around a name — it stores them
#    as part of it. Writing
#
#        "UI.HTML" RCDATA "C:/path/ui.html"
#
#    produces a resource whose name is the nine-character string `"UI.HTML"`,
#    including both quote marks. FindResourceW(L"UI.HTML") then matches nothing,
#    because the name it is looking for is seven characters and the one stored
#    is nine.
#
#    The file path IS quoted, and must be: it contains spaces and slashes.
#
# 2. THE NAME IS UPPERCASED.
#
#    rc.exe stores string resource names in upper case, and FindResourceW
#    matches case-sensitively. Uppercasing here and in the manifest makes the
#    two agree by construction rather than by luck.
#
# Forward slashes throughout the path, and not file(TO_NATIVE_PATH): CMake
# treats a backslash as an escape introducer, so a Windows path written into a
# generated file silently loses characters — "\ui\assets" arrives as "\ui" plus
# a BEL, "\styles\theme.css" as "\styles" plus a TAB. rc.exe accepts forward
# slashes, so there is no reason to produce backslashes at all.

set(rc_lines)
set(style_list)
set(script_list)
set(shell_name "")

foreach(asset IN LISTS asset_files)
    file(RELATIVE_PATH relative "${STORYNODE_UI_ASSETS_DIR}" "${asset}")
    string(REPLACE "\\" "/" relative "${relative}")
    string(TOUPPER "${relative}" resource_name)

    string(REPLACE "\\" "/" asset_path "${asset}")

    string(APPEND rc_lines "${resource_name} RCDATA \"${asset_path}\"\n")

    if(relative STREQUAL "ui.html")
        set(shell_name "${resource_name}")
    elseif(relative MATCHES "\\.css$")
        string(APPEND style_list "    L\"${resource_name}\",\n")
    elseif(relative MATCHES "\\.js$")
        string(APPEND script_list "    L\"${resource_name}\",\n")
    endif()
endforeach()

if(shell_name STREQUAL "")
    message(FATAL_ERROR
        "No ui.html under ${STORYNODE_UI_ASSETS_DIR}.\n"
        "The interface needs a shell page to inline the other files into.")
endif()

set(rc_content
"// Generated by cmake/GenerateUiResources.cmake - do not edit.\n\
//\n\
// One RCDATA entry per interface asset, named by its path relative to\n\
// src/ui/assets. The host looks each one up by that name.\n\
\n\
#include <windows.h>\n\
\n\
${rc_lines}")

file(WRITE "${STORYNODE_UI_GENERATED_DIR}/ui_resources.rc" "${rc_content}")

# --- the manifest -----------------------------------------------------------
#
# A header rather than a runtime directory scan. Scanning the resource section
# would work, but it cannot say which entries are stylesheets and which are
# scripts, and it cannot order them.
#
# The names here are the same uppercased strings the resource script writes,
# built in the same loop above. Two lists that have to agree are two lists that
# eventually disagree, so there is only one.

set(manifest_content
"// Generated by cmake/GenerateUiResources.cmake - do not edit.\n\
//\n\
// The interface assets, in the order the host inlines them. Regenerated\n\
// whenever a file is added or removed, so no shared file lists them.\n\
\n\
#pragma once\n\
\n\
namespace storynode::ui {\n\
\n\
/// The shell page. Everything else is inlined into it.\n\
inline constexpr const wchar_t* kShellPath = L\"${shell_name}\";\n\
\n\
/// Stylesheets, inlined into the head in this order.\n\
inline constexpr const wchar_t* kStylesheets[] = {\n\
${style_list}};\n\
\n\
/// Scripts, inlined before the closing body tag in this order.\n\
inline constexpr const wchar_t* kScripts[] = {\n\
${script_list}};\n\
\n\
inline constexpr int kStylesheetCount =\n\
    sizeof(kStylesheets) / sizeof(kStylesheets[0]);\n\
inline constexpr int kScriptCount = sizeof(kScripts) / sizeof(kScripts[0]);\n\
\n\
} // namespace storynode::ui\n")

file(WRITE "${STORYNODE_UI_GENERATED_DIR}/ui_manifest.h" "${manifest_content}")

list(LENGTH stylesheet_paths style_count)
list(LENGTH script_paths script_count)
message(STATUS "Interface assets: ${shell_path}, ${style_count} stylesheets, ${script_count} scripts")
