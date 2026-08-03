// Wire-format gate for the C++ <-> HLSL sprite instance record.
//
// `lighting::sprite_instance` (src/lighting/sprite_batcher.h) is declared a
// second and third time, VERBATIM and with no `#include` to share it, in
// data/shaders/lighting/src/sprite.vert.hlsl and shadow.vert.hlsl. Both
// shaders index the SAME instance storage buffer
// (`StructuredBuffer<SpriteInstance>`), shadow.vert.hlsl because
// render_state::flush_shadow_casters runs a second sprite_batcher over it, so
// every declaration must agree byte for byte on field count AND order.
//
// It did not. shadow.vert.hlsl once declared only the first 16 floats while
// the real record was 80 bytes, so `Instances[iid + instance_base]` stepped
// 64 bytes into an 80-byte record and every caster past the first read a
// misaligned mixture of its neighbours' fields. That shipped unnoticed
// because debug_params::shadow_mask_str defaults to 0.0, and it was found by
// hand — the existing `static_assert( sizeof( sprite_instance ) == 96 )` sees
// only the C++ side, and the shaders are compiled at RUNTIME from data/, so
// the C++ build never parses them. This test is that missing gate.
//
// Tagged `[lighting]` only — deliberately NOT `[.gpu]`: it merely reads two
// text files, so it must run in the ordinary cata_test-tiles lane.

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "catch/catch_amalgamated.hpp"
#include "lighting/sprite_batcher.h"

namespace
{

/// THE SINGLE SOURCE OF TRUTH for the sprite instance wire format.
///
/// Adding, removing, renaming or REORDERING a field means editing all THREE
/// declarations together:
///   * src/lighting/sprite_batcher.h                (`lighting::sprite_instance`)
///   * data/shaders/lighting/src/sprite.vert.hlsl   (`struct SpriteInstance`)
///   * data/shaders/lighting/src/shadow.vert.hlsl   (`struct SpriteInstance`)
/// ...and this list. This test is what makes forgetting any one of the three a
/// test failure rather than a silently misaligned GPU read.
const std::vector<std::string> canonical_fields = {
    "dst_x", "dst_y", "dst_w", "dst_h",
    "src_u", "src_v", "src_uw", "src_vh",
    "tint_r", "tint_g", "tint_b", "tint_a",
    "rotation", "light_mul", "pad1", "pad2",
    "extrude_px", "extrude_dark", "extrude_lean", "extrude_pad",
    "light_mode", "flash_r", "flash_g", "flash_b",
};

/// One declarator from an HLSL struct body, in declaration order.
struct hlsl_field {
    std::string type;
    std::string name;
};

/// tests/test_main.cpp runs with the repo root as CWD (the same assumption
/// tests/data/*.json loads rely on), but no other test reads out of `data/`,
/// so a couple of parent-relative fallbacks are tried before giving up — and
/// giving up names every path attempted rather than yielding an empty list.
auto read_shader_source( const std::string &relative_path ) -> std::string
{
    const std::string prefixes[] = { "", "../", "../../" };
    std::string tried;
    for( const std::string &prefix : prefixes ) {
        const std::string path = prefix + relative_path;
        std::ifstream file( path, std::ios::binary );
        if( file.good() ) {
            return { std::istreambuf_iterator<char>( file ), std::istreambuf_iterator<char>() };
        }
        tried += ( tried.empty() ? "" : ", " ) + path;
    }
    FAIL( "could not open shader source; tried: " << tried
          << " (tests must run with the repo root as CWD)" );
    return {};
}

/// Drops `//` line comments and `/* */` block comments. Only the shapes these
/// two files actually use need to work.
auto strip_comments( const std::string &src ) -> std::string
{
    std::string out;
    out.reserve( src.size() );
    for( std::size_t i = 0; i < src.size(); ) {
        if( src.compare( i, 2, "//" ) == 0 ) {
            while( i < src.size() && src[i] != '\n' ) {
                ++i;
            }
        } else if( src.compare( i, 2, "/*" ) == 0 ) {
            const auto end = src.find( "*/", i + 2 );
            i = ( end == std::string::npos ) ? src.size() : end + 2;
        } else {
            out.push_back( src[i] );
            ++i;
        }
    }
    return out;
}

/// Extracts the declarators of `struct SpriteInstance { ... };` in order.
///
/// Fails loudly if the struct or its terminator cannot be found: returning an
/// empty list would quietly match nothing, which is precisely the failure mode
/// this test exists to close.
auto parse_sprite_instance( const std::string &path ) -> std::vector<hlsl_field>
{
    const std::string src = strip_comments( read_shader_source( path ) );

    const auto decl = src.find( "struct SpriteInstance" );
    INFO( "no `struct SpriteInstance` declaration in " << path );
    REQUIRE( decl != std::string::npos );

    const auto open = src.find( '{', decl );
    INFO( "unterminated `struct SpriteInstance` (no `{`) in " << path );
    REQUIRE( open != std::string::npos );

    const auto close = src.find( '}', open );
    INFO( "unterminated `struct SpriteInstance` (no `}`) in " << path );
    REQUIRE( close != std::string::npos );

    std::vector<hlsl_field> fields;
    std::istringstream body( src.substr( open + 1, close - open - 1 ) );
    // Each statement is `<type> name[, name]*;`.
    std::string statement;
    while( std::getline( body, statement, ';' ) ) {
        std::istringstream words( statement );
        std::string type;
        if( !( words >> type ) ) {
            continue; // trailing whitespace after the last declarator
        }
        std::string rest;
        std::getline( words, rest );
        std::istringstream names( rest );
        std::string name;
        while( std::getline( names, name, ',' ) ) {
            const auto begin = name.find_first_not_of( " \t\r\n" );
            if( begin == std::string::npos ) {
                continue;
            }
            const auto end = name.find_last_not_of( " \t\r\n" );
            fields.push_back( { type, name.substr( begin, end - begin + 1 ) } );
        }
    }

    INFO( "parsed no fields at all from `struct SpriteInstance` in " << path );
    REQUIRE( !fields.empty() );
    return fields;
}

/// Compares a parsed declaration against the canonical list, naming the file
/// and the FIRST differing index so a reorder is as loud as a truncation.
void check_matches_canonical( const std::string &path, const std::vector<hlsl_field> &fields )
{
    INFO( path << " declares " << fields.size() << " fields; canonical wire format has "
          << canonical_fields.size() );
    CHECK( fields.size() == canonical_fields.size() );

    const auto shared = std::min( fields.size(), canonical_fields.size() );
    for( std::size_t i = 0; i < shared; ++i ) {
        if( fields[i].name != canonical_fields[i] ) {
            FAIL( path << ": first differing field at index " << i << " — declares `"
                  << fields[i].name << "`, canonical wire format has `"
                  << canonical_fields[i] << "`" );
        }
        // A stray `int`/`uint` would keep the struct the same size but
        // reinterpret the bits, so the type is checked alongside the name.
        if( fields[i].type != "float" ) {
            FAIL( path << ": field " << i << " (`" << fields[i].name << "`) is declared `"
                  << fields[i].type << "`, must be `float` — a non-float of the same width "
                  "silently reinterprets the wire bits" );
        }
    }
}

const std::string sprite_vert_path = "data/shaders/lighting/src/sprite.vert.hlsl";
const std::string shadow_vert_path = "data/shaders/lighting/src/shadow.vert.hlsl";

} // namespace

