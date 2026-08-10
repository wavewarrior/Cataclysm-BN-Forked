#include "terrain_decals.h"

#include <algorithm>
#include <cstddef>
#include <random>

#include "debug.h"
#include "fstream_utils.h"
#include "game_constants.h" // SEEX / SEEY
#include "type_id.h"
#include "json.h"
#include "lighting/gpu_device.h"
#include "mapdata.h"        // ter_t, ter_id
#include "path_info.h"
#include "sdl_utils.h"
#include "sdl_wrappers.h"

#define dbg( x ) DebugLogFL( ( x ), DC::SDL )

namespace terrain_decals
{

namespace
{

/// Placement RNG — file-local so it never touches the game rng() stream.
/// Same discipline as splatmap_stamps.cpp: cosmetic, deterministic, co-op safe.
std::mt19937 decal_rng;

auto uniform( float lo, float hi ) -> float
{
    return std::uniform_real_distribution<float>( lo, hi )( decal_rng );
}

/// Seed from absolute submap coordinates — same formula family as splatmap.
auto seed_from_key( std::uint64_t key ) -> void
{
    // Decode rough coords from the packed key for varied seeding.
    const int64_t x = static_cast<int64_t>( ( key >> 40 ) & 0xFFFFFF ) - 0x800000;
    const int64_t y = static_cast<int64_t>( ( key >> 16 ) & 0xFFFFFF ) - 0x800000;
    const int64_t z = static_cast<int64_t>( key & 0xFF ) - 10;

    const std::size_t seed = std::hash<long long> {}(
                                 ( x * 73'856'093LL )
                                 ^ ( y * 19'349'663LL )
                                 ^ ( z * 83'492'791LL )
                                 ^ 4'231'765'123LL ); // salt distinct from splatmap
    decal_rng.seed( static_cast<std::mt19937::result_type>( seed ) );
}

} // namespace

manager::~manager() { shutdown(); }

// ---- Atlas load -----------------------------------------------------------

auto manager::load_atlas( gpu_device &dev ) -> bool
{
    const std::string dir = PATH_INFO::gfxdir() + "terrain_decals/";
    const std::string manifest = dir + "manifest.json";

    std::vector<std::string> grass_files;
    // The tile size the decal art was authored against. A 64x64 sprite drawn for
    // a 32 px tileset covers 2x2 tiles, and keeps covering 2x2 tiles at any zoom
    // or display scale.
    int art_tile_size = 32;
    const bool parsed = read_from_file_json( manifest, [&]( JsonIn & jsin ) {
        JsonObject jo = jsin.get_object();
        jo.allow_omitted_members();
        jo.read( "tile_size", art_tile_size );
        if( jo.has_object( "groups" ) ) {
            JsonObject groups = jo.get_object( "groups" );
            groups.allow_omitted_members();
            groups.read( "grass", grass_files );
        }
    }, /*optional=*/true );

    if( !parsed ) {
        dbg( DL::Warn ) << "terrain_decals: missing " << manifest
                        << " (cosmetic feature disabled)";
        return false;
    }
    if( grass_files.empty() ) {
        dbg( DL::Warn ) << "terrain_decals: no grass decals listed";
        return false;
    }

    // Load each sprite, compute total atlas dimensions.
    struct loaded_sprite {
        SDL_Surface_Ptr surface;
        int w = 0, h = 0;
    };
    std::vector<loaded_sprite> sprites;
    sprites.reserve( grass_files.size() );

    int max_h = 0;
    int total_w = 0;
    for( const auto &name : grass_files ) {
        SDL_Surface_Ptr surf = load_image( ( dir + name ).c_str() );
        if( !surf ) {
            dbg( DL::Warn ) << "terrain_decals: failed to load " << name;
            continue;
        }
        const int w = surf->w;
        const int h = surf->h;
        if( w <= 0 || h <= 0 ) {
            dbg( DL::Warn ) << "terrain_decals: zero-size sprite " << name;
            continue;
        }
        sprites.push_back( { std::move( surf ), w, h } );
        max_h = std::max( max_h, h );
        total_w += w;
    }

    if( sprites.empty() ) {
        dbg( DL::Warn ) << "terrain_decals: no valid sprites loaded";
        return false;
    }

    // Power-of-two atlas for safety (some drivers prefer it for samplers).
    const int atlas_w = []( int v ) { int r = 1; while( r < v ) r <<= 1; return r; }
    ( std::max( total_w, 256 ) );
    const int atlas_h = []( int v ) { int r = 1; while( r < v ) r <<= 1; return r; }
    ( std::max( max_h, 64 ) );

    SDL_Surface_Ptr sheet = create_surface_32( atlas_w, atlas_h );
    if( !sheet ) {
        dbg( DL::Error ) << "terrain_decals: atlas surface alloc failed";
        return false;
    }
    // Transparent black fill.
    SDL_FillSurfaceRect( sheet.get(), nullptr,
                         SDL_MapRGBA( SDL_GetPixelFormatDetails( sheet->format ), nullptr,
                                      0, 0, 0, 0 ) );

    // Pack sprites left-to-right in a single row.
    int cx = 0;
    variants_.reserve( sprites.size() );
    for( auto &spr : sprites ) {
        SDL_Rect dst{ cx, 0, spr.w, spr.h };
        if( !SDL_BlitSurface( spr.surface.get(), nullptr, sheet.get(), &dst ) ) {
            dbg( DL::Warn ) << "terrain_decals: blit failed: " << SDL_GetError();
        }
        const float art_tile = static_cast<float>( std::max( art_tile_size, 1 ) );
        variants_.push_back( decal_variant_info{
            .u = static_cast<float>( cx ) / atlas_w,
            .v = 0.f,
            .uw = static_cast<float>( spr.w ) / atlas_w,
            .vh = static_cast<float>( spr.h ) / atlas_h,
            .w_tiles = static_cast<float>( spr.w ) / art_tile,
            .h_tiles = static_cast<float>( spr.h ) / art_tile,
        } );
        cx += spr.w;
    }

    // Upload to GPU.
    SDL_GPUTextureCreateInfo tci{};
    tci.type = SDL_GPU_TEXTURETYPE_2D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width = static_cast<std::uint32_t>( atlas_w );
    tci.height = static_cast<std::uint32_t>( atlas_h );
    tci.layer_count_or_depth = 1;
    tci.num_levels = 1;
    tci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    atlas_ = SDL_CreateGPUTexture( dev.raw(), &tci );
    if( !atlas_ ) {
        dbg( DL::Error ) << "terrain_decals: atlas texture: " << SDL_GetError();
        return false;
    }

    // Convert to RGBA32 if needed.
    SDL_Surface *src = sheet.get();
    SDL_Surface *converted = nullptr;
    if( src->format != SDL_PIXELFORMAT_RGBA32 ) {
        converted = SDL_ConvertSurface( src, SDL_PIXELFORMAT_RGBA32 );
        if( !converted ) {
            dbg( DL::Error ) << "terrain_decals: atlas convert: " << SDL_GetError();
            SDL_ReleaseGPUTexture( dev.raw(), atlas_ );
            atlas_ = nullptr;
            return false;
        }
        src = converted;
    }

    const std::uint32_t row_bytes = static_cast<std::uint32_t>( atlas_w ) * 4u;
    const std::uint32_t bytes = row_bytes * static_cast<std::uint32_t>( atlas_h );
    SDL_GPUTransferBufferCreateInfo tbi{};
    tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbi.size = bytes;
    SDL_GPUTransferBuffer *xfer = SDL_CreateGPUTransferBuffer( dev.raw(), &tbi );
    bool ok = false;
    if( xfer ) {
        if( void *mapped = SDL_MapGPUTransferBuffer( dev.raw(), xfer, false ) ) {
            auto *out = static_cast<std::uint8_t *>( mapped );
            const auto *in = static_cast<const std::uint8_t *>( src->pixels );
            for( int y = 0; y < atlas_h; ++y ) {
                std::memcpy( out + static_cast<std::size_t>( y ) * row_bytes,
                             in + static_cast<std::size_t>( y ) * src->pitch, row_bytes );
            }
            SDL_UnmapGPUTransferBuffer( dev.raw(), xfer );

            if( SDL_GPUCommandBuffer *cb = SDL_AcquireGPUCommandBuffer( dev.raw() ) ) {
                if( SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass( cb ) ) {
                    SDL_GPUTextureTransferInfo ti{};
                    ti.transfer_buffer = xfer;
                    ti.offset = 0;
                    ti.pixels_per_row = static_cast<std::uint32_t>( atlas_w );
                    ti.rows_per_layer = static_cast<std::uint32_t>( atlas_h );
                    SDL_GPUTextureRegion reg{};
                    reg.texture = atlas_;
                    reg.w = static_cast<std::uint32_t>( atlas_w );
                    reg.h = static_cast<std::uint32_t>( atlas_h );
                    reg.d = 1;
                    SDL_UploadToGPUTexture( cp, &ti, &reg, false );
                    SDL_EndGPUCopyPass( cp );
                    ok = true;
                }
                SDL_SubmitGPUCommandBuffer( cb );
            }
        }
        SDL_ReleaseGPUTransferBuffer( dev.raw(), xfer );
    }
    if( converted ) {
        SDL_DestroySurface( converted );
    }
    if( !ok ) {
        dbg( DL::Error ) << "terrain_decals: atlas upload failed";
        SDL_ReleaseGPUTexture( dev.raw(), atlas_ );
        atlas_ = nullptr;
        return false;
    }

    DebugLogFL( DL::Info, DC::Main )
            << "terrain_decals: atlas loaded (" << variants_.size()
            << " variants, " << atlas_w << "x" << atlas_h << ")";
    return true;
}

// ---- Init / Shutdown ------------------------------------------------------

auto manager::init( gpu_device &dev ) -> bool
{
    dev_ = &dev;
    return load_atlas( dev );
}

auto manager::shutdown() noexcept -> void
{
    if( atlas_ && dev_ ) {
    SDL_ReleaseGPUTexture( dev_->raw(), atlas_ );
    }
    atlas_ = nullptr;
    variants_.clear();
    cache_.clear();
    dev_ = nullptr;
}

auto manager::ready() const noexcept -> bool
{
    return atlas_ != nullptr && !variants_.empty();
}

auto manager::variant_count() const noexcept -> int
{
    return static_cast<int>( variants_.size() );
}

auto manager::atlas() const noexcept -> const SDL_GPUTexture *
{
    return atlas_;
}

auto manager::variants() const noexcept -> const std::vector<decal_variant_info> &
{
    return variants_;
}

// ---- Placement ------------------------------------------------------------

auto manager::compute_placements( std::uint64_t key,
                                  const std::function<ter_id( int, int )> &ter_at )
-> const std::vector<terrain_decal_instance> &
{
    // Check cache first.
    for( auto &entry : cache_ ) {
        if( entry.key == key ) {
            return entry.placements;
        }
    }

    // Seed RNG from submap key — deterministic.
    seed_from_key( key );

    std::vector<terrain_decal_instance> placements;
    placements.reserve( 16 ); // typical submap won't need more

    // Shuffle tile order for organic distribution.
    std::vector<std::pair<int, int>> tiles;
    tiles.reserve( SEEX * SEEY );
    for( int y = 0; y < SEEY; ++y ) {
        for( int x = 0; x < SEEX; ++x ) {
            tiles.push_back( { x, y } );
        }
    }
    std::shuffle( tiles.begin(), tiles.end(), decal_rng );
    for( const auto &[tx, ty] : tiles ) {
        const ter_id tid = ter_at( tx, ty );
        if( tid == t_null ) {
            continue;
        }

        const ter_t &ter = *tid;
        const auto &cfg = ter.decal_config;
        if( !cfg ) {
            continue;
        }

        // Density roll.
        if( uniform( 0.f, 1.f ) > cfg->density ) {
            continue;
        }

        // Min-spacing check against existing placements.
        const float spacing_sq = static_cast<float>( cfg->min_spacing * cfg->min_spacing );
        bool too_close = false;
        for( const auto &placed : placements ) {
            const float dx = static_cast<float>( placed.tile_x ) - static_cast<float>( tx );
            const float dy = static_cast<float>( placed.tile_y ) - static_cast<float>( ty );
            if( dx * dx + dy * dy < spacing_sq ) {
                too_close = true;
                break;
            }
        }
        if( too_close ) {
            continue;
        }

        // Place decal. The anchor tile is exact; the offset is the sub-tile
        // jitter that keeps a grid of decals from looking like a grid.
        //
        // Rotation is a deliberately SMALL jitter, not a full turn: foliage art
        // is drawn with a definite "up" (blades rise, the base sits on the
        // ground), so a freely rotated tuft reads as upside-down grass. A few
        // degrees of lean breaks up repetition without fighting the art.
        constexpr float max_lean = 0.12f; // radians, ~7 degrees
        terrain_decal_instance inst{};
        inst.tile_x = static_cast<std::uint8_t>( tx );
        inst.tile_y = static_cast<std::uint8_t>( ty );
        inst.off_x = uniform( -0.3f, 0.3f );
        inst.off_y = uniform( -0.3f, 0.3f );
        inst.rotation = uniform( -max_lean, max_lean );
        inst.variant = static_cast<int>( decal_rng() % static_cast<unsigned>( variants_.size() ) );
        placements.push_back( inst );
    }
    cache_.push_back( cache_entry{ .key = key, .placements = std::move( placements ) } );
    return cache_.back().placements;
}

auto manager::invalidate( std::uint64_t key ) -> void
{
    cache_.erase( std::remove_if( cache_.begin(), cache_.end(),
    [key]( const cache_entry & e ) { return e.key == key; } ), cache_.end() );
}

auto manager::clear() -> void
{
    cache_.clear();
}

} // namespace terrain_decals