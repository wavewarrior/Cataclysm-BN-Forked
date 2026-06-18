#include "rmlui_proc_texture.h"

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace lighting
{
namespace
{
struct rgba {
    std::uint8_t r, g, b, a;
};

constexpr rgba DARK{ 41, 32, 22, 255 };     // #292016
constexpr rgba LIGHT{ 161, 136, 95, 255 };  // #a1885f

constexpr int RING = 20;        // ring depth (corner size & edge thickness), px
constexpr int GLYPH = 5;        // 5x5 source glyph
constexpr int GLYPH_SCALE = 2;  // drawn at 2x
constexpr int GW = GLYPH * GLYPH_SCALE;  // glyph footprint (10px)
constexpr int UNIT = 256;       // edge texture length (long, varied repeat)

// Rune band occupies these depths (between the outer rule and the band-inner
// rule). Motifs are drawn here.
constexpr int BAND_TOP = 4;

// The nested border rules ("rails") run the FULL length of every edge and turn
// the corners, so the rune band is always enclosed top and bottom. Depth is
// measured from the OUTER edge.
struct rule_row {
    int depth;
    rgba col;
};
constexpr rule_row ALLROWS[] = {
    { 1, LIGHT }, { 2, LIGHT }, { 3, DARK }, { 14, LIGHT },
    { 16, DARK }, { 17, LIGHT }, { 18, LIGHT },
};

// A rune cell's vertical walls connect the outer rail just above the band
// (depth 3) to the inner rail just below it (depth 14), boxing the glyphs in.
constexpr int DIV_TOP = 3;
constexpr int DIV_BOT = 14;

inline void put( std::vector<std::uint8_t> &px, int W, int H, int x, int y, rgba c )
{
    if( x < 0 || y < 0 || x >= W || y >= H ) {
        return;
    }
    const std::size_t i = ( static_cast<std::size_t>( y ) * W + x ) * 4;
    px[i + 0] = c.r;
    px[i + 1] = c.g;
    px[i + 2] = c.b;
    px[i + 3] = c.a;
}

// A band strip in orientation-agnostic coordinates: `along` runs down the edge,
// `depth` is the perpendicular distance from the outer edge. Horizontal edges
// map (along,depth)->(x,y); vertical edges map ->(x=depth, y=along).
struct strip {
    std::vector<std::uint8_t> *px;
    int w;
    int h;
    bool horizontal;
    int along_len() const
    {
        return horizontal ? w : h;
    }
    void plot( int along, int depth, rgba c ) const
    {
        if( horizontal ) {
            put( *px, w, h, along, depth, c );
        } else {
            put( *px, w, h, depth, along, c );
        }
    }
};

// Corner rules: each rail turns the corner as a continuous nested L-bracket
// (no dots, no taper), running the full ring on both axes so it joins the edge
// rails seamlessly. Mirrored/rotated into the other corners by the decorator.
void draw_corner_rules( std::vector<std::uint8_t> &px, int W, int H )
{
    for( const rule_row &r : ALLROWS ) {
        const int d = r.depth;
        for( int i = 0; i < RING; ++i ) {
            put( px, W, H, i, d, r.col );  // horizontal rail (top edge)
            put( px, W, H, d, i, r.col );  // vertical rail (left edge)
        }
    }
}

// Symmetrical 5x5 glyph (mirrored on X), each lit cell a GLYPH_SCALE block.
void glyph( const strip &s, int a0, int d0, std::mt19937 &gen )
{
    // ~38% lit → sparse, rune-like marks rather than solid noise blocks.
    std::uniform_int_distribution<int> bit( 0, 99 );
    int grid[5][3];
    for( int y = 0; y < 5; ++y ) {
        for( int x = 0; x < 3; ++x ) {
            grid[y][x] = ( bit( gen ) < 38 ) ? 1 : 0;
        }
    }
    for( int y = 0; y < 5; ++y ) {
        for( int x = 0; x < 5; ++x ) {
            const int sxg = ( x < 3 ) ? x : 4 - x;
            if( grid[y][sxg] == 1 ) {
                for( int dy = 0; dy < GLYPH_SCALE; ++dy ) {
                    for( int dx = 0; dx < GLYPH_SCALE; ++dx ) {
                        s.plot( a0 + x * GLYPH_SCALE + dx, d0 + y * GLYPH_SCALE + dy, LIGHT );
                    }
                }
            }
        }
    }
}

// A vertical line `w` px wide (along the band depth), starting at `along`.
void vline( const strip &s, int along, int w, int d0, int d1, rgba c )
{
    for( int dx = 0; dx < w; ++dx ) {
        for( int d = d0; d <= d1; ++d ) {
            s.plot( along + dx, d, c );
        }
    }
}

// A horizontal line at depth `d`, from `a0` to `a1` inclusive.
void hline( const strip &s, int a0, int a1, int d, rgba c )
{
    for( int a = a0; a <= a1; ++a ) {
        s.plot( a, d, c );
    }
}

// The rune band is an airy, irregular chain of runes separated by dividers. A
// rune is 1..3 glyphs. A SINGLE-glyph rune is encased in a closed rectangle; a
// A rune is a sequence of 1..4 glyphs. MOST runes are encased in a closed
// rectangle — a longer sequence makes a longer box, so the band carries plenty
// of long encased runs. SOME runes are bare, their glyphs tied by a horizontal
// spine into one continuous long rune. There are NO continuous edge rails.
// Between runes sits generous dark space, then an optional thicker (2px) divider
// — none (plain gap), single `|`, or double `||`. No dots.
constexpr int WALL = 1;  // outline wall thickness
constexpr int DIVW = 2;  // divider thickness (a tad thicker than a wall)
constexpr int PAD = 3;   // padding glyph<->wall inside a box
constexpr int GGAP = 2;  // gap between glyphs within a rune
constexpr int SP = 4;    // breathing space around dividers / between runes
void draw_band( const strip &s, std::mt19937 &gen )
{
    const int n = s.along_len();
    int a = SP;
    while( true ) {
        // ~70% encased (1..4 glyphs, longer sequences common); rest bare (2..4,
        // spine-tied). A long sequence makes a long box.
        const int kr = static_cast<int>( gen() % 100 );
        const bool encased = kr < 70;
        const int gr = static_cast<int>( gen() % 100 );
        const int ng = encased
                       ? ( ( gr < 26 ) ? 1 : ( gr < 52 ) ? 2 : ( gr < 78 ) ? 3 : 4 )
                       : ( ( gr < 50 ) ? 2 : ( gr < 82 ) ? 3 : 4 );
        const int run = ng * GW + ( ng - 1 ) * GGAP;  // glyph run width
        const int unit_w = encased ? ( WALL + PAD + run + PAD + WALL ) : run;
        if( a + unit_w >= n ) {
            break;
        }
        if( encased ) {
            // Closed rectangle around the glyph sequence.
            vline( s, a, WALL, DIV_TOP, DIV_BOT, LIGHT );
            vline( s, a + unit_w - WALL, WALL, DIV_TOP, DIV_BOT, LIGHT );
            hline( s, a, a + unit_w - 1, DIV_TOP, LIGHT );
            hline( s, a, a + unit_w - 1, DIV_BOT, LIGHT );
        } else {
            // Bare rune: a horizontal spine ties the glyphs into one continuous
            // long rune (rather than scattered marks reading as dots).
            const int mid = BAND_TOP + GW / 2;  // band centre
            hline( s, a, a + run - 1, mid, LIGHT );
        }
        int gx = encased ? ( a + WALL + PAD ) : a;
        for( int k = 0; k < ng; ++k ) {
            glyph( s, gx, BAND_TOP, gen );
            gx += GW + GGAP;
        }
        a += unit_w;
        // Inter-rune spacing: always breathing space, then 40% plain gap,
        // 45% single `|`, 15% double `||`.
        const int dr = static_cast<int>( gen() % 100 );
        a += SP;
        if( dr >= 40 ) {
            vline( s, a, DIVW, DIV_TOP, DIV_BOT, LIGHT );
            a += DIVW;
            if( dr >= 85 ) {  // double divider
                a += SP;
                vline( s, a, DIVW, DIV_TOP, DIV_BOT, LIGHT );
                a += DIVW;
            }
            a += SP;
        }
    }
}

void draw_edge( const strip &s, std::mt19937 &gen )
{
    draw_band( s, gen );
}

std::uint32_t fnv1a( const std::string &s )
{
    std::uint32_t hsh = 2166136261u;
    for( const char ch : s ) {
        hsh ^= static_cast<std::uint8_t>( ch );
        hsh *= 16777619u;
    }
    return hsh;
}
}  // namespace

std::vector<std::uint8_t> gen_runic_frame( const std::string &variant, int &out_w, int &out_h )
{
    // Only three base regions are generated; the decorator mirrors/rotates them
    // into all four corners and both edge pairs (see rmlui_layer::apply_crt).
    // The variant string seeds the rune pattern, so every launch is identical.
    std::mt19937 gen( fnv1a( variant ) );

    auto alloc = [&]( int w, int h ) {
        out_w = w;
        out_h = h;
        return std::vector<std::uint8_t>( static_cast<std::size_t>( w ) * h * 4, 0 );
    };

    if( variant == "runic-corner" ) {
        std::vector<std::uint8_t> px = alloc( RING, RING );
        draw_corner_rules( px, out_w, out_h );
        return px;
    }
    if( variant == "runic-hedge" ) {
        std::vector<std::uint8_t> px = alloc( UNIT, RING );
        const strip s{ &px, out_w, out_h, true };
        draw_edge( s, gen );
        return px;
    }
    if( variant == "runic-vedge" ) {
        std::vector<std::uint8_t> px = alloc( RING, UNIT );
        const strip s{ &px, out_w, out_h, false };
        draw_edge( s, gen );
        return px;
    }

    // Unknown variant: a 1x1 transparent texture (harmless).
    return alloc( 1, 1 );
}
}  // namespace lighting
