// Mac-side D3D12 shader reflection gate.
//
// Compiles every lighting HLSL shader to SPIR-V *and* DXIL via the same
// SDL_shadercross the game uses at runtime, reflects the resource layout, and
// lints for the binding patterns that fail D3D12 pipeline creation. Needs NO
// GPU device — HLSL->SPIRV/DXIL compilation and reflection are pure-CPU, so the
// whole check runs on macOS where there is no D3D12 backend at all.
//
// Why this exists: the bugs that keep biting on Win11/D3D12 (E_INVALIDARG at
// SDL_CreateGPUGraphicsPipeline, see src/lighting/CLAUDE.md) are driven by
// shadercross's HLSL->DXIL codegen + reflection, both of which are
// host-OS-independent. So they can be caught here instead of three sessions
// later on a Windows box.
//
// Severity:
//   ERROR (exit 1) — HLSL->SPIRV fails, HLSL->DXIL fails (DXC rejected it → a
//                    real Win11 D3D12 driver will reject it too), or reflection
//                    fails (the game needs that reflection to build the shader).
//   WARN           — fragment shader with storage buffers but zero samplers.
//                    This is the *suspected* rc.frag E_INVALIDARG cause (D3D12
//                    root-sig assumes sampled->storage-texture->storage-buffer
//                    t-register order; a bare leading storage buffer mismatches
//                    it). Scoped to storage *buffers* on purpose — storage
//                    *textures* with no sampler are known to create fine
//                    (tonemap.frag). --strict promotes warnings to errors once
//                    you have confirmed the hypothesis on real hardware.
//
// Usage:
//   shader_reflect_check [--strict] [shader_dir]
// shader_dir defaults to the compiled-in CATA_SHADER_DIR (the source tree).

#include <SDL3/SDL.h>
#include <SDL3_shadercross/SDL_shadercross.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef CATA_SHADER_DIR
#define CATA_SHADER_DIR "data/shaders/lighting/src"
#endif

namespace fs = std::filesystem;

