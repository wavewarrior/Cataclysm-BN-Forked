# Mac-side D3D12 shader reflection gate. Compiles each lighting HLSL shader to
# SPIR-V + DXIL via the same SDL_shadercross the game uses, reflects the resource
# layout, and lints for the binding patterns that fail D3D12 pipeline creation
# (see src/lighting/CLAUDE.md). Runs entirely on macOS — needs NO GPU device.
#
# Separate EXCLUDE_FROM_ALL target so it never slows the normal game build:
#   cmake --build <build-dir> --target shader_reflect_check
#   <build-dir>/tools/shader_check/shader_reflect_check [--strict] [shader_dir]
add_executable( shader_reflect_check EXCLUDE_FROM_ALL shader_reflect_check.cpp )
target_compile_features( shader_reflect_check PRIVATE cxx_std_17 )

# Default shader dir = the source tree, so it runs with no args from anywhere.
target_compile_definitions( shader_reflect_check PRIVATE
    CATA_SHADER_DIR="${CMAKE_SOURCE_DIR}/data/shaders/lighting/src" )

# Pick whichever SDL3 target flavour the build exposed (shared on macOS,
# possibly static on vcpkg Windows triplets).
if( TARGET SDL3::SDL3-shared )
    set( _cata_sc_sdl3 SDL3::SDL3-shared )
elseif( TARGET SDL3::SDL3-static )
    set( _cata_sc_sdl3 SDL3::SDL3-static )
else()
    set( _cata_sc_sdl3 SDL3::SDL3 )
endif()
target_link_libraries( shader_reflect_check PRIVATE
    SDL3_shadercross::SDL3_shadercross ${_cata_sc_sdl3} )

# This is a dev tool, not shipped code — silence the project-wide warnings (and
# any -Werror / /WX) so it builds regardless of the strict game warning config.
if( MSVC )
    target_compile_options( shader_reflect_check PRIVATE /W3 /WX- )
else()
    target_compile_options( shader_reflect_check PRIVATE -w )
endif()

# CTest registration. SHADER_CHECK_STRICT (default OFF) promotes the no-sampler
# storage-buffer binding WARN to a hard failure. Left OFF for now because the
# known-pending shaders still trip it (rc.frag/rc_bounce are deleted by the GI
# compute migration; vol.frag is flagged pending Win11 confirmation). With it
# OFF the test still fails on the unambiguous errors (HLSL->SPIRV/DXIL/reflect
# failures) — the real safety net. Flip ON once the shaders are clean:
#   cmake -S . -B <build> -DSHADER_CHECK_STRICT=ON
option( SHADER_CHECK_STRICT "shader_reflect_check fails on binding warnings, not just hard errors" OFF )
if( TESTS )
    add_test( NAME shader_reflect_check
              COMMAND shader_reflect_check $<$<BOOL:${SHADER_CHECK_STRICT}>:--strict> )
endif()
