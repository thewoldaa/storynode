# ---------------------------------------------------------------------------
# Tests.
#
# The test binary links the core library and nothing else, so it runs with no
# window, no message loop and no WebView2 runtime. That is the payoff for
# keeping core free of Windows headers: the tests are fast and they run
# anywhere.
# ---------------------------------------------------------------------------

file(
    GLOB STORYNODE_TEST_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_executable(storynode_tests ${STORYNODE_TEST_SOURCES})

target_include_directories(
    storynode_tests
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(storynode_tests PRIVATE storynode_core)

if(TARGET storynode_bridge)
    target_link_libraries(storynode_tests PRIVATE storynode_bridge)
endif()

target_compile_options(storynode_tests PRIVATE ${STORYNODE_WARNINGS})

add_test(NAME storynode_tests COMMAND storynode_tests)

# A test that hangs is worse than a test that fails, because CI waits for the
# timeout instead of reporting. Two minutes is far longer than this suite
# takes and short enough that a hang is obvious.
set_tests_properties(storynode_tests PROPERTIES TIMEOUT 120)

# The UI verification tests. Registered from a separate file so the test list
# stays readable.
include("${CMAKE_CURRENT_SOURCE_DIR}/VerifyCMakeLists.txt")
