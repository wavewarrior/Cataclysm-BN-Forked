#include "rmlui_proc_texture.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <random>
#include <string>
#include <vector>

#include "fstream_utils.h"
#include "json.h"
#include "path_info.h"

namespace lighting
{
namespace
{
struct rgba {
    std::uint8_t r, g, b, a;
};

// Live, debug-tunable config (declared in rmlui_proc_texture.h). Lazily loaded
// from disk on first access so in-game frames adopt the saved look without any
// explicit init call.
runic_params g_runic;
bool g_runic_loaded = false;

// The lit rune-ink colour, built from the live config (alpha always opaque).
inline rgba light_col( const runic_params &c )
{
    return rgba{ static_cast<std::uint8_t>( c.col_r ),
                 static_cast<std::uint8_t>( c.col_g ),
                 static_cast<std::uint8_t>( c.col_b ), 255 };
}

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

// Corner cap: the rune band turns the corner as a set of OPEN L brackets — the
// band's outer wall (DIV_TOP), centre rule and inner wall (DIV_BOT) each bend
// 90°, vertex at the OUTER corner, both arms running inward along the two edges.
// The bracket therefore frames the panel interior (open toward it) and the
// centre-rule arm meets each edge's centre rule, so the band reads as one
// continuous channel turning the corner. Base = top-left; the decorator
// mirrors/rotates it into the other three corners.
void draw_corner_rules( std::vector<std::uint8_t> &px, int W, int H )
{
    const runic_params &cfg = runic_cfg();
    const rgba LIGHT = light_col( cfg );
    const int RING = cfg.ring, DIV_TOP = cfg.div_top, DIV_BOT = cfg.div_bot;
    const int c = ( DIV_TOP + DIV_BOT ) / 2;  // band centre
    const int depths[] = { DIV_TOP, c, DIV_BOT };
    for( const int d : depths ) {
        for( int i = d; i < RING; ++i ) {
            put( px, W, H, i, d, LIGHT );  // horizontal arm (turns along top edge)
            put( px, W, H, d, i, LIGHT );  // vertical arm (turns along left edge)
        }
    }
}

// Symmetrical 5x5 glyph (mirrored on X), each lit cell a GLYPH_SCALE block.
void glyph( const strip &s, int a0, int d0, std::mt19937 &gen )
{
    const runic_params &cfg = runic_cfg();
    const rgba LIGHT = light_col( cfg );
    const int GLYPH_SCALE = cfg.glyph_scale;
    // fill_pct% lit → sparse, rune-like marks rather than solid noise blocks.
    std::uniform_int_distribution<int> bit( 0, 99 );
    int grid[5][3];
    for( int y = 0; y < 5; ++y ) {
        for( int x = 0; x < 3; ++x ) {
            grid[y][x] = ( bit( gen ) < cfg.fill_pct ) ? 1 : 0;
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

// Edge composition (panel-relative, NOT tiled): the edge is drawn at the panel's
// length and laid out symmetrically about its centre — plain rule lines fill the
// span, with compact rune groups stamped at fixed fractions. One of three
// templates (chosen by the panel seed) decides where the groups sit. Top/bottom
// edges use a DOUBLE rule, side edges a SINGLE rule.

// Glyph count for box `i` of an `n`-box group: a symmetric palindrome that swells
// toward the centre (e.g. n=3 -> 1,2,1; n=5 -> 1,2,3,2,1), capped at 3.
int box_glyphs( int i, int n )
{
    const int d = std::min( i, n - 1 - i );
    return std::min( 1 + d, 3 );
}

int box_width( int g )
{
    const runic_params &cfg = runic_cfg();
    const int WALL = cfg.wall, PAD = cfg.pad, GGAP = cfg.ggap;
    const int GW = 5 * cfg.glyph_scale;
    return 2 * WALL + 2 * PAD + g * GW + ( g - 1 ) * GGAP;
}

int group_width( int nel )
{
    const runic_params &cfg = runic_cfg();
    const int DSLOT = cfg.gapi + cfg.divw + cfg.gapi;
    int w = 0;
    for( int i = 0; i < nel; ++i ) {
        w += box_width( box_glyphs( i, nel ) );
    }
    return w + ( nel - 1 ) * DSLOT;
}

// A compact rune group: `nel` encased boxes, single dividers between them.
void draw_group( const strip &s, int a0, int nel, std::mt19937 &gen )
{
    const runic_params &cfg = runic_cfg();
    const rgba LIGHT = light_col( cfg );
    const int WALL = cfg.wall, PAD = cfg.pad, GGAP = cfg.ggap, GAPI = cfg.gapi;
    const int DIVW = cfg.divw, DIV_TOP = cfg.div_top, DIV_BOT = cfg.div_bot;
    const int BAND_TOP = cfg.band_top;
    const int GW = 5 * cfg.glyph_scale;
    const int DSLOT = cfg.gapi + cfg.divw + cfg.gapi;
    int a = a0;
    for( int i = 0; i < nel; ++i ) {
        const int g = box_glyphs( i, nel );
        const int bw = box_width( g );
        vline( s, a, WALL, DIV_TOP, DIV_BOT, LIGHT );
        vline( s, a + bw - WALL, WALL, DIV_TOP, DIV_BOT, LIGHT );
        hline( s, a, a + bw - 1, DIV_TOP, LIGHT );
        hline( s, a, a + bw - 1, DIV_BOT, LIGHT );
        int gx = a + WALL + PAD;
        for( int k = 0; k < g; ++k ) {
            glyph( s, gx, BAND_TOP, gen );
            gx += GW + GGAP;
        }
        a += bw;
        if( i < nel - 1 ) {
            vline( s, a + GAPI, DIVW, DIV_TOP, DIV_BOT, LIGHT );
            a += DSLOT;
        }
    }
}

// A connecting rule between groups, centred on the band. Double = a tight `=`
// (two lines 2px apart) for horizontal edges; single = one centre line for
// vertical edges.
void draw_rule( const strip &s, int a0, int a1, bool dbl )
{
    if( a1 < a0 ) {
        return;
    }
    const runic_params &cfg = runic_cfg();
    const rgba LIGHT = light_col( cfg );
    const int DIV_TOP = cfg.div_top, DIV_BOT = cfg.div_bot;
    const int c = ( DIV_TOP + DIV_BOT ) / 2;  // band centre
    if( dbl ) {
        hline( s, a0, a1, c - 1, LIGHT );
        hline( s, a0, a1, c + 1, LIGHT );
    } else {
        hline( s, a0, a1, c, LIGHT );
    }
}

// Lay out one edge symmetrically: rune groups at template-defined fractions of
// the usable span (inside the corner margins), joined by rule lines.
void draw_edge( const strip &s, unsigned seed, bool dbl, int tmpl )
{
    const int n = s.along_len();
    std::mt19937 gen( seed );

    const runic_params &cfg = runic_cfg();
    const int RING = cfg.ring;
    const int RGAP = cfg.rgap;         // gap between a rule and a group
    const int lo = RING;               // keep groups clear of the corners
    const int hi = n - 1 - RING;
    const int usable = hi - lo;

    // One of three layout templates. `tmpl` (0/1/2) forces a specific one; -1
    // falls back to the panel seed. Groups sit at fractions of the usable span;
    // rule lines fill the rest. Centre/thirds are deliberately sparse (fixed
    // group count, so they thin out on big panels); the fixed-interval template
    // scales its group COUNT to the span length, so it stays dense at any size.
    const int which = ( tmpl >= 0 && tmpl <= 2 ) ? tmpl : static_cast<int>( seed % 3 );
    struct pos {
        double frac;
        int nel;
    };
    std::vector<pos> ps;
    switch( which ) {
        case 0:  // centred: a single group at the middle
            ps = { { 0.5, 4 } };
            break;
        case 1:  // thirds: groups at 1/3, centre, 2/3
            ps = { { 1.0 / 3, 2 }, { 0.5, 3 }, { 2.0 / 3, 2 } };
            break;
        default: {  // fixed interval: a group every ~PITCH px, run centred in span
            const int PITCH = cfg.pitch;
            const int count = std::max( 1, usable / PITCH );
            for( int i = 0; i < count; ++i ) {
                ps.push_back( { ( i + 0.5 ) / count, ( i % 2 == 0 ) ? 2 : 3 } );
            }
            break;
        }
    }

    std::vector<std::pair<int, int>> spans;  // {a0, a1} of each placed group
    int last_end = lo - 1;
    for( const pos &p : ps ) {
        const int gw = group_width( p.nel );
        if( gw <= 0 || gw > usable ) {
            continue;
        }
        const int cx = lo + static_cast<int>( p.frac * usable );
        int a0 = cx - gw / 2;
        a0 = std::max( a0, lo );
        a0 = std::min( a0, hi - gw + 1 );
        if( a0 <= last_end + RGAP ) {  // would collide with previous group
            continue;
        }
        draw_group( s, a0, p.nel, gen );
        spans.emplace_back( a0, a0 + gw - 1 );
        last_end = a0 + gw - 1;
    }

    // Rule lines fill the gaps (and run to 0 / n-1 so they tuck under the corners).
    int prev = 0;
    for( const std::pair<int, int> &sp : spans ) {
        draw_rule( s, prev, sp.first - 1 - RGAP, dbl );
        prev = sp.second + 1 + RGAP;
    }
    draw_rule( s, prev, n - 1, dbl );
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

runic_params &runic_cfg()
{
    if( !g_runic_loaded ) {
        g_runic_loaded = true;   // set first so load_runic_cfg() won't recurse
        load_runic_cfg();
    }
    return g_runic;
}

void save_runic_cfg()
{
    const std::string path = PATH_INFO::config_dir() + "runic_frame.json";
    const runic_params &c = g_runic;
    write_to_file( path, [&]( std::ostream & o ) {
        JsonOut j( o, true );
        j.start_object();
        j.member( "col_r", c.col_r );
        j.member( "col_g", c.col_g );
        j.member( "col_b", c.col_b );
        j.member( "ring", c.ring );
        j.member( "glyph_scale", c.glyph_scale );
        j.member( "band_top", c.band_top );
        j.member( "div_top", c.div_top );
        j.member( "div_bot", c.div_bot );
        j.member( "wall", c.wall );
        j.member( "divw", c.divw );
        j.member( "pad", c.pad );
        j.member( "ggap", c.ggap );
        j.member( "gapi", c.gapi );
        j.member( "rgap", c.rgap );
        j.member( "pitch", c.pitch );
        j.member( "unit", c.unit );
        j.member( "fill_pct", c.fill_pct );
        j.member( "frame_inset", c.frame_inset );
        j.member( "force_template", c.force_template );
        j.member( "use_fixed_seed", c.use_fixed_seed );
        // stored as int (bit-preserving round-trip via static_cast on load)
        j.member( "seed", static_cast<int>( c.seed ) );
        j.end_object();
    }, "runic frame config" );
}

void load_runic_cfg()
{
    const std::string path = PATH_INFO::config_dir() + "runic_frame.json";
    runic_params &c = g_runic;
    // optional=true → silent when the file does not yet exist (first launch).
    read_from_file_json( path, [&]( JsonIn & in ) {
        JsonObject jo = in.get_object();
        jo.allow_omitted_members();
        c.col_r = jo.get_int( "col_r", c.col_r );
        c.col_g = jo.get_int( "col_g", c.col_g );
        c.col_b = jo.get_int( "col_b", c.col_b );
        c.ring = jo.get_int( "ring", c.ring );
        c.glyph_scale = jo.get_int( "glyph_scale", c.glyph_scale );
        c.band_top = jo.get_int( "band_top", c.band_top );
        c.div_top = jo.get_int( "div_top", c.div_top );
        c.div_bot = jo.get_int( "div_bot", c.div_bot );
        c.wall = jo.get_int( "wall", c.wall );
        c.divw = jo.get_int( "divw", c.divw );
        c.pad = jo.get_int( "pad", c.pad );
        c.ggap = jo.get_int( "ggap", c.ggap );
        c.gapi = jo.get_int( "gapi", c.gapi );
        c.rgap = jo.get_int( "rgap", c.rgap );
        c.pitch = jo.get_int( "pitch", c.pitch );
        c.unit = jo.get_int( "unit", c.unit );
        c.fill_pct = jo.get_int( "fill_pct", c.fill_pct );
        c.frame_inset = jo.get_int( "frame_inset", c.frame_inset );
        c.force_template = jo.get_int( "force_template", c.force_template );
        c.use_fixed_seed = jo.get_bool( "use_fixed_seed", c.use_fixed_seed );
        c.seed = static_cast<unsigned>( jo.get_int( "seed", static_cast<int>( c.seed ) ) );
    }, true );
}

std::vector<std::uint8_t> gen_runic_frame( const std::string &variant, int &out_w, int &out_h )
{
    // Variants: "runic-corner" (fixed 20x20 nested-square anchor); edges are
    // panel-relative, "runic-hedge:<len>:<seed>" (horizontal) and
    // "runic-vedge:<len>:<seed>" (vertical). <len> is the panel border-box length
    // in px; <seed> selects the symmetric layout template and the rune glyphs, so
    // a given panel size is deterministic across launches. apply_crt mirrors the
    // base regions into all four corners and both edge pairs.
    const runic_params &cfg = runic_cfg();
    const int RING = cfg.ring;
    const int UNIT = cfg.unit;

    auto alloc = [&]( int w, int h ) {
        out_w = w;
        out_h = h;
        return std::vector<std::uint8_t>( static_cast<std::size_t>( w ) * h * 4, 0 );
    };

    // rfind(...,0)==0 (prefix match) so the cache-bust suffix "runic-corner:G<n>"
    // still routes here; the corner art ignores any trailing params.
    if( variant.rfind( "runic-corner", 0 ) == 0 ) {
        std::vector<std::uint8_t> px = alloc( RING, RING );
        draw_corner_rules( px, out_w, out_h );
        return px;
    }

    // Parse "<prefix>:<len>:<seed>" (both optional). Defaults: len=UNIT, seed from
    // the variant hash (so the bare "runic-hedge"/"runic-vedge" still work).
    // Format: "<prefix>:<len>:<seed>:<tmpl>" (len/seed/tmpl all optional). tmpl
    // (0/1/2) forces a layout template; -1 leaves it seed-driven.
    auto parse_edge = [&]( const std::string & prefix, int &len, unsigned &seed, int &tmpl ) -> bool {
        if( variant.rfind( prefix, 0 ) != 0 ) {
            return false;
        }
        len = UNIT;
        seed = fnv1a( variant );
        tmpl = -1;
        const std::size_t c1 = variant.find( ':' );
        if( c1 != std::string::npos ) {
            const std::size_t c2 = variant.find( ':', c1 + 1 );
            try {
                len = std::stoi( variant.substr( c1 + 1, c2 - c1 - 1 ) );
                if( c2 != std::string::npos ) {
                    // stoul reads the leading number, stopping at any ":<tmpl>".
                    seed = static_cast<unsigned>( std::stoul( variant.substr( c2 + 1 ) ) );
                    const std::size_t c3 = variant.find( ':', c2 + 1 );
                    if( c3 != std::string::npos ) {
                        tmpl = std::stoi( variant.substr( c3 + 1 ) );
                    }
                }
            } catch( ... ) {
                // Malformed params: fall back to the defaults.
                len = UNIT;
                seed = fnv1a( variant );
                tmpl = -1;
            }
        }
        len = std::max( len, 2 * RING + 1 );  // must fit the corner margins
        return true;
    };

    int len = UNIT;
    unsigned seed = 0;
    int tmpl = -1;
    if( parse_edge( "runic-hedge", len, seed, tmpl ) ) {
        std::vector<std::uint8_t> px = alloc( len, RING );
        const strip s{ &px, out_w, out_h, true };
        draw_edge( s, seed, /*dbl=*/true, tmpl );  // horizontal: double rule
        return px;
    }
    if( parse_edge( "runic-vedge", len, seed, tmpl ) ) {
        std::vector<std::uint8_t> px = alloc( RING, len );
        const strip s{ &px, out_w, out_h, false };
        draw_edge( s, seed, /*dbl=*/false, tmpl );  // vertical: single rule
        return px;
    }

    // Debug marker bars (F12 panel-centre cross): "dbg-v:<h>" = 3xh, "dbg-h:<w>"
    // = wx3, solid magenta. Length parsed after the colon; default 64.
    const bool dvert = variant.rfind( "dbg-v", 0 ) == 0;
    if( dvert || variant.rfind( "dbg-h", 0 ) == 0 ) {
        int n = 64;
        const std::size_t c = variant.find( ':' );
        if( c != std::string::npos ) {
            try {
                n = std::max( 1, std::stoi( variant.substr( c + 1 ) ) );
            } catch( ... ) {
                n = 64;
            }
        }
        std::vector<std::uint8_t> px = dvert ? alloc( 3, n ) : alloc( n, 3 );
        for( int i = 0; i < out_w * out_h; ++i ) {
            px[i * 4 + 0] = 255;
            px[i * 4 + 1] = 0;
            px[i * 4 + 2] = 255;
            px[i * 4 + 3] = 255;
        }
        return px;
    }

    // Unknown variant: a 1x1 transparent texture (harmless).
    return alloc( 1, 1 );
}
}  // namespace lighting
