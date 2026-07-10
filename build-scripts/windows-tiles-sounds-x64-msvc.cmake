# Guard: only run in the top-level project, not inside FetchContent subprojects.
if(NOT "${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
    return()
endif()

#[=======================================================================[

windows-tiles-sounds-x64-msvc
-----------------------------

Pre-load script for Windows builds with Ninja Multi-Config and MSVC.

#]=======================================================================]

# Ensure /bigobj is set — vcpkg toolchain can override CMAKE_CXX_FLAGS_INIT.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /bigobj")

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
