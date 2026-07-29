#include "catch/catch_amalgamated.hpp"

#include <set>

#include "coordinates.h"
#include "field_type.h"
#include "splatmap_stamps.h"
#include "type_id.h"

// The splatmap suppresses the grid-locked tile sprite for exactly the field
// types it draws decals for, so covers_field IS the contract between the two
// renderers: a false negative double-draws a splatter, a false positive makes a
// field invisible.
TEST_CASE( "splatmap_covers_every_blood_and_gibs_field", "[splatmap]" )
{
    CHECK( splatmap::covers_field( fd_blood ) );
    CHECK( splatmap::covers_field( fd_blood_veggy ) );
    CHECK( splatmap::covers_field( fd_blood_insect ) );
    CHECK( splatmap::covers_field( fd_blood_invertebrate ) );
    CHECK( splatmap::covers_field( fd_gibs_flesh ) );
    CHECK( splatmap::covers_field( fd_gibs_veggy ) );
    CHECK( splatmap::covers_field( fd_gibs_insect ) );
    CHECK( splatmap::covers_field( fd_gibs_invertebrate ) );
}

TEST_CASE( "splatmap_covers_nothing_else", "[splatmap]" )
{
    CHECK_FALSE( splatmap::covers_field( fd_null ) );
    CHECK_FALSE( splatmap::covers_field( fd_web ) );
    CHECK_FALSE( splatmap::covers_field( fd_bile ) );
    CHECK_FALSE( splatmap::covers_field( fd_sludge ) );
    CHECK_FALSE( splatmap::covers_field( fd_fire ) );
}

// key_of packs a submap coordinate into the splatmap cache key. A collision
// would make two different submaps share one decal texture, so distinctness
// across each axis (including negatives, which are biased into the packed
// range) is the property that matters.
TEST_CASE( "splatmap_key_is_injective_per_axis", "[splatmap]" )
{
    const auto key = []( int x, int y, int z ) {
        return splatmap::key_of( tripoint_abs_sm( x, y, z ) );
    };

    const std::set<std::uint64_t> keys{
        key( 0, 0, 0 ), key( 1, 0, 0 ), key( 0, 1, 0 ), key( 0, 0, 1 ),
        key( -1, 0, 0 ), key( 0, -1, 0 ), key( 0, 0, -1 ), key( -1, -1, -1 ),
    };
    CHECK( keys.size() == 8 );

    // Stable across repeated calls, including for negative coordinates — the
    // seed/regenerate path depends on the same submap hashing identically after
    // a reload.
    CHECK( key( -3, -7, -2 ) == key( -3, -7, -2 ) );
    CHECK( key( 12345, -6789, 5 ) == key( 12345, -6789, 5 ) );

    // Adjacent submaps on the x axis must not alias into each other's y range.
    CHECK( key( 1, 0, 0 ) != key( 0, 1, 0 ) );
    CHECK( key( 0, 1, 0 ) != key( 0, 0, 1 ) );
}

// queue_splatter is called from map::add_splatter, which runs in tests where no
// renderer exists. It must be a silent no-op there rather than dereferencing a
// null tilecontext.
TEST_CASE( "splatmap_queue_splatter_is_safe_without_a_renderer", "[splatmap]" )
{
    CHECK_NOTHROW( splatmap::queue_splatter( tripoint_bub_ms( 60, 60, 0 ), fd_blood, 3 ) );
    CHECK_NOTHROW( splatmap::queue_splatter( tripoint_bub_ms( 60, 60, 0 ), fd_null, 1 ) );
    CHECK_NOTHROW( splatmap::seed_submap( tripoint_bub_ms( 60, 60, 0 ), 0u ) );
}

// active() is the SHARED gate: draw_field_or_item hides the grid-locked blood
// sprite only when it is true, and render_world_pass_w composites decals only
// when it is true. With no renderer (this test binary, and the --check-mods
// path) it MUST be false — otherwise the sprite would be suppressed with
// nothing drawn in its place and blood would be invisible.
TEST_CASE( "splatmap_is_inactive_without_a_renderer", "[splatmap]" )
{
    CHECK_FALSE( splatmap::active() );
}
