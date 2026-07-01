cmake_minimum_required(VERSION 3.24)

if (POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif ()

project(CataclysmBN C CXX)

list(APPEND CMAKE_MODULE_PATH
        ${CMAKE_SOURCE_DIR}/CMakeModules)
if (NOT VCPKG_MANIFEST_MODE)
    list(APPEND CMAKE_MODULE_PATH
            ${CMAKE_SOURCE_DIR}/CMakeModules/Find)
endif()


option(CATA_FORMAT_TARGETS "Enable C++ formatting targets" ON)
if (CATA_FORMAT_TARGETS)
    include(FormatSource)
endif ()
include(FetchContent)

set(CMAKE_TLS_VERIFY ON)

# Build options
include(CMakeDependentOption)
include(ExternalProject)
option(DYNAMIC_ATLAS "Enable dynamic atlas generation" ON)
option(SOUND "Support for in-game sounds & music." "OFF")
option(BACKTRACE "Support for printing stack backtraces on crash" "ON")
option(LIBBACKTRACE "Print backtrace with libbacktrace." "OFF")
option(USE_XDG_DIR "Use XDG directories for save and config files." "OFF")
# On Windows, default to current directory for backwards compatibility
# On other platforms, use home directory
if(WIN32)
    set(USE_HOME_DIR_DEFAULT OFF)
else()
    set(USE_HOME_DIR_DEFAULT ON)
endif()
option(USE_HOME_DIR "Use user's home directory for save and config files." ${USE_HOME_DIR_DEFAULT})
cmake_dependent_option(USE_PREFIX_DATA_DIR
    "Use UNIX system directories for game data in release build." ON
    "UNIX" OFF
)
set(LANGUAGES "" CACHE STRING
        "Compile localization files for specified languages. \
        List of language ids separated by semicolon. \
        Set to 'all' or leave empty to compile all, set to 'none' to compile none.")
option(DYNAMIC_LINKING
        "Use dynamic linking. Or use static to remove MinGW dependency instead." "ON")
option(USE_UNITY_BUILD "Use unity build." "OFF")
option(JSON_FORMAT "Build JSON formatter" "OFF")
option(CATA_CCACHE "Try to find and build with ccache" "ON")
option(TESTS "Compile Cata's tests" "ON")
find_program(DENO_BINARY deno)
if(DENO_BINARY)
    set(LUA_DOCS_DEFAULT ON)
else()
    set(LUA_DOCS_DEFAULT OFF)
endif()
option(LUA_DOCS_ON_BUILD "Generate Lua reference/annotations after building cataclysm-bn-tiles (requires deno)." ${LUA_DOCS_DEFAULT})
option(CATA_CLANG_TIDY_PLUGIN "Build Cata's custom clang-tidy plugin" "OFF")
option(USE_TRACY "Use Tracy profiler" "OFF")
set(CATA_CLANG_TIDY_INCLUDE_DIR "" CACHE STRING
        "Path to internal clang-tidy headers required for plugin (e.g. ClangTidy.h)")
set(CATA_CHECK_CLANG_TIDY "" CACHE STRING "Path to check_clang_tidy.py for plugin tests")
set(GIT_BINARY "" CACHE STRING "Git binary name or path.")
set(LINKER "" CACHE STRING "Custom Linker to use")
set(GETTEXT_MSGFMT_BINARY "" CACHE FILEPATH "msgfmt binary name or path.")
option(BUILD_SDL3 "Force builds SDL3 and its modules from source, instead of using system packages" "OFF")

if (TESTS)
include(CTest)
endif()

message(STATUS "${PROJECT_NAME} build environment --")
message(STATUS "Build realm is: ${CMAKE_SYSTEM_NAME} ${CMAKE_HOST_SYSTEM_PROCESSOR}")

if (LINKER)
    add_link_options("-fuse-ld=${LINKER}")
endif ()

# For multi-config generators (e.g. Visual Studio), CMAKE_BUILD_TYPE is not used;
# the configuration is selected at build time via --config flag.
get_property(_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (NOT CMAKE_BUILD_TYPE AND NOT _is_multi_config)
    set(CMAKE_BUILD_TYPE Debug)
endif ()

add_definitions(-DCMAKE)

include(GetGitRevisionDescription)
git_describe(GIT_VERSION --tags --always --match "[0-9A-Z]*.[0-9A-Z]*")
if (NOT "${GIT_VERSION}" MATCHES "GIT-NOTFOUND")
    string(REPLACE "-NOTFOUND" "" GIT_VERSION ${GIT_VERSION})
    string(TIMESTAMP _timestamp %Y-%m-%d-%H%M)
    file(WRITE ${CMAKE_SOURCE_DIR}/src/version.h
         "// NOLINT(cata-header-guard)\n\#define VERSION \"${GIT_VERSION}\"\n\#define BUILD_TIMESTAMP \"${_timestamp}\"\n")
    message(STATUS "${PROJECT_NAME} build version is: ${GIT_VERSION}")
    add_definitions(-DGIT_VERSION)

    # get_git_head_revision() does not work with worktrees in Windows
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE _sha1
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    file(WRITE VERSION.txt "\
build type: ${BUILD_PRESET_NAME}\n\
build number: ${_timestamp}\n\
commit sha: ${_sha1}\n\
commit url: https://github.com/cataclysmbn/Cataclysm-BN/commit/${_sha1}"
    )
endif ()

#OS Check Placeholders. Will be used for BINDIST
if ("${CMAKE_SYSTEM_NAME}" MATCHES "Linux")
    set(_OS_LINUX_ 1)
endif ()

if ("${CMAKE_SYSTEM_NAME}" MATCHES "FreeBSD")
    set(_OS_FREEBSD_ 1)
endif ()

if ("${CMAKE_SYSTEM_NAME}" MATCHES "Darwin")
    set(_OS_DARWIN_ 1)
    add_definitions(-DMACOSX)
    add_definitions(-DOSX_SDL3_LIBS)
endif ()

include(CheckCXXCompilerFlag)

#FIXME: Add dest build choice: m32 for 32 bit or m64 for 64 bit version
#add_definitions("-m32")
#SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32")
#SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32")
#SET(CMAKE_SHARED_LIBRARY_C_FLAGS "${CMAKE_SHARED_LIBRARY_C_FLAGS} -m32")
#SET(CMAKE_SHARED_LIBRARY_CXX_FLAGS "${CMAKE_SHARED_LIBRARY_CXX_FLAGS} -m32")

if (NOT CMAKE_FIND_PACKAGE_PREFER_CONFIG)
    set(CMAKE_FIND_PACKAGE_PREFER_CONFIG OFF)
endif ()

find_package(PkgConfig)
find_package(SQLite3)
if (NOT TARGET SQLite3::SQLite3 AND TARGET SQLite::SQLite3)
    add_library(SQLite3::SQLite3 ALIAS SQLite::SQLite3)
endif ()
find_package(ZLIB)
if (NOT DYNAMIC_LINKING)
    if(NOT MSVC)
        set(CMAKE_FIND_LIBRARY_SUFFIXES ".a;.dll.a")
    endif()
    set(BUILD_SHARED_LIBRARIES OFF)
    check_cxx_compiler_flag (-static HAVE_STATIC_FLAG)
    if (HAVE_STATIC_FLAG)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
    endif ()
    # Workaround for cmake link library guesser
    set(CMAKE_EXE_LINK_DYNAMIC_C_FLAGS)       # remove -Wl,-Bdynamic
    set(CMAKE_EXE_LINK_DYNAMIC_CXX_FLAGS)
    set(CMAKE_SHARED_LIBRARY_C_FLAGS)         # remove -fPIC
    set(CMAKE_SHARED_LIBRARY_CXX_FLAGS)
    set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)    # remove -rdynamic
    set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS)