namespace
{

std::string read_file( const fs::path &p )
{
    std::ifstream f( p, std::ios::binary );
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Infer the shader stage from a "<name>.<stage>.hlsl" filename.
bool stage_from_name( const std::string &fn, SDL_ShaderCross_ShaderStage &out,
                      const char *&label )
{
    auto ends = [&]( const std::string & suf ) {
        return fn.size() >= suf.size() &&
               fn.compare( fn.size() - suf.size(), suf.size(), suf ) == 0;
    };
    if( ends( ".vert.hlsl" ) ) {
        out = SDL_SHADERCROSS_SHADERSTAGE_VERTEX;
        label = "vert";
        return true;
    }
    if( ends( ".frag.hlsl" ) ) {
        out = SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT;
        label = "frag";
        return true;
    }
    if( ends( ".comp.hlsl" ) ) {
        out = SDL_SHADERCROSS_SHADERSTAGE_COMPUTE;
        label = "comp";
        return true;
    }
    return false;
}

} // namespace

int main( int argc, char **argv )
{
    bool strict = false;
    std::string dir = CATA_SHADER_DIR;
    for( int i = 1; i < argc; ++i ) {
        const std::string a = argv[i];
        if( a == "--strict" ) {
            strict = true;
        } else {
            dir = a;
        }
    }

    if( !SDL_ShaderCross_Init() ) {
        std::fprintf( stderr, "SDL_ShaderCross_Init failed: %s\n", SDL_GetError() );
        return 2;
    }

    std::vector<fs::path> files;
    std::error_code ec;
    for( const auto &e : fs::directory_iterator( dir, ec ) ) {
        if( e.is_regular_file() && e.path().extension() == ".hlsl" ) {
            files.push_back( e.path() );
        }
    }
    if( ec ) {
        std::fprintf( stderr, "cannot read shader dir '%s': %s\n",
                      dir.c_str(), ec.message().c_str() );
        SDL_ShaderCross_Quit();
        return 2;
    }
    std::sort( files.begin(), files.end() );

    std::printf( "shader_reflect_check — %zu file(s) in %s%s\n",
                 files.size(), dir.c_str(), strict ? "  [strict]" : "" );
    std::printf( "%-26s %-5s %-6s %s\n", "shader", "stage", "DXIL", "reflection" );
    std::printf( "%s\n", std::string( 78, '-' ).c_str() );

    int errors = 0, warnings = 0, skipped = 0;
    std::vector<std::string> failed;

    for( const auto &p : files ) {
        const std::string fn = p.filename().string();
        SDL_ShaderCross_ShaderStage stage;
        const char *slabel = nullptr;
        if( !stage_from_name( fn, stage, slabel ) ) {
            std::printf( "%-26s  (skip: no .vert/.frag/.comp stage)\n", fn.c_str() );
            ++skipped;
            continue;
        }

        const std::string src = read_file( p );
        if( src.empty() ) {
            std::printf( "%-26s %-5s  ERROR empty/unreadable\n", fn.c_str(), slabel );
            ++errors;
            failed.push_back( fn );
            continue;
        }

        // Resolve `#include` (jfa_*.comp pull in jfa_shared.hlsl) relative to the
        // shader's own directory — mirrors the game's runtime include_dir so this
        // gate actually exercises the included files instead of erroring on them.
        const std::string inc_dir = p.parent_path().string();

        SDL_ShaderCross_HLSL_Info info{};
        info.source = src.c_str();
        info.entrypoint = "main";
        info.include_dir = inc_dir.c_str();
        info.defines = nullptr;
        info.shader_stage = stage;
        info.props = 0;

        // HLSL -> SPIRV (reflection source; mirrors the game's compile path).
        size_t spv_sz = 0;
        void *spv = SDL_ShaderCross_CompileSPIRVFromHLSL( &info, &spv_sz );
        if( !spv || spv_sz == 0 ) {
            std::printf( "%-26s %-5s  ERROR HLSL->SPIRV: %s\n",
                         fn.c_str(), slabel, SDL_GetError() );
            ++errors;
            failed.push_back( fn );
            continue;
        }

        // HLSL -> DXIL gate: the closest local proxy to "Win11/D3D12 accepts it".
        size_t dxil_sz = 0;
        void *dxil = SDL_ShaderCross_CompileDXILFromHLSL( &info, &dxil_sz );
        const bool dxil_ok = dxil && dxil_sz > 0;
        std::string dxil_err;
        if( !dxil_ok ) {
            dxil_err = SDL_GetError();
        }
        if( dxil ) {
            SDL_free( dxil );
        }

        // Reflect the resource layout the D3D12 root signature is built from.
        std::string refl;
        bool refl_ok = true;
        bool warn_no_sampler = false;
        if( stage == SDL_SHADERCROSS_SHADERSTAGE_COMPUTE ) {
            SDL_ShaderCross_ComputePipelineMetadata *m =
                SDL_ShaderCross_ReflectComputeSPIRV(
                    static_cast<const Uint8 *>( spv ), spv_sz, 0 );
            if( m ) {
                char buf[256];
                std::snprintf( buf, sizeof( buf ),
                               "smp=%u ro[tex=%u buf=%u] rw[tex=%u buf=%u] ub=%u thr=%ux%ux%u",
                               m->num_samplers, m->num_readonly_storage_textures,
                               m->num_readonly_storage_buffers, m->num_readwrite_storage_textures,
                               m->num_readwrite_storage_buffers, m->num_uniform_buffers,
                               m->threadcount_x, m->threadcount_y, m->threadcount_z );
                refl = buf;
                SDL_free( m );
            } else {
                refl_ok = false;
                refl = std::string( "reflect FAILED: " ) + SDL_GetError();
            }
        } else {
            SDL_ShaderCross_GraphicsShaderMetadata *m =
                SDL_ShaderCross_ReflectGraphicsSPIRV(
                    static_cast<const Uint8 *>( spv ), spv_sz, 0 );
            if( m ) {
                const Uint32 smp = m->resource_info.num_samplers;
                const Uint32 st = m->resource_info.num_storage_textures;
                const Uint32 sb = m->resource_info.num_storage_buffers;
                const Uint32 ub = m->resource_info.num_uniform_buffers;
                char buf[256];
                std::snprintf( buf, sizeof( buf ), "smp=%u storage[tex=%u buf=%u] ub=%u",
                               smp, st, sb, ub );
                refl = buf;
                if( stage == SDL_SHADERCROSS_SHADERSTAGE_FRAGMENT && sb > 0 && smp == 0 ) {
                    warn_no_sampler = true;
                }
                SDL_free( m );
            } else {
                refl_ok = false;
                refl = std::string( "reflect FAILED: " ) + SDL_GetError();
            }
        }

        SDL_free( spv );

        std::printf( "%-26s %-5s %-6s %s\n", fn.c_str(), slabel,
                     dxil_ok ? "ok" : "FAIL", refl.c_str() );

        bool is_err = false;
        if( !dxil_ok ) {
            std::printf( "   ERROR  HLSL->DXIL failed (DXC rejected → Win11/D3D12 will reject "
                         "pipeline creation): %s\n", dxil_err.c_str() );
            is_err = true;
        }
        if( !refl_ok ) {
            std::printf( "   ERROR  SPIR-V reflection failed — the game needs this to build the shader.\n" );
            is_err = true;
        }
        if( warn_no_sampler ) {
            std::printf( "   WARN   fragment storage buffer with 0 samplers — D3D12 root-sig assumes "
                         "sampled->storage t-order;\n"
                         "          a bare leading storage buffer is the suspected rc.frag "
                         "E_INVALIDARG. Add a sampled\n"
                         "          texture at t0/s0 (mirror sprite.frag) or confirm-then-suppress.%s\n",
                         strict ? "  [strict->error]" : "" );
            if( strict ) {
                is_err = true;
            } else {
                ++warnings;
            }
        }
        if( is_err ) {
            ++errors;
            failed.push_back( fn );
        }
    }

    std::printf( "%s\n", std::string( 78, '-' ).c_str() );
    const int checked = static_cast<int>( files.size() ) - skipped;
    std::printf( "done: %d checked, %d error(s), %d warning(s), %d skipped\n",
                 checked, errors, warnings, skipped );
    if( !failed.empty() ) {
        std::printf( "failed:" );
        for( const auto &n : failed ) {
            std::printf( " %s", n.c_str() );
        }
        std::printf( "\n" );
    }

    SDL_ShaderCross_Quit();
    return errors > 0 ? 1 : 0;
}
