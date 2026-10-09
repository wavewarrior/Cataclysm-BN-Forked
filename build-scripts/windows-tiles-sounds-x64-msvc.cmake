# Guard: only run in the top-level project, not inside FetchContent subprojects.
if(NOT "${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
    return()
endif()

#[=======================================================================[

windows-tiles-sounds-x64-msvc
-----------------------------

Pre-load script for Windows builds with Ninja Multi-Config and MSVC.

#]=======================================================================]

# /bigobj, plus /utf-8 as in build-scripts/MSVC.cmake (the CI toolchain, which this preset
# does not load): the vendored fmt static_asserts "Unicode support requires compiling with
# /utf-8". Added as compile options, NOT by setting CMAKE_<LANG>_FLAGS here: this file runs
# before project() initialises those, so a normal variable would shadow CMake's MSVC
# defaults (/DWIN32 /D_WINDOWS /EHsc) and every TU would build without C++ unwinding (C4530).
add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/bigobj;/utf-8>")
# CMake's MSVC default links /debug configs /INCREMENTAL, which updates the PDB in place so it
# fragments and grows on every relink (3.6 -> 5.4 GB in a day) until LNK1140 "limit exceeded
# for program database". CI's MSVC.cmake passes /INCREMENTAL:NO for this; the larger PDB page
# size is only headroom. Debug keeps incremental linking (set further down).
add_link_options("$<$<CONFIG:RelWithDebInfo,Release>:/INCREMENTAL:NO>")
add_link_options("$<$<LINK_LANGUAGE:C,CXX>:/PDBPAGESIZE:16384>")

# --- Box2D physics: ON by default for this preset -------------------------
# The root CMakeLists declares `option(BOX2D "..." OFF)`, a global default that
# also covers Linux/macOS. This file is CMAKE_PROJECT_INCLUDE_BEFORE, so it runs
# at the top-level project() call — well before that option() — and option()
# never overwrites an existing cache entry. Seeding it here therefore flips the
# default for the Windows MSVC preset ONLY, leaving other platforms untouched.
#
# Deliberately NOT forced: if the entry already exists (a `-DBOX2D=OFF` on the
# command line, or a previously configured tree) this set() is a no-op, so the
# override still wins.
#
# NOTE: this configure preset is Ninja Multi-Config (Debug;RelWithDebInfo;Release)
# and a CMake cache is per-tree, not per-configuration — so BOX2D is on for all
# three build presets, not just windows-msvc-relwithdebinfo. There is no
# per-config way to express this.
set(BOX2D ON CACHE BOOL "Enable Box2D physics for vehicle VV collision")

# Speed up Debug linking: incremental link + fast PDB generation.
# /INCREMENTAL updates the binary incrementally using an .ilk file.
# /DEBUG:FASTLINK avoids merging all type info into one large PDB.
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /INCREMENTAL /DEBUG:FASTLINK")
set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} /INCREMENTAL /DEBUG:FASTLINK")

if (NOT $ENV{VCPKG_INSTALLATION_ROOT} STREQUAL "")
    set(ENV{VCPKG_ROOT} $ENV{VCPKG_INSTALLATION_ROOT})
endif()
if ("$ENV{VCPKG_ROOT}" STREQUAL "" AND WIN32)
    set(ENV{VCPKG_ROOT} $CACHE{VCPKG_ROOT})
endif()

include(${CMAKE_SOURCE_DIR}/build-scripts/VsDevCmd.cmake)

set(CONFIGURE_PRESET "windows-tiles-sounds-x64-msvc")
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/CMakeUserPresets.json")
    configure_file(
        ${CMAKE_SOURCE_DIR}/build-scripts/CMakeUserPresets.json.in
        ${CMAKE_SOURCE_DIR}/CMakeUserPresets.json
        @ONLY
    )
    message(STATUS "Generated CMakeUserPresets.json for terminal builds.")
endif()

find_program(CCACHE_EXE ccache)
if(CCACHE_EXE)
    set(CMAKE_C_COMPILER_LAUNCHER   ccache)
    set(CMAKE_CXX_COMPILER_LAUNCHER ccache)
endif()

set(GETTEXT_VERSION "1.0-v1.18-r1")
set(GETTEXT_DIR "${CMAKE_SOURCE_DIR}/build-data/gettext")
set(GETTEXT_ARCHIVE "${CMAKE_SOURCE_DIR}/build-data/gettext-${GETTEXT_VERSION}.zip")
set(GETTEXT_URL "https://github.com/mlocati/gettext-iconv-windows/releases/download/v${GETTEXT_VERSION}/gettext1.0-iconv1.18-static-64.zip")

if(NOT EXISTS "${GETTEXT_DIR}/bin/msgfmt.exe")
    message(STATUS "Downloading pre-built gettext binaries...")
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/build-data")
    
    file(DOWNLOAD
        "${GETTEXT_URL}"
        "${GETTEXT_ARCHIVE}"
        SHOW_PROGRESS
        STATUS DOWNLOAD_STATUS
        TLS_VERIFY ON
    )
    
    list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
    if(NOT STATUS_CODE EQUAL 0)
        list(GET DOWNLOAD_STATUS 1 ERROR_MESSAGE)
        message(FATAL_ERROR "Failed to download gettext: ${ERROR_MESSAGE}")
    endif()
    
    message(STATUS "Extracting gettext binaries...")
    file(ARCHIVE_EXTRACT
        INPUT "${GETTEXT_ARCHIVE}"
        DESTINATION "${GETTEXT_DIR}"
    )
    
    file(REMOVE "${GETTEXT_ARCHIVE}")
    message(STATUS "gettext installed to: ${GETTEXT_DIR}")
endif()

set(GETTEXT_MSGFMT_BINARY "${GETTEXT_DIR}/bin/msgfmt.exe" CACHE FILEPATH "Path to msgfmt executable" FORCE)