endif ()

message(STATUS "${PROJECT_NAME} build options --")

# Preset variables
if (NOT LANGUAGES)
    set(LANGUAGES all)
endif ()

if (GIT_BINARY)
    set(GIT_EXECUTABLE ${GIT_BINARY})
else ()
    find_package(Git)
    if (NOT GIT_FOUND)
        message(WARNING
            "Git binary not found. Build version will be set to NULL. \
            Install Git package or use -DGIT_BINARY to set path to git binary.")
    endif ()
endif ()

# Can't use both home and xdg directories
if (USE_XDG_DIR)
    set(USE_HOME_DIR OFF)
endif ()

# Set build types and display info
if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    set(RELEASE 0)
    message(STATUS "Build ${PROJECT_NAME} in development mode --")
    message(STATUS "Binaries will be located in: " ${CMAKE_SOURCE_DIR})
    set(CMAKE_VERBOSE_MAKEFILE ON)
    # Since CataclysmBN does not respect PREFIX for development builds
    # and has funny path handlers, we should create resulting Binaries
    # in the source directory
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR} CACHE PATH
            "Single Directory for all Executables.")
    set(CMAKE_INSTALL_BINDIR .)
else ()
    set(RELEASE 1)
    add_definitions(-DRELEASE)
    # Use CMAKE_INSTALL_PREFIX as storage of data,gfx, etc.. Useful only on *nix OS.
    if(USE_PREFIX_DATA_DIR)
        if ("${CMAKE_SYSTEM_NAME}" MATCHES "(Linux|FreeBSD|Darwin)")
            set(CMAKE_INSTALL_DATAROOTDIR "${CMAKE_INSTALL_PREFIX}/share/cataclysm-bn")
            add_definitions(-DPREFIX=${CMAKE_INSTALL_PREFIX})
            configure_file(
                "${CMAKE_SOURCE_DIR}/src/prefix.h.in"
                "${CMAKE_SOURCE_DIR}/src/prefix.h"
                @ONLY)
            add_definitions(-DDATA_DIR_PREFIX)
        endif ()
    else()
        set(CMAKE_INSTALL_DATADIR   data)
        set(CMAKE_INSTALL_DOCDIR    .   )
        set(CMAKE_INSTALL_LOCALEDIR lang)
        set(CMAKE_INSTALL_BINDIR    .   )
    endif ()
    message(STATUS "CMAKE_INSTALL_PREFIX          : ${CMAKE_INSTALL_PREFIX}")
    message(STATUS "CMAKE_INSTALL_BINDIR          : ${CMAKE_INSTALL_BINDIR}")
    message(STATUS "DESKTOP_ENTRY_PATH            : ${DESKTOP_ENTRY_PATH}")
    message(STATUS "PIXMAPS_ENTRY_PATH            : ${PIXMAPS_ENTRY_PATH}")
    message(STATUS "PIXMAPS_UNITY_ENTRY_PATH      : ${PIXMAPS_UNITY_ENTRY_PATH}")
    message(STATUS "MANPAGE_ENTRY_PATH            : ${MANPAGE_ENTRY_PATH}")
endif ()