TEST_CASE( "sprite_instance wire format matches both HLSL declarations", "[lighting]" )
{
    const auto sprite_fields = parse_sprite_instance( sprite_vert_path );
    const auto shadow_fields = parse_sprite_instance( shadow_vert_path );

    check_matches_canonical( sprite_vert_path, sprite_fields );
    check_matches_canonical( shadow_vert_path, shadow_fields );

    SECTION( "the two shader declarations agree with each other" ) {
        // shadow.vert.hlsl is drawn by render_state::flush_shadow_casters on a
        // SECOND sprite_batcher over the SAME instance buffer, so its stride
        // must equal sprite.vert.hlsl's exactly.
        const auto shared = std::min( sprite_fields.size(), shadow_fields.size() );
        for( std::size_t i = 0; i < shared; ++i ) {
            if( sprite_fields[i].name != shadow_fields[i].name ) {
                FAIL( "sprite.vert.hlsl and shadow.vert.hlsl first differ at field index "
                      << i << ": `" << sprite_fields[i].name << "` vs `"
                      << shadow_fields[i].name << "`" );
            }
        }
        CHECK( sprite_fields.size() == shadow_fields.size() );
    }

    SECTION( "the canonical list matches the real C++ struct" ) {
        // C++ has no reflection, so the FIELD ORDER of `lighting::sprite_instance`
        // cannot be verified here. That side is pinned by `sizeof` plus code
        // review; the HLSL side is pinned exactly. An honest limitation — this
        // test proves the two shaders match the canonical list and that the list
        // is the right LENGTH for the C++ struct, not that C++ orders it that way.
        CHECK( sizeof( lighting::sprite_instance ) == 4 * canonical_fields.size() );

        // sprite_batcher.h's `pad*` fields exist to keep the record 16-byte
        // aligned, which the vertex shader's StructuredBuffer<SpriteInstance>
        // binding requires; a size that is not a multiple of 16 breaks that.
        CHECK( sizeof( lighting::sprite_instance ) % 16 == 0 );
        CHECK( alignof( lighting::sprite_instance ) == alignof( float ) );
    }
}
