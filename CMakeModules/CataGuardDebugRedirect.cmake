# Refuse a Debug configure on Apple platforms (issue #196).
#
# The Debug branch in the top-level CMakeLists.txt sets
# CMAKE_RUNTIME_OUTPUT_DIRECTORY to ${CMAKE_SOURCE_DIR}, so a Debug configure
# on macOS drops cataclysm-bn-tiles and cata_test-tiles in the repo root.
# Those copies keep launching after the real build lives in
# out/build/osx-arm-slim, and stale binaries then masquerade as current ones
# (AGENTS.md, "Binary path"). No macOS preset is Debug, and a bare
# `cmake -B <dir>` defaults CMAKE_BUILD_TYPE to Debug, so the trap is armed by
# default; refusing at configure time closes it at its source.
#
# Non-Apple platforms are untouched: the guard is a no-op unless APPLE is set.
if (APPLE AND (CMAKE_BUILD_TYPE STREQUAL "Debug"))
    message(FATAL_ERROR
        "Debug builds are refused on Apple platforms: the Debug build type "
        "redirects binaries to the repo root, where stale copies run happily "
        "against newer sources. Configure with the osx-arm-slim preset "
        "instead: `cmake --preset osx-arm-slim`. If this build directory was "
        "ever configured as Debug, delete it; the redirect stays in its "
        "CMakeCache.txt even after a reconfigure.")
endif ()