message(STATUS "GIT_BINARY                    : ${GIT_EXECUTABLE}")
message(STATUS "DYNAMIC_LINKING               : ${DYNAMIC_LINKING}")
message(STATUS "DYNAMIC_ATLAS                 : ${DYNAMIC_ATLAS}")
message(STATUS "SOUND                         : ${SOUND}")
message(STATUS "BACKTRACE                     : ${BACKTRACE}")
message(STATUS "LIBBACKTRACE                  : ${LIBBACKTRACE}")
message(STATUS "USE_TRACY                     : ${USE_TRACY}")
message(STATUS "USE_XDG_DIR                   : ${USE_XDG_DIR}")
message(STATUS "USE_HOME_DIR                  : ${USE_HOME_DIR}")
message(STATUS "UNITY_BUILD                   : ${USE_UNITY_BUILD}")
message(STATUS "LANGUAGES                     : ${LANGUAGES}")
if (LINKER)
    message(STATUS "LINKER                        : ${LINKER}")
endif ()
message(STATUS "BUILD_SDL3                    : ${BUILD_SDL3}")

message(STATUS "See CMake compiling guide for details and more info --")

if (NOT MSVC)
    set(CATA_WARNINGS
        "-Wall -Wextra \
       -Wformat-signedness \
       -Wlogical-op \
       -Wmissing-declarations \
       -Wmissing-noreturn \
       -Wnon-virtual-dtor \
       -Wold-style-cast \
       -Woverloaded-virtual \
       -Wpedantic \
       -Wsuggest-override \
       -Wunused-macros \
       -Wno-zero-as-null-pointer-constant \
       -Wno-unknown-warning-option \
       -Wno-dangling-reference \
       -Wno-range-loop-analysis")
    if("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
       set(CATA_WARNINGS "${CATA_WARNINGS} -Wno-unknown-warning-option")
    else()
       set(CATA_WARNINGS "${CATA_WARNINGS} -Wno-unknown-warning")
    endif()
    if (NOT "${CMAKE_SYSTEM_NAME}" MATCHES "Windows")
        set(CATA_WARNINGS "${CATA_WARNINGS} -Wredundant-decls")
    endif ()
    set(CATA_OTHER_FLAGS "${CATA_OTHER_FLAGS} -fsigned-char -fno-builtin-std-forward_like")
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-finite-math-only>)
    # Add debug symbols for non-release builds or when backtrace is enabled
    if (BACKTRACE OR NOT RELEASE)
        set(CATA_OTHER_FLAGS "${CATA_OTHER_FLAGS} -g1")
        # Use debug prefix map for ccache compatibility with git worktrees
        # see https://ccache.dev/manual/latest.html#_compiling_in_different_directories
        file(RELATIVE_PATH REL_SRC_PATH "${CMAKE_BINARY_DIR}" "${CMAKE_SOURCE_DIR}")
        set(CATA_OTHER_FLAGS "${CATA_OTHER_FLAGS} -ffile-prefix-map=${REL_SRC_PATH}=/")
    endif()
    # Compact the whitespace in the warning string
    string(REGEX REPLACE "[\t ]+" " " CATA_WARNINGS "${CATA_WARNINGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CATA_WARNINGS} ${CATA_OTHER_FLAGS}")
    set(CMAKE_CXX_FLAGS_DEBUG "-Og -g2")
endif ()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Force out-of-source build
if ("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    message(FATAL_ERROR
            "This project requires an out of source build. \
            Remove the file 'CMakeCache.txt' found in this directory before continuing; \
            create a separate build directory and run 'cmake [options] <srcs>' from there. \
            See CMake compiling guide for details and more info.")
endif ()

#set(THREADS_USE_PTHREADS_WIN32 True)
set(CMAKE_THREAD_PREFER_PTHREAD True)
find_package(Threads REQUIRED)

if (USE_TRACY)

    if (NOT TRACY_VERSION)
        if("${CMAKE_SYSTEM_NAME}" MATCHES "Linux")
            set(TRACY_VERSION 6d1deb5640ed11da01995fb1791115cfebe54dbf)
        else()
            set(TRACY_VERSION v0.10)
        endif()
        message(STATUS "Using tracy version: ${TRACY_VERSION}")
    endif()

    FetchContent_Declare(
        tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG "${TRACY_VERSION}"
        GIT_PROGRESS TRUE)

    FetchContent_MakeAvailable(tracy)
    set_target_properties(TracyClient PROPERTIES TRACY_ENABLE ON TRACY_ON_DEMAND ON)

    if(NOT MSVC)
        target_compile_options(TracyClient PRIVATE "-Wno-everything")
    endif()
    if (NOT DYNAMIC_LINKING)
        set_target_properties(TracyClient PROPERTIES TRACY_STATIC ON)
    endif ()
endif ()

