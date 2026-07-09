set(MAIN_CPP ${CMAKE_SOURCE_DIR}/src/main.cpp)
set(MESSAGES_CPP ${CMAKE_SOURCE_DIR}/src/messages.cpp)
set(RESOURCE_RC ${CMAKE_SOURCE_DIR}/src/resource.rc)

file(GLOB_RECURSE CATACLYSM_BN_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
list(FILTER CATACLYSM_BN_SOURCES EXCLUDE REGEX "/src/(lua|sol|third-party)/")

list(REMOVE_ITEM CATACLYSM_BN_SOURCES ${MAIN_CPP} ${MESSAGES_CPP})

# Lua binding translation units are template-heavy and primarily run at startup;
# compile them with no optimization to reduce build times.
file(GLOB CATALUA_BINDINGS_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/catalua_bindings*.cpp)
if (MSVC)
    set(CATALUA_BINDINGS_COMPILE_OPTIONS /Od /bigobj)
else ()
    set(CATALUA_BINDINGS_COMPILE_OPTIONS -O0 -g0)
endif ()
foreach (catalua_src IN LISTS CATALUA_BINDINGS_SOURCES)
    set_source_files_properties(${catalua_src} PROPERTIES
        COMPILE_OPTIONS "${CATALUA_BINDINGS_COMPILE_OPTIONS}"
        # catalua_bindings*.cpp use -O0 which conflicts with the PCH built at -O2;
        # clang rejects including a Release-optimized PCH in a -O0 TU (__OPTIMIZE__
        # mismatch).  Skip PCH for these files — they're template-heavy anyway so
        # the PCH savings here are minimal.
        SKIP_PRECOMPILE_HEADERS ON
    )
endforeach ()
# Large template-heavy files that need /bigobj on MSVC
if (MSVC)
    set(BIGOBJ_FILES
        ${CMAKE_SOURCE_DIR}/src/character.cpp
        ${CMAKE_SOURCE_DIR}/src/game.cpp
        ${CMAKE_SOURCE_DIR}/src/iexamine.cpp
        ${CMAKE_SOURCE_DIR}/src/item.cpp
        ${CMAKE_SOURCE_DIR}/src/iuse_actor.cpp
        ${CMAKE_SOURCE_DIR}/src/mapgen.cpp
        ${CMAKE_SOURCE_DIR}/src/monster.cpp
        ${CMAKE_SOURCE_DIR}/src/overmap.cpp
        ${CMAKE_SOURCE_DIR}/src/overmapbuffer.cpp
    )
    foreach(bigobj_file IN LISTS BIGOBJ_FILES)
        set_source_files_properties(${bigobj_file} PROPERTIES COMPILE_OPTIONS "/bigobj")
    endforeach()
endif()

file(GLOB_RECURSE CATACLYSM_BN_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.h)
list(FILTER CATACLYSM_BN_HEADERS EXCLUDE REGEX "/src/(lua|sol|third-party)/")

add_subdirectory(lua)
if (UNIX)
    target_compile_definitions(liblua PUBLIC LUA_USE_LINUX)
elseif (APPLE)
    target_compile_definitions(liblua PUBLIC LUA_USE_MACOSX)
elseif (WIN32)
    target_compile_definitions(liblua PUBLIC LUA_USE_WINDOWS)
endif()

add_subdirectory(sol)

# Get GIT version strings
add_custom_target(
    get_version
    DEPENDS ${CMAKE_BINARY_DIR}/generated/version.h
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/generated/version.h
    COMMAND ${CMAKE_COMMAND}
            -D SRC=${CMAKE_SOURCE_DIR}/src/version.h.in
            -D DST=${CMAKE_BINARY_DIR}/generated/version.h
            -D GIT_EXECUTABLE=${GIT_EXECUTABLE}
            -P ${CMAKE_SOURCE_DIR}/src/version.cmake
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Function to setup common properties for both tiles and curses targets
function(setup_common_target_properties target_name)
    # Add version dependency
    add_dependencies(${target_name} get_version)

    # Add common libraries
    target_link_libraries(${target_name} PUBLIC ZLIB::ZLIB)
    target_link_libraries(${target_name} PUBLIC SQLite3::SQLite3)

    # Setup threading
    if (CMAKE_USE_PTHREADS_INIT)
        target_compile_options(${target_name} PUBLIC "-pthread")
    endif ()

    if (CMAKE_THREAD_LIBS_INIT)
        target_link_libraries(${target_name} PUBLIC ${CMAKE_THREAD_LIBS_INIT})
    endif ()

    target_compile_definitions(${target_name} PUBLIC LUA)
    if (UNIX)
        target_compile_definitions(${target_name} PUBLIC LUA_USE_LINUX)
    elseif (APPLE)
        target_compile_definitions(${target_name} PUBLIC LUA_USE_MACOSX)
    endif()
    target_link_libraries(${target_name} PUBLIC libsol)

    # Windows-specific libraries
    if (WIN32)
        target_link_libraries(${target_name} PUBLIC gdi32.lib)
        target_link_libraries(${target_name} PUBLIC winmm.lib)
        target_link_libraries(${target_name} PUBLIC imm32.lib)
        target_link_libraries(${target_name} PUBLIC ole32.lib)
        target_link_libraries(${target_name} PUBLIC oleaut32.lib)
        target_link_libraries(${target_name} PUBLIC version.lib)

        if (BACKTRACE)
            target_link_libraries(${target_name} PUBLIC dbghelp.lib)
            if (LIBBACKTRACE)
                target_link_libraries(${target_name} PUBLIC backtrace)
            endif ()
        endif ()
    elseif (APPLE)
        target_link_libraries(${target_name} PUBLIC "-framework CoreFoundation")
    endif ()

    # Backtrace support
    if (LIBBACKTRACE)
        target_link_libraries(${target_name} PUBLIC backtrace)
    endif ()

    # Tracy profiling support
    if (USE_TRACY)
        target_link_libraries(${target_name} PUBLIC TracyClient)
        target_include_directories(${target_name} SYSTEM PUBLIC ${tracy_SOURCE_DIR}/public)
        target_compile_definitions(${target_name} PUBLIC USE_TRACY)
    endif ()

    target_precompile_headers(${target_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/pch/main-pch.hpp)
endfunction()

# Build tiles version (only supported configuration)
add_library(
        cataclysm-bn-tiles-common OBJECT
        ${CATACLYSM_BN_SOURCES}
        ${CATACLYSM_BN_HEADERS})
target_include_directories(cataclysm-bn-tiles-common INTERFACE ${CMAKE_SOURCE_DIR}/src)
# PRIVATE entry so sources nested under src/<subdir>/ (e.g. src/lighting/) can
# `#include "foo.h"` to reach top-level src/ headers. Without this only sources
# directly in src/ resolve those includes via the current-file-directory rule.
target_include_directories(cataclysm-bn-tiles-common PRIVATE ${CMAKE_SOURCE_DIR}/src)

if (WIN32)
    add_definitions(-DUSE_WINMAIN)
    add_executable(
            cataclysm-bn-tiles WIN32
            ${MAIN_CPP}
            ${MESSAGES_CPP}
            ${RESOURCE_RC})
else ()
    add_executable(cataclysm-bn-tiles
            ${MAIN_CPP}
            ${MESSAGES_CPP})
endif ()

setup_common_target_properties(cataclysm-bn-tiles-common)

if (DYNAMIC_ATLAS)
    target_compile_definitions(cataclysm-bn-tiles-common PUBLIC DYNAMIC_ATLAS)
endif()
target_link_libraries(cataclysm-bn-tiles PRIVATE cataclysm-bn-tiles-common)

# Windows specific tiles libraries
if (WIN32)
    target_link_libraries(cataclysm-bn-tiles-common PUBLIC setupapi.lib)
    if (SOUND)
        target_link_libraries(cataclysm-bn-tiles-common PUBLIC shlwapi.lib)
    endif()
endif()

if (NOT DYNAMIC_LINKING)
    target_link_libraries(cataclysm-bn-tiles-common PUBLIC
        SDL3::SDL3-static
        SDL3_image::SDL3_image-static
        SDL3_ttf::SDL3_ttf-static
    )
else()
    target_link_libraries(cataclysm-bn-tiles-common PUBLIC
        SDL3::SDL3
        SDL3_image::SDL3_image
        SDL3_ttf::SDL3_ttf
    )
endif ()

# SDL_shadercross: always shared. Phase 2c of the lighting rework uses it to
# translate HLSL → backend-native shader bytecode at runtime (Metal/Mac,
# DXIL/Win11, SPIR-V/Linux).
target_link_libraries(cataclysm-bn-tiles-common PUBLIC SDL3_shadercross::SDL3_shadercross)

# RmlUi UI layer. PUBLIC so the executable and
# cata_test both link it; inert at runtime until rmlui_layer init succeeds.
# Linked now with no caller yet so Phase 0 genuinely compiles RmlUi + resolves
# FreeType — declare-without-link would build green and prove nothing.
target_link_libraries(cataclysm-bn-tiles-common PUBLIC rmlui)

# Ship the runtime DLLs next to the executable so a fresh-checkout dev build
# on Windows 11 (NVIDIA RTX 4090 target) has the shadercross DLL + DXC
# sidecars (dxcompiler.dll / dxil.dll) resolvable at launch. Windows-only:
# $<TARGET_RUNTIME_DLLS> is empty on macOS/Linux (those use rpath baked into
# the executable instead), and an empty source list crashes
# `cmake -E copy_if_different`.
if (WIN32)
    add_custom_command(TARGET cataclysm-bn-tiles POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:cataclysm-bn-tiles>
                $<TARGET_FILE_DIR:cataclysm-bn-tiles>
        COMMAND_EXPAND_LISTS
        VERBATIM)

    # When SDL3_shadercross is built static (vcpkg x64-windows-static path)
    # its vendored shared deps don't appear in $<TARGET_RUNTIME_DLLS> because
    # they're PRIVATE inside the static lib and the link chain has nothing
    # to follow. Copy them explicitly so the exe finds dxcompiler.dll,
    # dxil.dll, and spirv-cross-c-shared.dll at launch. Install them into
    # the RUNTIME dir too so `RELEASE` packaging picks them up.
    set(_CATA_SHADERCROSS_RUNTIME_TARGETS dxcompiler dxildll spirv-cross-c-shared)
    foreach(_t IN LISTS _CATA_SHADERCROSS_RUNTIME_TARGETS)
        if(TARGET ${_t})
            get_target_property(_type ${_t} TYPE)
            if(_type STREQUAL "SHARED_LIBRARY" OR _type STREQUAL "MODULE_LIBRARY")
                add_custom_command(TARGET cataclysm-bn-tiles POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            $<TARGET_FILE:${_t}>
                            $<TARGET_FILE_DIR:cataclysm-bn-tiles>
                    VERBATIM)
                if (RELEASE)
                    install(FILES $<TARGET_FILE:${_t}> DESTINATION ${CMAKE_INSTALL_BINDIR})
                endif()
            endif()
        endif()
    endforeach()
endif ()

if (SOUND)
    if (NOT DYNAMIC_LINKING)
        target_link_libraries(cataclysm-bn-tiles-common PUBLIC
            SDL3_mixer::SDL3_mixer-static
        )
    else()
        target_link_libraries(cataclysm-bn-tiles-common PUBLIC
            SDL3_mixer::SDL3_mixer
        )
    endif()
    add_definitions(-DSDL_SOUND)
endif ()

if (RELEASE)
    install(TARGETS cataclysm-bn-tiles RUNTIME)
endif ()

set_target_properties(cataclysm-bn-tiles PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

if( LUA_DOCS_ON_BUILD AND DENO_BINARY )
    add_custom_command(
        TARGET cataclysm-bn-tiles POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E env --unset=LD_LIBRARY_PATH "CATA_EXE=$<TARGET_FILE:cataclysm-bn-tiles>" ${DENO_BINARY} task docs:gen
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating Lua reference/annotations (deno task docs:gen)"
        VERBATIM
    )
endif()
