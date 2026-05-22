#include "lighting/snapshot.h"

#include <algorithm>
#include <cmath>

#include "character.h"
#include "creature.h"
#include "debug.h"
#include "field.h"
#include "game.h"
#include "game_constants.h"
#include "item.h"
#include "map.h"
#include "map_iterator.h"
#include "monster.h"
#include "npc.h"
#include "units.h"
#include "vehicle.h"
#include "veh_type.h"
#include "vehicle_part.h"
#include "vpart_position.h"

static constexpr float M_PIf = 3.14159265358979323846f;
static constexpr float FALLOFF_DEFAULT = 1.5f;

namespace lighting
{

// Helper: construct a basic OMNI gpu_emitter from tile position + intensity + color.
static gpu_emitter make_omni( int lx, int ly, int lz,
                               float radius, float r, float g, float b )
{
    gpu_emitter e{};
    e.pos_x           = static_cast<float>( lx ) + 0.5f;
    e.pos_y           = static_cast<float>( ly ) + 0.5f;
    e.pos_z           = static_cast<float>( lz );
    e.radius          = radius;
    e.r               = r;
    e.g               = g;
    e.b               = b;
    e.falloff         = FALLOFF_DEFAULT;
    e.cone_dir_x      = 0.0f;
    e.cone_dir_y      = 0.0f;
    e.cone_half_angle = M_PIf;
    e.shape           = static_cast<uint32_t>( emitter_shape::OMNI );
    // Stable per-tile seed so flicker is spatially coherent across frames.
    e.flicker_seed    = static_cast<uint32_t>( lx * 31337 + ly * 7919 );
    return e;
}

// Helper: construct a CONE gpu_emitter.
static gpu_emitter make_cone( int lx, int ly, int lz,
                               float radius, float r, float g, float b,
                               float dir_x, float dir_y, float half_angle_rad )
{
    gpu_emitter e = make_omni( lx, ly, lz, radius, r, g, b );
    e.cone_dir_x      = dir_x;
    e.cone_dir_y      = dir_y;
    e.cone_half_angle = half_angle_rad;
    e.shape           = static_cast<uint32_t>( emitter_shape::CONE );
    return e;
}

// Enumerate all emitters for a single z-level and append to `out`.
// Mirrors map::generate_lightmap_worker() and the character/creature
// enumeration above it in lightmap.cpp.
static void collect_zlev( const map &m, int zlev,
                           std::vector<gpu_emitter> &out )
{
    // ---------- terrain / furniture / fields / items (submap grid) ----------
    for( int smx = 0; smx < my_MAPSIZE; ++smx ) {
        for( int smy = 0; smy < my_MAPSIZE; ++smy ) {
            const submap *cur = m.get_submap_at_grid( tripoint_bub_sm{ smx, smy, zlev } );
            if( !cur ) {
                continue;
            }
            for( int sx = 0; sx < SEEX; ++sx ) {
                for( int sy = 0; sy < SEEY; ++sy ) {
                    const int lx = smx * SEEX + sx;
                    const int ly = smy * SEEY + sy;
                    const tripoint_bub_ms p{ lx, ly, zlev };

                    // Terrain
                    const ter_t *terrain = cur->get_ter( { sx, sy } ).obj_ptr();
                    if( terrain && terrain->light_emitted > 0 ) {
                        out.push_back( make_omni( lx, ly, zlev,
                                                  static_cast<float>( terrain->light_emitted ),
                                                  terrain->light_color.r,
                                                  terrain->light_color.g,
                                                  terrain->light_color.b ) );
                    }

                    // Furniture
                    const furn_t *furniture = cur->get_furn( { sx, sy } ).obj_ptr();
                    if( furniture && furniture->light_emitted > 0 ) {
                        out.push_back( make_omni( lx, ly, zlev,
                                                  static_cast<float>( furniture->light_emitted ),
                                                  furniture->light_color.r,
                                                  furniture->light_color.g,
                                                  furniture->light_color.b ) );
                    }

                    // Fields
                    std::ranges::for_each( cur->get_field( { sx, sy } ), [&]( const auto &fld ) {
                        if( !fld.first.is_valid() ) {
                            return;
                        }
                        const field_entry *fe = &fld.second;
                        const int fe_lum = fe->light_emitted();
                        if( fe_lum > 0 ) {
                            const light_color_rgb fc = fe->light_color();
                            out.push_back( make_omni( lx, ly, zlev,
                                                      static_cast<float>( fe_lum ),
                                                      fc.r, fc.g, fc.b ) );
                        }
                    } );

                    // Items
                    if( cur->get_lum( { sx, sy } ) ) {
                        for( const item * const itm : m.i_at( p ) ) {
                            float ilum = 0.0f;
                            units::angle iwidth = 0_degrees;
                            units::angle idir   = 0_degrees;
                            if( itm->getlight( ilum, iwidth, idir ) ) {
                                if( iwidth > 0_degrees ) {
                                    // Arc/directional light — convert to CONE approximation.
                                    const float dir_rad = units::to_radians( idir );
                                    out.push_back( make_cone( lx, ly, zlev,
                                                              ilum, 0, 0, 0,
                                                              std::cos( dir_rad ),
                                                              std::sin( dir_rad ),
                                                              units::to_radians( iwidth ) * 0.5f ) );
                                } else {
                                    out.push_back( make_omni( lx, ly, zlev,
                                                              ilum, 0, 0, 0 ) );
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ---------- vehicles ----------
    // Mirrors the vehicle lights loop in generate_lightmap_worker.
    for( const wrapped_vehicle &wv : m.get_vehicles() ) {
        const vehicle *v = wv.v;
        if( !v ) {
            continue;
        }
        if( v->global_pos3().z != zlev ) {
            continue;
        }

        for( const vehicle_part * const pt : v->lights( true ) ) {
            const vpart_info &vp = pt->info();
            const tripoint part_pos = v->global_part_pos3( *pt );
            const int lx = part_pos.x;
            const int ly = part_pos.y;

            const float r = vp.light_color.r;
            const float g = vp.light_color.g;
            const float b = vp.light_color.b;

            if( vp.has_flag( VPFLAG_CONE_LIGHT ) || vp.has_flag( VPFLAG_WIDE_CONE_LIGHT ) ) {
                // Directed cone light.  45° half-angle for narrow, 45° for wide cone.
                const float half_rad = vp.has_flag( VPFLAG_WIDE_CONE_LIGHT )
                                       ? units::to_radians( 45_degrees )
                                       : units::to_radians( 22.5_degrees );
                const float face_rad = static_cast<float>( v->face.dir() ) *
                                       ( M_PIf / 180.0f );
                out.push_back( make_cone( lx, ly, zlev,
                                          static_cast<float>( vp.bonus ),
                                          r, g, b,
                                          std::cos( face_rad ),
                                          std::sin( face_rad ),
                                          half_rad ) );
            } else if( vp.has_flag( VPFLAG_CIRCLE_LIGHT ) ||
                       vp.has_flag( VPFLAG_DOME_LIGHT )   ||
                       vp.has_flag( VPFLAG_AISLE_LIGHT )  ||
                       vp.has_flag( VPFLAG_ATOMIC_LIGHT ) ) {
                out.push_back( make_omni( lx, ly, zlev,
                                          static_cast<float>( vp.bonus ),
                                          r, g, b ) );
            }
        }
    }
}

std::vector<gpu_emitter> build_emitter_snapshot( event_queue &eq, float frame_ms )
{
    std::vector<gpu_emitter> out;
    out.reserve( 1024 );

    if( !g ) {
        return out;
    }

    const map &m = get_map();
    const int zlev = g->u.pos().z;

    // ---------- characters (player + NPCs) ----------
    auto collect_character = [&]( const Character &c ) {
        const float lum = c.active_light();
        if( lum <= 0.0f ) {
            return;
        }
        const tripoint pos = c.pos();
        if( !m.inbounds( pos ) ) {
            return;
        }
        out.push_back( make_omni( pos.x, pos.y, pos.z, lum, 0, 0, 0 ) );
        if( c.has_effect( effect_onfire ) ) {
            out.push_back( make_omni( pos.x, pos.y, pos.z, 8.0f, 1.0f, 0.5f, 0.0f ) );
        }
    };

    collect_character( get_player_character() );
    for( const npc &guy : g->all_npcs() ) {
        collect_character( guy );
    }

    // ---------- monsters ----------
    for( const monster &critter : g->all_monsters() ) {
        if( critter.is_hallucination() ) {
            continue;
        }
        const tripoint mp = critter.pos();
        if( !m.inbounds( mp ) ) {
            continue;
        }
        if( critter.has_effect( effect_onfire ) ) {
            out.push_back( make_omni( mp.x, mp.y, mp.z, 8.0f, 1.0f, 0.5f, 0.0f ) );
        }
        if( critter.type->luminance > 0 ) {
            out.push_back( make_omni( mp.x, mp.y, mp.z,
                                      critter.type->luminance, 0, 0, 0 ) );
        }
    }

    // ---------- static emitters (terrain / furniture / fields / items / vehicles) ----------
    collect_zlev( m, zlev, out );

    // ---------- flash events from event_queue ----------
    {
        std::vector<flash_event> flashes;
        eq.drain( frame_ms, flashes );
        for( const flash_event &f : flashes ) {
            // Fade intensity linearly over lifespan.
            const float frac = 1.0f - ( f.elapsed_ms / f.duration_ms );
            const float radius = f.intensity * std::max( 0.0f, frac );
            const tripoint_abs_ms &wp = f.pos;
            // Convert to local map coordinates.
            const tripoint local = m.getlocal( wp );
            if( !m.inbounds( local ) ) {
                continue;
            }
            out.push_back( make_omni( local.x, local.y, local.z,
                                      radius, f.r, f.g, f.b ) );
        }
    }

    // Warn if we're approaching the per-frame budget.
    if( static_cast<int>( out.size() ) > MAX_EMITTERS * 3 / 4 ) {
        dbg( DL::Warn ) << "build_emitter_snapshot: " << out.size()
                        << " emitters (budget " << MAX_EMITTERS << ")";
    }
    // Clamp hard at MAX_EMITTERS to prevent SSBO overflow.
    if( static_cast<int>( out.size() ) > MAX_EMITTERS ) {
        out.resize( MAX_EMITTERS );
    }

    return out;
}

} // namespace lighting