# Check for build types and libraries
# Find SDL, SDL_ttf & SDL_image for graphical install
message(STATUS "Searching for SDL3 library --")
    if (NOT CMAKE_FIND_PACKAGE_PREFER_CONFIG)
        if (NOT BUILD_SDL3)
            set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)
            # ^^ Use sdl3-config.cmake provided by the system or VCPKG
            find_package(SDL3 CONFIG)
            set(CMAKE_FIND_PACKAGE_PREFER_CONFIG OFF)
        endif ()
        if(NOT SDL3_FOUND)
            message("Installing SDL3 from Source")
            set(SDL_TEST_LIBRARY OFF)
            SET(SDL_SHARED ON)
            # Install via FetchContent
            FetchContent_Declare(
                    SDL3
                    URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.10.tar.gz
                    EXCLUDE_FROM_ALL
            )
            FetchContent_MakeAvailable(SDL3)
            if (DYNAMIC_LINKING)
                install(TARGETS SDL3-shared DESTINATION ${CMAKE_LIBRARY_OUTPUT_DIRECTORY} )
            endif ()
        else()
            message("Using system SDL3")
        endif()
    endif()
    if (NOT (SDL3_FOUND OR TARGET SDL3::SDL3 OR TARGET SDL3::SDL3-static))
        message(FATAL_ERROR
                "This project requires SDL3 to be installed. \
                 Please install the SDL3 development libraries. \
                 See CMake compiling guide for details and more info.")
    endif ()

    message(STATUS "Searching for SDL3_ttf library --")
    if (NOT CMAKE_FIND_PACKAGE_PREFER_CONFIG)
        if (NOT BUILD_SDL3)
            set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)
            # ^^ Use sdl3_ttf-config.cmake provided by the system or VCPKG
            find_package(SDL3_ttf CONFIG)
            set(CMAKE_FIND_PACKAGE_PREFER_CONFIG OFF)
        endif ()
        if(NOT SDL3_ttf_FOUND)
            message("Installing SDL3_ttf from Source")
            set(SDLTTF_PLUTOSVG OFF)
            # Install via FetchContent
            FetchContent_Declare(
                    SDL3_TTF
                    URL https://github.com/libsdl-org/SDL_ttf/archive/refs/tags/release-3.2.2.tar.gz
                    EXCLUDE_FROM_ALL
            )
            FetchContent_MakeAvailable(SDL3_TTF)
            if (DYNAMIC_LINKING)
                install(TARGETS SDL3_ttf-shared DESTINATION ${CMAKE_LIBRARY_OUTPUT_DIRECTORY} )
            endif ()
        else()
            message("Using system SDL3_ttf")
        endif()
    endif()
    if (NOT (SDL3_ttf_FOUND OR TARGET SDL3_ttf::SDL3_ttf OR TARGET SDL3_ttf::SDL3_ttf-static))
        message(FATAL_ERROR
                "This project requires SDL3_ttf to be installed. \
                Please install the SDL3_ttf development libraries. \
                See CMake compiling guide for details and more info.")
    endif ()

    message(STATUS "Searching for SDL3_image library --")
    if (NOT CMAKE_FIND_PACKAGE_PREFER_CONFIG)
        if (NOT BUILD_SDL3)
            set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)
            # ^^ Use sdl3_image-config.cmake provided by the system or VCPKG
            find_package(SDL3_image CONFIG)
            set(CMAKE_FIND_PACKAGE_PREFER_CONFIG OFF)
        endif ()
        if(NOT SDL3_image_FOUND)
            message("Installing SDL3_image from Source")
            set(SDLIMAGE_JXL ON)
            set(SDLIMAGE_PNG_LIBPNG ON)
            # Install via FetchContent
            FetchContent_Declare(
                    SDL3_IMAGE
                    URL https://github.com/libsdl-org/SDL_image/archive/refs/tags/release-3.4.4.tar.gz
                    EXCLUDE_FROM_ALL
            )
            FetchContent_MakeAvailable(SDL3_IMAGE)
            if (DYNAMIC_LINKING)
                install(TARGETS SDL3_image-shared DESTINATION ${CMAKE_LIBRARY_OUTPUT_DIRECTORY} )
            endif ()
        else()
            message("Using system SDL3_image")
        endif()
    endif()
    if (NOT (SDL3_image_FOUND OR TARGET SDL3_image::SDL3_image OR TARGET SDL3_image::SDL3_image-static))
        message(FATAL_ERROR
                "This project requires SDL3_image to be installed. \
                 Please install the SDL3_image development libraries. \
                 See doc/COMPILING/COMPILING-CMAKE.md for details and more info.")
    endif ()
if (SOUND)
    # Sound requires SDL_mixer library
    message(STATUS "Searching for SDL3_mixer library --")
    if (NOT CMAKE_FIND_PACKAGE_PREFER_CONFIG)
        if (NOT BUILD_SDL3)
            set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)
            # ^^ Use sdl3_mixer-config.cmake provided by the system or VCPKG
            find_package(SDL3_mixer CONFIG)
            set(CMAKE_FIND_PACKAGE_PREFER_CONFIG OFF)
        endif ()
        if(NOT SDL3_mixer_FOUND)
            message("Installing SDL3_mixer from Source")
            # Install via FetchContent
            FetchContent_Declare(
                    SDL3_MIXER
                    URL https://github.com/libsdl-org/SDL_mixer/archive/refs/tags/release-3.2.0.tar.gz
                    EXCLUDE_FROM_ALL
            )
            FetchContent_MakeAvailable(SDL3_MIXER )
            if (DYNAMIC_LINKING)
                install(TARGETS SDL3_mixer-shared DESTINATION ${CMAKE_LIBRARY_OUTPUT_DIRECTORY} )
            endif ()
        else()
            message("Using system SDL3_mixer")
        endif()
    endif()
    if (NOT (SDL3_mixer_FOUND OR TARGET SDL3_mixer::SDL3_mixer OR TARGET SDL3_mixer::SDL3_mixer-static))
        message(FATAL_ERROR
                "You need the SDL3_mixer development library \
                    to be able to compile with sound enabled. \
                    See CMake compiling guide for details and more info.")
    endif ()
endif ()

# --- SDL_shadercross ---
# Runtime HLSL → SPIR-V / DXIL / MSL translation for the SDL_GPU lighting
# pipeline (phase 2c of the lighting rework). VENDORED brings in DXC and
# SPIRV-Cross as sub-projects so we don't need a system Vulkan SDK or a
# separately installed dxcompiler.dll. We always build the shared variant —
# the resulting runtime library + DXC dependencies (dxcompiler.dll / dxil.dll
# on Win11, libSDL3_shadercross.dylib on macOS, libSDL3_shadercross.so on
# Linux) are copied next to the executable below.
#
# Target platforms verified:
#   * macOS arm64 (M-series) — Metal backend, HLSL → MSL via shadercross.
#   * Windows 11 + NVIDIA RTX 4090 — D3D12 backend, HLSL → DXIL via DXC.
#                                    Also Vulkan backend, HLSL → SPIR-V.
#   * Linux — Vulkan backend, HLSL → SPIR-V.
message(STATUS "Searching for SDL3_shadercross library --")
if (NOT TARGET SDL3_shadercross::SDL3_shadercross)
    if (NOT BUILD_SDL3)
        set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)
        find_package(SDL3_shadercross CONFIG QUIET)
        set(CMAKE_FIND_PACKAGE_PREFER_CONFIG OFF)
    endif ()
    if (NOT TARGET SDL3_shadercross::SDL3_shadercross)
        message("Installing SDL3_shadercross from Source")
        # Pick SHARED vs STATIC based on which SDL3 target the caller has
        # exposed. Shadercross's CMakeLists requires the matching SDL3
        # component:
        #   * SHARED → `find_package(SDL3 REQUIRED COMPONENTS SDL3-shared)`
        #   * STATIC → `find_package(SDL3 REQUIRED COMPONENTS Headers)`
        # On vcpkg `x64-windows-static`, SDL3 ships as static-only — the
        # SHARED branch then errors out with `SDL3_FOUND to FALSE`. We
        # cannot fix that by *also* fetching SDL3-shared, because the exe
        # would end up with two SDL3 instances (static linked into the exe,
        # shared loaded by shadercross.dll) and SDL_GPU device state would
        # split between them. Build shadercross-static instead — it links
        # against `SDL3::Headers` only and resolves the actual SDL3 symbols
        # through the exe at link time. DXC sidecar DLLs (dxcompiler.dll /
        # dxil.dll) still ship via the POST_BUILD copy step further down.
        if (TARGET SDL3::SDL3-shared)
            set(_CATA_SHADERCROSS_SHARED ON)
            set(_CATA_SHADERCROSS_STATIC OFF)
        else ()
            message(STATUS "SDL3-shared target missing (vcpkg static triplet?) — building SDL3_shadercross as static")
            set(_CATA_SHADERCROSS_SHARED OFF)
            set(_CATA_SHADERCROSS_STATIC ON)
        endif ()
        # Pin to a known-good main SHA (libsdl-org/SDL_shadercross has NO tagged
        # releases — SHA pinning is the only option). Bump deliberately.
        # 2026-06-17: bumped 6b06e55c → 9a461644 to pick up the 2026-05-30
        # toolchain bumps (DirectXShaderCompiler → 2026-02 release, vkd3d 2.0,
        # SPIRV vulkan-sdk-1.4.350). The old DXC generated DXIL that the Win11
        # D3D12 driver rejected at pipeline creation (E_INVALIDARG) for the
        # radiance-cascade lighting shaders — see src/lighting/CLAUDE.md. Paired
        # with SDL release-3.4.10 (this shadercross was developed against it).
        set(SDLSHADERCROSS_DXC          ON  CACHE BOOL "" FORCE)
        set(SDLSHADERCROSS_VENDORED     ON  CACHE BOOL "" FORCE)
        set(SDLSHADERCROSS_SHARED       ${_CATA_SHADERCROSS_SHARED} CACHE BOOL "" FORCE)
        set(SDLSHADERCROSS_STATIC       ${_CATA_SHADERCROSS_STATIC} CACHE BOOL "" FORCE)
        set(SDLSHADERCROSS_CLI          OFF CACHE BOOL "" FORCE)
        set(SDLSHADERCROSS_INSTALL      OFF CACHE BOOL "" FORCE)
        # Vendored SPIRV-Tools defaults `SPIRV_WERROR=ON` and additionally
        # hardcodes `/WX` into its MSVC base warning flags. MSVC 14.50 emits
        # new warnings the SPIRV-Tools tree does not suppress
        # (e.g. set_spec_constant_default_value_pass.cpp), which with `/WX`
        # become fatal errors — the build dies around 800/1785. Disable the
        # cache toggle and post-process the subtree below to also strip the
        # hardcoded `/WX`. Same treatment covers vendored DXC, SPIRV-Cross.
        set(SPIRV_WERROR                OFF CACHE BOOL "" FORCE)
        set(SPIRV_WARN_EVERYTHING       OFF CACHE BOOL "" FORCE)
        set(SPIRV_SKIP_TESTS            ON  CACHE BOOL "" FORCE)
        set(SPIRV_SKIP_EXECUTABLES      ON  CACHE BOOL "" FORCE)
        # Save + scrub CATA_WARNINGS while the vendored DXC / SPIRV-Tools /
        # SPIRV-Cross trees configure: those subprojects build with
        # `-Werror -Wold-style-cast -Wshadow -Wundef -Wconversion …` of
        # their own, and they trip immediately when our project-level
        # `-Wold-style-cast`, `-Wmissing-noreturn`, `-Wsuggest-override`,
        # etc. leak in through CMAKE_CXX_FLAGS. Restore right after.
        set(_CATA_SAVED_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        set(_CATA_SAVED_C_FLAGS   "${CMAKE_C_FLAGS}")
        string(REPLACE "${CATA_WARNINGS}"    "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        string(REPLACE "${CATA_OTHER_FLAGS}" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        # Force vendored deps (spirv-cross, SPIRV-Tools, DXC) to use /MDd like the
        # rest of the project. Without this they default to /MTd and the linker dies.
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "" FORCE)
        # spirv-cross calls project() which resets CMAKE_MSVC_RUNTIME_LIBRARY;
        # use CMAKE_PROJECT_<NAME>_INCLUDE to re-set after the project() call.
        set(CMAKE_PROJECT_SPIRV-CROSS_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules/FixSpirvCrossRuntime.cmake")
        FetchContent_Declare(
            SDL3_SHADERCROSS
            GIT_REPOSITORY https://github.com/libsdl-org/SDL_shadercross.git
            GIT_TAG        9a461644308366b50f0c364069d762be8e3b3443
            EXCLUDE_FROM_ALL
        )
        FetchContent_MakeAvailable(SDL3_SHADERCROSS)
        set(CMAKE_CXX_FLAGS "${_CATA_SAVED_CXX_FLAGS}")
        set(CMAKE_C_FLAGS   "${_CATA_SAVED_C_FLAGS}")
        # Recursively strip `/WX` (and `-Werror`) from every target the
        # shadercross subtree just created — SPIRV-Tools hardcodes `/WX`
        # in its target COMPILE_OPTIONS bypassing both SPIRV_WERROR and
        # any flags scrub. Append `/WX-` so MSVC's last-flag-wins picks
        # warnings-as-warnings even on targets we cannot enumerate by name.
        function(_cata_strip_werror _dir)
            get_directory_property(_targets DIRECTORY "${_dir}" BUILDSYSTEM_TARGETS)
            foreach(_t IN LISTS _targets)
                get_target_property(_type ${_t} TYPE)
                if(_type STREQUAL "INTERFACE_LIBRARY" OR _type STREQUAL "UTILITY")
                    continue()
                endif()
                get_target_property(_opts ${_t} COMPILE_OPTIONS)
                if(_opts)
                    list(REMOVE_ITEM _opts "/WX" "-WX" "-Werror")
                    set_target_properties(${_t} PROPERTIES COMPILE_OPTIONS "${_opts}")
                endif()
                if(MSVC)
                    target_compile_options(${_t} PRIVATE /WX-)
                else()
                    target_compile_options(${_t} PRIVATE -Wno-error)
                endif()
            endforeach()
            get_directory_property(_subs DIRECTORY "${_dir}" SUBDIRECTORIES)
            foreach(_s IN LISTS _subs)
                _cata_strip_werror("${_s}")
            endforeach()
        endfunction()
        if(DEFINED sdl3_shadercross_SOURCE_DIR)
            _cata_strip_werror("${sdl3_shadercross_SOURCE_DIR}")
        endif()
    else ()
        message("Using system SDL3_shadercross")
    endif ()
endif ()
if (NOT TARGET SDL3_shadercross::SDL3_shadercross)
    message(FATAL_ERROR
            "This project requires SDL3_shadercross. \
             FetchContent failed and no system package was found. \
             See doc/COMPILING/COMPILING-CMAKE.md.")
endif ()

if (BACKTRACE)
    add_definitions(-DBACKTRACE)
    if (LIBBACKTRACE AND NOT MSVC)
        message(STATUS "Integrating libbacktrace build using ExternalProject")

        set(LIBBACKTRACE_GIT_REPO https://github.com/ianlancetaylor/libbacktrace.git)
        set(LIBBACKTRACE_GIT_TAG master)

        set(LIBBACKTRACE_INSTALL_DIR "${CMAKE_BINARY_DIR}/libbacktrace/install")
        SET(LIBBACKTRACE_LIBRARY "${LIBBACKTRACE_INSTALL_DIR}/lib/libbacktrace.a")

        include(ProcessorCount)
        ProcessorCount(NCPU)
        if(NCPU EQUAL 0)
            set(NCPU 1)
        endif()

        # Set up ccache for libbacktrace if available
        if (CCACHE_FOUND AND CATA_CCACHE AND CMAKE_C_COMPILER_LAUNCHER)
            # Join compiler launcher and compiler into a single string for configure
            set(LIBBACKTRACE_CC_LIST "${CMAKE_C_COMPILER_LAUNCHER}" "${CMAKE_C_COMPILER}")
            set(LIBBACKTRACE_CXX_LIST "${CMAKE_CXX_COMPILER_LAUNCHER}" "${CMAKE_CXX_COMPILER}")
            list(JOIN LIBBACKTRACE_CC_LIST " " LIBBACKTRACE_CC)
            list(JOIN LIBBACKTRACE_CXX_LIST " " LIBBACKTRACE_CXX)
        else()
            set(LIBBACKTRACE_CC "${CMAKE_C_COMPILER}")
            set(LIBBACKTRACE_CXX "${CMAKE_CXX_COMPILER}")
        endif()

        ExternalProject_Add(
            libbacktrace_external
            GIT_REPOSITORY ${LIBBACKTRACE_GIT_REPO}
            GIT_TAG ${LIBBACKTRACE_GIT_TAG}
            GIT_SHALLOW TRUE

            SOURCE_DIR "${CMAKE_BINARY_DIR}/libbacktrace/src"
            BINARY_DIR "${CMAKE_BINARY_DIR}/libbacktrace/build"

            CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
                CC=${LIBBACKTRACE_CC}
                CXX=${LIBBACKTRACE_CXX}
                <SOURCE_DIR>/configure
                --prefix=${LIBBACKTRACE_INSTALL_DIR}

            BUILD_COMMAND make -j${NCPU}

            INSTALL_COMMAND make install

            INSTALL_DIR ${LIBBACKTRACE_INSTALL_DIR}
            BUILD_BYPRODUCTS ${LIBBACKTRACE_LIBRARY}

            UPDATE_DISCONNECTED 1
        )

        add_library(backtrace SHARED IMPORTED)
        set_target_properties(backtrace
            PROPERTIES IMPORTED_LOCATION ${LIBBACKTRACE_LIBRARY}
        )
        # Hack to make it work, otherwise INTERFACE_INCLUDE_DIRECTORIES will not be propagated
        file(MAKE_DIRECTORY "${LIBBACKTRACE_INSTALL_DIR}/include")
        target_include_directories(backtrace INTERFACE
            "${LIBBACKTRACE_INSTALL_DIR}/include"
        )
        add_dependencies(backtrace libbacktrace_external)
    endif ()
    if (LIBBACKTRACE)
        add_definitions(-DLIBBACKTRACE)
    endif ()
endif ()

if (NOT (LANGUAGES STREQUAL "none") AND "${GETTEXT_MSGFMT_BINARY}" STREQUAL "")
    if(MSVC)
        list(APPEND Gettext_ROOT C:\\msys64\\usr)
        list(APPEND Gettext_ROOT C:\\Program\ Files\\Git\\usr)
    endif(MSVC)
    find_package(Gettext)
endif ()
if (NOT GETTEXT_MSGFMT_EXECUTABLE )
    set(GETTEXT_MSGFMT_EXECUTABLE "${GETTEXT_MSGFMT_BINARY}")
endif()

# Ok. Now create build and install recipes
add_subdirectory(lang)

if (USE_HOME_DIR)
    add_definitions(-DUSE_HOME_DIR)
endif ()

if (USE_XDG_DIR)
    add_definitions(-DUSE_XDG_DIR)
endif ()

find_program(CCACHE_FOUND ccache)
if (CCACHE_FOUND AND CATA_CCACHE)
    # Configure ccache for git worktrees
    # Set base_dir to make paths relative, enabling cache sharing across worktrees
    # see https://ccache.dev/manual/latest.html#_configuration_options
    set(CATA_CCACHE_CMD
        "${CMAKE_COMMAND}" -E env
        "CCACHE_BASEDIR=${CMAKE_SOURCE_DIR}"
        "CCACHE_NOHASHDIR=true"
        "${CCACHE_FOUND}"
    )

    set(CMAKE_C_COMPILER_LAUNCHER   ${CATA_CCACHE_CMD})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CATA_CCACHE_CMD})
    set(CMAKE_C_LINKER_LAUNCHER     ${CATA_CCACHE_CMD})
    set(CMAKE_CXX_LINKER_LAUNCHER   ${CATA_CCACHE_CMD})
endif ()

# --- RmlUi (player-facing menu UI layer — spike) ---
# Unlike ImGui (ships no CMake), RmlUi ships its own CMake, so we just
# FetchContent_MakeAvailable it and link the `rmlui` interface target (which
# aggregates rmlui_core + rmlui_debugger). Pinned to release 6.2 so Mac/Metal
# and Win11/D3D12 build identical code; bump deliberately + re-run D3D12 smoke.
#
# Font engine = freetype (RmlUi default). RmlUi does NOT fetch FreeType — it
# calls find_package(Freetype REQUIRED) itself. On the mac dev machine that
# resolves via brew (/opt/homebrew/opt/freetype); Win11 provisioning is a
# Phase-5 concern, not gold-plated here.
#
# Static for the spike: RmlUi obeys the global BUILD_SHARED_LIBS (default ON);
# force OFF here and restore the prior value, so the throwaway spike links
# statically and needs no runtime-DLL/rpath wiring.
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
set(_cata_saved_build_shared "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF)
FetchContent_Declare(
    RmlUi
    GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
    GIT_TAG 6.2
    GIT_PROGRESS TRUE
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(RmlUi)
set(BUILD_SHARED_LIBS "${_cata_saved_build_shared}")
unset(_cata_saved_build_shared)
# Vendored third-party: silence warnings so the fork's -Werror/​/WX can't trip.
foreach (_rml_tgt rmlui_core rmlui_debugger)
    if (TARGET ${_rml_tgt})
        if (MSVC)
            target_compile_options(${_rml_tgt} PRIVATE /w)
        else ()
            target_compile_options(${_rml_tgt} PRIVATE -w)
        endif ()
    endif ()
endforeach ()

add_subdirectory(src)
add_subdirectory(data)

if (UNIX AND DYNAMIC_LINKING)
    set_target_properties(
            cataclysm-bn-tiles
            PROPERTIES
            INSTALL_RPATH "$<IF:$<BOOL:${APPLE}>,@executable_path/lib:@executable_path/lib64,\$ORIGIN/lib:\$ORIGIN/lib64>"
    )
endif()

if(TESTS)
    add_subdirectory(tests)
endif()

if (JSON_FORMAT)
    add_subdirectory(tools/format)
endif()
# Mac-side D3D12 shader reflection gate (EXCLUDE_FROM_ALL — built on demand only).
if (TARGET SDL3_shadercross::SDL3_shadercross)
    add_subdirectory(tools/shader_check)
endif()
if (CATA_CLANG_TIDY_PLUGIN)
    add_subdirectory(tools/clang-tidy-plugin)
endif()

if (RELEASE)
    if (USE_PREFIX_DATA_DIR)
        install(DIRECTORY doc TYPE DOC)
        install(FILES
            ${CMAKE_SOURCE_DIR}/README.md
            ${CMAKE_SOURCE_DIR}/LICENSE.txt
            ${CMAKE_SOURCE_DIR}/LICENSE-OFL-Terminus-Font.txt
            ${CMAKE_SOURCE_DIR}/VERSION.txt
            TYPE DOC)
    else()
        install(DIRECTORY doc DESTINATION .)
        install(FILES
            ${CMAKE_SOURCE_DIR}/README.md
            ${CMAKE_SOURCE_DIR}/LICENSE.txt
            ${CMAKE_SOURCE_DIR}/LICENSE-OFL-Terminus-Font.txt
            ${CMAKE_SOURCE_DIR}/VERSION.txt
            DESTINATION .)
    endif()
    if (USE_PREFIX_DATA_DIR)
        install(DIRECTORY ${CMAKE_SOURCE_DIR}/gfx
            TYPE DATA)
    else()
        install(DIRECTORY ${CMAKE_SOURCE_DIR}/gfx
            DESTINATION .)
    endif()
    # Install cataclysm-launcher for portable Linux builds
    if (NOT USE_PREFIX_DATA_DIR AND UNIX AND NOT APPLE)
        install(PROGRAMS ${CMAKE_SOURCE_DIR}/cataclysm-launcher
            DESTINATION .)
    endif()
    if ( NOT USE_XDG_DIR AND NOT USE_HOME_DIR )
        install(DIRECTORY ${CMAKE_SOURCE_DIR}/mods
            DESTINATION .)
    endif()
endif()

if (APPLE AND RELEASE)
    set(CATA_OSX_GAME_TARGET cataclysm-bn-tiles)

    set(CATA_OSX_APP_DIR "${CMAKE_SOURCE_DIR}/Cataclysm.app")
    set(CATA_OSX_CONTENTS_DIR "${CATA_OSX_APP_DIR}/Contents")
    set(CATA_OSX_RESOURCES_DIR "${CATA_OSX_CONTENTS_DIR}/Resources")
    set(CATA_OSX_DATA_DIR "${CATA_OSX_RESOURCES_DIR}/data")
    set(CATA_OSX_FRAMEWORKS_DIR "" CACHE PATH "Directory containing macOS SDL3 frameworks for app packaging.")
    if (NOT CATA_OSX_FRAMEWORKS_DIR)
        if (EXISTS "$ENV{HOME}/Library/Frameworks/SDL3.framework")
            set(CATA_OSX_FRAMEWORKS_DIR "$ENV{HOME}/Library/Frameworks")
        elseif (EXISTS "/Library/Frameworks/SDL3.framework")
            set(CATA_OSX_FRAMEWORKS_DIR "/Library/Frameworks")
        endif()
    endif()

    set(CATA_OSX_COPY_RESOURCE_COMMANDS)
    list(APPEND CATA_OSX_COPY_RESOURCE_COMMANDS
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/gfx" "${CATA_OSX_RESOURCES_DIR}/gfx")
    if (SOUND)
        list(APPEND CATA_OSX_COPY_RESOURCE_COMMANDS
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data/sound" "${CATA_OSX_DATA_DIR}/sound")
    endif()

    set(CATA_OSX_COPY_FRAMEWORK_COMMANDS)
    if (CATA_OSX_FRAMEWORKS_DIR)
        set(CATA_OSX_FRAMEWORK_NAMES SDL3 SDL3_image SDL3_ttf)
        if (SOUND)
            list(APPEND CATA_OSX_FRAMEWORK_NAMES SDL3_mixer)
        endif()
        foreach (CATA_OSX_FRAMEWORK_NAME ${CATA_OSX_FRAMEWORK_NAMES})
            list(APPEND CATA_OSX_COPY_FRAMEWORK_COMMANDS
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CATA_OSX_FRAMEWORKS_DIR}/${CATA_OSX_FRAMEWORK_NAME}.framework"
                    "${CATA_OSX_RESOURCES_DIR}/${CATA_OSX_FRAMEWORK_NAME}.framework")
        endforeach()
    endif()

    add_custom_target(osx-app
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${CATA_OSX_APP_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CATA_OSX_CONTENTS_DIR}/MacOS"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CATA_OSX_RESOURCES_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CATA_OSX_DATA_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/build-data/osx/Info.plist" "${CATA_OSX_CONTENTS_DIR}/Info.plist"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/build-data/osx/Cataclysm.sh" "${CATA_OSX_CONTENTS_DIR}/MacOS/Cataclysm.sh"
        COMMAND chmod +x "${CATA_OSX_CONTENTS_DIR}/MacOS/Cataclysm.sh"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${CATA_OSX_GAME_TARGET}>" "${CATA_OSX_RESOURCES_DIR}/$<TARGET_FILE_NAME:${CATA_OSX_GAME_TARGET}>"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/build-data/osx/AppIcon.icns" "${CATA_OSX_RESOURCES_DIR}/AppIcon.icns"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data/font" "${CATA_OSX_DATA_DIR}/font"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data/json" "${CATA_OSX_DATA_DIR}/json"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data/mods" "${CATA_OSX_DATA_DIR}/mods"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data/names" "${CATA_OSX_DATA_DIR}/names"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data/raw" "${CATA_OSX_DATA_DIR}/raw"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data/motd" "${CATA_OSX_DATA_DIR}/motd"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data/credits" "${CATA_OSX_DATA_DIR}/credits"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data/title" "${CATA_OSX_DATA_DIR}/title"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data/help" "${CATA_OSX_DATA_DIR}/help"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/data/lua" "${CATA_OSX_DATA_DIR}/lua"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/lang/mo" "${CATA_OSX_RESOURCES_DIR}/lang/mo"
        ${CATA_OSX_COPY_RESOURCE_COMMANDS}
        ${CATA_OSX_COPY_FRAMEWORK_COMMANDS}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS ${CATA_OSX_GAME_TARGET} translations)

    if (GIT_VERSION)
        set(CATA_OSX_DMG_VERSION "${GIT_VERSION}")
    else()
        set(CATA_OSX_DMG_VERSION unknown)
    endif()
    add_custom_target(dmgdist
        COMMAND plutil -convert binary1 "${CATA_OSX_APP_DIR}/Contents/Info.plist"
        COMMAND dmgbuild -s build-data/osx/dmgsettings.py "Cataclysm BN" "CataclysmBN-${CATA_OSX_DMG_VERSION}.dmg"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS osx-app)
endif()

configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        IMMEDIATE @ONLY)

add_custom_target(uninstall
        "${CMAKE_COMMAND}"
        -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")

if (USE_UNITY_BUILD)
    set_property(GLOBAL PROPERTY UNITY_BUILD ON)
endif ()
