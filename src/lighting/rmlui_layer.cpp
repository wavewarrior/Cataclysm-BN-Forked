#include "rmlui_layer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include <SDL3/SDL.h>

#include <fstream>
#include <iterator>

#include <RmlUi/Core.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/RenderManager.h>

#include "debug.h"
#include "gpu_device.h"
#include "path_info.h"
#include "rmlui_render_interface.h"
#include "rmlui_system_interface.h"
#include "ui_theme.h"

// Lighting/ files must define dbg themselves (not globally available).
#define dbg( x ) DebugLogFL( ( x ), DC::SDL )

namespace rmlui_layer
{

namespace
{

bool g_ready = false;
// init() tried once (success or failure); don't re-attempt every frame.
bool g_attempted = false;
// Window kept for per-frame context sizing + the render-pass projection.
SDL_Window *g_window = nullptr;

// Interfaces are held by raw pointer inside RmlUi, so they must outlive
// Rml::Shutdown(). Owned here; destroyed AFTER Rml::Shutdown() in shutdown().
std::unique_ptr<lighting::rmlui_render_interface> g_render;
std::unique_ptr<lighting::rmlui_system_interface> g_system;

// Owned by RmlUi core; destroyed by Rml::Shutdown(). Not deleted manually.
Rml::Context *g_context = nullptr;

// Last physical/logical pixel ratio applied to the context (HiDPI density).
float g_density_ratio = 1.0f;

// User UI-scale multiplier (F4 dev slider). Multiplies the HiDPI ratio applied to
// the context's dp lengths, scaling font + all dp spacing across every RmlUi panel.
// Does NOT touch g_density_ratio (input mapping keeps the true physical/logical ratio).
float g_ui_scale = 1.0f;

// CRT post-effect knobs (F4 sliders). See crt() / apply_crt().
crt_params g_crt;

// Documents currently open (shown) via open_document(), in open order. The
// layer is "active" while this is non-empty. Documents are owned by g_context;
// this only tracks which are live so active()/the frame gates can see them.
std::vector<Rml::ElementDocument *> g_open_docs;

bool any_open()
{
    return !g_open_docs.empty();
}

// --- World-space text layer (§7) state ---
// One submitted text item (physical-px top-left + utf8 + 0xRRGGBBAA colour).
struct world_text_item {
    float x = 0.f;
    float y = 0.f;
    std::string text;
    unsigned int rgba = 0xFFFFFFFFu;
};
// This frame's submitted items (cleared by world_text_begin, drained next frame).
std::vector<world_text_item> g_world_text;
// Geometry compiled from g_world_text in prepare(), drawn in render_in_pass().
// Rml::Geometry is a move-only render resource; clearing this vector releases the
// GPU geometry (deferred free, rolled forward by g_render->begin_frame()).
struct world_text_geom {
    Rml::Geometry geom;
    Rml::Texture texture;
    Rml::Vector2f pos;
};
std::vector<world_text_geom> g_world_geom;
// World-text tuning (F4 sliders; bake the dialed-in values once settled).
int g_world_text_px = 24;     // font point size
float g_world_text_dx = 0.f;  // extra x offset (px); + shifts right
float g_world_text_dy = 0.f;  // extra y offset (px); + shifts down
// World text resolves its font via a DIRECT GetFontFaceHandle lookup (no fallback
// resolution like document text gets), so the font is registered under this
// explicit family at init from these retained bytes (RmlUi keeps the data span
// until Shutdown). Looking up the TTF's embedded family name directly fails.
const char *const WORLD_TEXT_FAMILY = "cata-world-text";
std::vector<char> g_world_font_data;

bool world_text_have()
{
    return g_ready && g_context && !g_world_text.empty();
}

// Build (compile) geometry for this frame's world-text items via RmlUi's own
// FontEngine + RenderManager. Called from prepare() (outside the render pass) so
// the geometry uploads with the documents' in upload_pending(). Safe to call with
// no items (clears + returns).
void build_world_text()
{
    g_world_geom.clear();
    if( !world_text_have() ) {
        return;
    }
    Rml::FontEngineInterface *fe = Rml::GetFontEngineInterface();
    if( !fe ) {
        return;
    }
    Rml::RenderManager &rm = g_context->GetRenderManager();
    // Look up our explicitly-registered family (see init). Auto weight matches the
    // bundled Terminus (registered as "Medium" / 500).
    const Rml::FontFaceHandle face = fe->GetFontFaceHandle(
            WORLD_TEXT_FAMILY, Rml::Style::FontStyle::Normal,
            Rml::Style::FontWeight::Auto, g_world_text_px );
    if( face == 0 ) {
        DebugLog( DL::Info, DC::Main ) << "world_text: GetFontFaceHandle returned 0 (font not found)";
        return;
    }
    for( const world_text_item &it : g_world_text ) {
        const Rml::byte r = ( it.rgba >> 24 ) & 0xFFu;
        const Rml::byte g = ( it.rgba >> 16 ) & 0xFFu;
        const Rml::byte b = ( it.rgba >> 8 ) & 0xFFu;
        const Rml::byte a = it.rgba & 0xFFu;
        // Premultiplied alpha (render interface uses premult blend).
        const Rml::ColourbPremultiplied col(
            static_cast<Rml::byte>( r * a / 255 ),
            static_cast<Rml::byte>( g * a / 255 ),
            static_cast<Rml::byte>( b * a / 255 ), a );
        static const Rml::String world_text_lang;
        const Rml::TextShapingContext shaping{ world_text_lang };
        Rml::TexturedMeshList meshes;
        fe->GenerateString( rm, face, Rml::FontEffectsHandle( 0 ), it.text,
                            Rml::Vector2f( 0.f, 0.f ), col, 1.0f,
                            shaping, meshes );
        for( Rml::TexturedMesh &tm : meshes ) {
            world_text_geom out;
            out.geom = rm.MakeGeometry( std::move( tm.mesh ) );
            out.texture = tm.texture;
            // GenerateString lays text on the baseline at y=0; nudge down by the
            // point size so screen_y reads as the text's top edge, plus the F4 offsets.
            out.pos = Rml::Vector2f( it.x + g_world_text_dx,
                                     it.y + static_cast<float>( g_world_text_px ) + g_world_text_dy );
            g_world_geom.push_back( std::move( out ) );
        }
    }
    // Compile pass: a Render() with no open pass only compiles geometry CPU-side
    // (begin_render_pass not called yet) so upload_pending() can upload it. The
    // real draw happens again in render_in_pass() with the pass open.
    for( world_text_geom &w : g_world_geom ) {
        w.geom.Render( w.pos, w.texture );
    }
}

// Stylesheet preprocessor: a FileInterface that substitutes {{theme-tokens}} in
// .rcss content at load (RmlUi 6.2 has no native CSS variables). Other files pass
// through to plain fopen, exactly like RmlUi's default interface. In-memory .rcss
// handles are tagged in `mem_handles` so the stream methods serve the substituted
// buffer instead of the FILE*.
struct mem_file {
    std::string data;
    std::size_t pos = 0;
};

class theme_file_interface : public Rml::FileInterface
{
    public:
        Rml::FileHandle Open( const Rml::String &path ) override {
            std::FILE *fp = std::fopen( path.c_str(), "rb" );
            if( fp == nullptr ) {
                return 0;
            }
            const bool is_rcss = path.size() >= 5 &&
                                 path.compare( path.size() - 5, 5, ".rcss" ) == 0;
            if( !is_rcss ) {
                return reinterpret_cast<Rml::FileHandle>( fp );
            }
            std::string buf;
            std::fseek( fp, 0, SEEK_END );
            const long len = std::ftell( fp );
            std::fseek( fp, 0, SEEK_SET );
            if( len > 0 ) {
                buf.resize( static_cast<std::size_t>( len ) );
                const std::size_t rd = std::fread( &buf[0], 1, buf.size(), fp );
                buf.resize( rd );
            }
            std::fclose( fp );
            ui_theme::substitute_tokens( buf );
            mem_file *mf = new mem_file{ std::move( buf ), 0 };
            const Rml::FileHandle h = reinterpret_cast<Rml::FileHandle>( mf );
            mem_handles.insert( h );
            return h;
        }
        void Close( Rml::FileHandle file ) override {
            const auto it = mem_handles.find( file );
            if( it != mem_handles.end() ) {
                mem_handles.erase( it );
                delete reinterpret_cast<mem_file *>( file );
            } else {
                std::fclose( reinterpret_cast<std::FILE *>( file ) );
            }
        }
        std::size_t Read( void *buffer, std::size_t size, Rml::FileHandle file ) override {
            if( is_mem( file ) ) {
                mem_file *mf = reinterpret_cast<mem_file *>( file );
                const std::size_t n = std::min( size, mf->data.size() - mf->pos );
                std::memcpy( buffer, mf->data.data() + mf->pos, n );
                mf->pos += n;
                return n;
            }
            return std::fread( buffer, 1, size, reinterpret_cast<std::FILE *>( file ) );
        }
        bool Seek( Rml::FileHandle file, long offset, int origin ) override {
            if( is_mem( file ) ) {
                mem_file *mf = reinterpret_cast<mem_file *>( file );
                long base = 0;
                if( origin == SEEK_CUR ) {
                    base = static_cast<long>( mf->pos );
                } else if( origin == SEEK_END ) {
                    base = static_cast<long>( mf->data.size() );
                }
                const long np = base + offset;
                if( np < 0 || np > static_cast<long>( mf->data.size() ) ) {
                    return false;
                }
                mf->pos = static_cast<std::size_t>( np );
                return true;
            }
            return std::fseek( reinterpret_cast<std::FILE *>( file ), offset, origin ) == 0;
        }
        std::size_t Tell( Rml::FileHandle file ) override {
            if( is_mem( file ) ) {
                return reinterpret_cast<mem_file *>( file )->pos;
            }
            return static_cast<std::size_t>( std::ftell( reinterpret_cast<std::FILE *>( file ) ) );
        }
    private:
        std::unordered_set<Rml::FileHandle> mem_handles;
        bool is_mem( Rml::FileHandle f ) const {
            return mem_handles.count( f ) > 0;
        }
};

}  // namespace

bool init( lighting::gpu_device &dev )
{
    if( g_attempted ) {
        return g_ready;
    }
    if( !dev.ready() ) {
        dbg( DL::Info ) << "rmlui_layer: init skipped (device not ready)";
        return false;
    }
    g_attempted = true;
    g_window = dev.window_ptr();

    // Fail-safe: any failure degrades to "no RmlUi", never crashes the game.
    g_render = std::make_unique<lighting::rmlui_render_interface>();
    g_system = std::make_unique<lighting::rmlui_system_interface>();

    // Build the render interface's GPU resources (pipeline, white texture,
    // sampler) before RmlUi can call into it, on the live render_state device.
    if( !g_render->init( dev ) ) {
        dbg( DL::Error ) << "rmlui_layer: render interface init failed";
        g_render.reset();
        g_system.reset();
        return false;
    }

    Rml::SetRenderInterface( g_render.get() );
    Rml::SetSystemInterface( g_system.get() );

    // Load the theme tokens and install the stylesheet preprocessor BEFORE
    // Initialise, so the very first document's .rcss gets {{token}} substitution.
    ui_theme::load();
    static theme_file_interface g_file_iface;
    Rml::SetFileInterface( &g_file_iface );

    if( !Rml::Initialise() ) {
        dbg( DL::Error ) << "rmlui_layer: Rml::Initialise failed";
        g_render->shutdown();
        g_render.reset();
        g_system.reset();
        return false;
    }

    int win_w = 0;
    int win_h = 0;
    SDL_GetWindowSizeInPixels( g_window, &win_w, &win_h );
    g_context = Rml::CreateContext( "main", Rml::Vector2i( win_w, win_h ) );
    if( g_context == nullptr ) {
        dbg( DL::Error ) << "rmlui_layer: CreateContext failed";
        Rml::Shutdown();
        g_render->shutdown();
        g_render.reset();
        g_system.reset();
        return false;
    }

    // Registered as a fallback (second arg) so glyphs still resolve even if a
    // document's font-family doesn't match. Family name embedded in the TTF is
    // "Terminus (TTF)" — the RCSS must use that exact string.
    const std::string font = PATH_INFO::fontdir() + "Terminus.ttf";
    if( !Rml::LoadFontFace( font, true ) ) {
        // Non-fatal — boxes/colours still render; only text would be missing.
        dbg( DL::Warn ) << "rmlui_layer: LoadFontFace failed for " << font;
    }

    // Also register the same font from memory under an explicit family name for
    // the §7 world-text layer, which resolves via a direct GetFontFaceHandle()
    // lookup (no document-style fallback resolution). The bytes must stay alive
    // until Shutdown, so they live in the file-scope g_world_font_data.
    {
        std::ifstream wf( font, std::ios::binary );
        g_world_font_data.assign( std::istreambuf_iterator<char>( wf ),
                                  std::istreambuf_iterator<char>() );
        if( g_world_font_data.empty() ||
            !Rml::LoadFontFace(
                Rml::Span<const Rml::byte>(
                    reinterpret_cast<const Rml::byte *>( g_world_font_data.data() ),
                    g_world_font_data.size() ),
                WORLD_TEXT_FAMILY, Rml::Style::FontStyle::Normal,
                Rml::Style::FontWeight::Auto, false ) ) {
            dbg( DL::Warn ) << "rmlui_layer: world-text font register failed";
        }
    }

    g_ready = true;
    dbg( DL::Info ) << "rmlui_layer: init ok (" << win_w << "x" << win_h << ")";
    return true;
}

void shutdown()
{
    if( !g_attempted ) {
        return;
    }
    // Tear RmlUi down before the interfaces it points at, then release the
    // render interface's GPU resources before the device is destroyed.
    if( g_ready ) {
        Rml::Shutdown();
    }
    g_open_docs.clear();
    g_context = nullptr;
    if( g_render ) {
        g_render->shutdown();
    }
    g_render.reset();
    g_system.reset();
    g_window = nullptr;
    g_ready = false;
    g_attempted = false;
}

bool ready()
{
    return g_ready;
}

bool active()
{
    return g_ready && any_open();
}

Rml::Context *context()
{
    return g_ready ? g_context : nullptr;
}

float density_ratio()
{
    return g_density_ratio;
}

float &ui_scale()
{
    return g_ui_scale;
}

crt_params &crt()
{
    return g_crt;
}

void reload_theme()
{
    Rml::Factory::ClearStyleSheetCache();
    for( Rml::ElementDocument *doc : g_open_docs ) {
        if( doc != nullptr ) {
            doc->ReloadStyleSheet();
        }
    }
}

Rml::ElementDocument *open_document( const std::string &rml_path )
{
    if( !g_ready || g_context == nullptr ) {
        return nullptr;
    }
    Rml::ElementDocument *doc = g_context->LoadDocument( rml_path );
    if( doc == nullptr ) {
        dbg( DL::Warn ) << "rmlui_layer: LoadDocument failed for " << rml_path;
        return nullptr;
    }
    doc->Show();
    g_open_docs.push_back( doc );
    dbg( DL::Info ) << "rmlui_layer: opened document " << rml_path
                    << " (" << g_open_docs.size() << " open)";
    return doc;
}

void close_document( Rml::ElementDocument *doc )
{
    if( doc == nullptr || g_context == nullptr ) {
        return;
    }
    const auto it = std::find( g_open_docs.begin(), g_open_docs.end(), doc );
    if( it == g_open_docs.end() ) {
        return;  // already closed / not ours
    }
    g_open_docs.erase( it );
    doc->Hide();
    g_context->UnloadDocument( doc );
}

namespace
{
int mod_state()
{
    const SDL_Keymod m = SDL_GetModState();
    int s = 0;
    if( m & SDL_KMOD_CTRL ) {
        s |= Rml::Input::KM_CTRL;
    }
    if( m & SDL_KMOD_SHIFT ) {
        s |= Rml::Input::KM_SHIFT;
    }
    if( m & SDL_KMOD_ALT ) {
        s |= Rml::Input::KM_ALT;
    }
    if( m & SDL_KMOD_GUI ) {
        s |= Rml::Input::KM_META;
    }
    return s;
}
}  // namespace

bool process_event( const SDL_Event &ev )
{
    if( !g_ready || !any_open() || g_context == nullptr ) {
        return false;
    }
    // Mouse only: keyboard belongs to the game's input_context, which drives
    // menu navigation. We forward mouse so :hover and row click events work.
    // Context is sized in physical pixels; SDL mouse coords are in window points.
    float sx = 1.f;
    float sy = 1.f;
    if( g_window != nullptr ) {
        int pw = 0;
        int ph = 0;
        int ww = 0;
        int wh = 0;
        SDL_GetWindowSizeInPixels( g_window, &pw, &ph );
        SDL_GetWindowSize( g_window, &ww, &wh );
        if( ww > 0 ) {
            sx = static_cast<float>( pw ) / ww;
        }
        if( wh > 0 ) {
            sy = static_cast<float>( ph ) / wh;
        }
    }
    const int mods = mod_state();
    switch( ev.type ) {
        case SDL_EVENT_MOUSE_MOTION:
            g_context->ProcessMouseMove( static_cast<int>( ev.motion.x * sx ),
                                         static_cast<int>( ev.motion.y * sy ), mods );
            return true;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const int btn = ev.button.button == SDL_BUTTON_LEFT ? 0
                            : ev.button.button == SDL_BUTTON_RIGHT ? 1
                            : ev.button.button == SDL_BUTTON_MIDDLE ? 2 : 3;
            if( ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ) {
                g_context->ProcessMouseButtonDown( btn, mods );
            } else {
                g_context->ProcessMouseButtonUp( btn, mods );
            }
            return true;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            // SDL: +y scrolls up; RmlUi: +delta scrolls down.
            g_context->ProcessMouseWheel( -ev.wheel.y, mods );
            return true;
        default:
            // Keyboard / text / everything else: let the game handle it.
            return false;
    }
}

namespace
{
// Authentic non-uniform CRT flicker — the aleclownes.com/2017 reference opacity
// table (20 steps, random-ish). Stepped (not interpolated) for the stutter; the
// `flicker` slider lerps from 1.0 (off) toward these values (mean ~0.5).
constexpr float CRT_FLICKER[20] = {
    0.27861f, 0.34769f, 0.23604f, 0.90626f, 0.18128f,
    0.83891f, 0.65583f, 0.67807f, 0.26559f, 0.84693f,
    0.96019f, 0.08594f, 0.20313f, 0.71988f, 0.53455f,
    0.37288f, 0.71428f, 0.70419f, 0.70030f, 0.24387f,
};

// 0..1 -> 0..255 alpha byte for an #rrggbbaa colour.
unsigned crt_a255( float a01 )
{
    return static_cast<unsigned>( std::lround( std::clamp( a01, 0.0f, 1.0f ) * 255.0f ) );
}

// Apply the F4 CRT knobs to every open document as inline RCSS gradient decorators,
// before Update() so they lay out this frame. Scanlines = repeating-linear-gradient
// on #crt-overlay (drawn on top, sized to the window in explicit px since an abspos
// width/height:100% resolves to zero here); the scroll is baked into the stop
// offsets (phase). Vignette = radial-gradient on every .panel.
void apply_crt()
{
    const double t = g_system ? g_system->GetElapsedTime() : 0.0;

    char vignette[160];
    if( g_crt.enabled ) {
        ( void )std::snprintf( vignette, sizeof( vignette ),
                               "radial-gradient( farthest-corner, #00000000, #000000%02x )",
                               crt_a255( g_crt.vignette_alpha ) );
    } else {
        ( void )std::snprintf( vignette, sizeof( vignette ), "none" );
    }

    for( Rml::ElementDocument *doc : g_open_docs ) {
        if( doc == nullptr ) {
            continue;
        }
        Rml::ElementList panels;
        doc->GetElementsByClassName( panels, "panel" );
        for( Rml::Element *pe : panels ) {
            pe->SetProperty( "decorator", vignette );
        }
        Rml::Element *overlay = doc->GetElementById( "crt-overlay" );
        if( overlay == nullptr ) {
            continue;
        }
        // Mask the scanlines to the active panel (the "device screen"): size the
        // overlay to the panel's border-box rect, not the whole window. Geometry is
        // from the previous frame's layout (apply runs before Update), but panels
        // don't move so the 1-frame lag is invisible; a zero box (first frame /
        // unlaid-out) just hides the overlay until it settles.
        Rml::Element *panel = panels.empty() ? nullptr : panels.front();
        Rml::Vector2f poff;
        Rml::Vector2f psz;
        if( panel != nullptr ) {
            poff = panel->GetAbsoluteOffset( Rml::BoxArea::Border );
            psz = panel->GetBox().GetSize( Rml::BoxArea::Border );
        }
        if( !g_crt.enabled || panel == nullptr || psz.x <= 0.f || psz.y <= 0.f ) {
            overlay->SetProperty( "display", "none" );
            continue;
        }
        const float pitch = std::max( 2.0f, g_crt.scanline_pitch );
        const float thick = std::clamp( g_crt.scanline_thickness, 0.5f, pitch - 0.5f );
        const float gap = pitch - thick;
        const float phase = static_cast<float>( std::fmod( t * g_crt.roll_speed, pitch ) );
        const unsigned sa = crt_a255( g_crt.scanline_alpha );
        // Flicker = a uniform black layer (under the scanlines) whose alpha pulses
        // per the random table. Modulating a thin scanline's opacity is invisible;
        // pulsing a full-panel tint reads as whole-screen CRT flicker. 0 -> no pulse.
        const int idx = static_cast<int>( std::fmod( t * 24.0, 20.0 ) );
        const float ftab = CRT_FLICKER[std::clamp( idx, 0, 19 )];
        const unsigned fa = crt_a255( g_crt.flicker * ( 1.0f - ftab ) );
        char dec[320];
        ( void )std::snprintf( dec, sizeof( dec ),
                               "linear-gradient( 0deg, #000000%02x, #000000%02x ), "
                               "repeating-linear-gradient( 0deg, "
                               "#00000000 %.2fpx, #00000000 %.2fpx, "
                               "#000000%02x %.2fpx, #000000%02x %.2fpx )",
                               fa, fa, phase, phase + gap, sa, phase + gap, sa, phase + pitch );
        overlay->SetProperty( "display", "block" );
        overlay->SetProperty( "position", "absolute" );
        overlay->SetProperty( "left", std::to_string( poff.x ) + "px" );
        overlay->SetProperty( "top", std::to_string( poff.y ) + "px" );
        overlay->SetProperty( "width", std::to_string( psz.x ) + "px" );
        overlay->SetProperty( "height", std::to_string( psz.y ) + "px" );
        overlay->SetProperty( "decorator", dec );
        overlay->SetProperty( "opacity", "1.0" );
    }
}
}  // namespace

void new_frame()
{
    if( !g_ready ) {
        return;
    }
    // Drain deferred GPU-resource frees every frame, even with nothing open.
    g_render->begin_frame();
    if( any_open() && g_context != nullptr && g_window != nullptr ) {
        int w = 0;
        int h = 0;
        SDL_GetWindowSizeInPixels( g_window, &w, &h );
        if( w > 0 && h > 0 ) {
            g_context->SetDimensions( Rml::Vector2i( w, h ) );
            // Context is sized in PHYSICAL pixels so the GPU projection matches
            // the framebuffer. Set the density ratio = physical / logical so RCSS
            // `dp` units scale with HiDPI — without this, px-sized fonts render at
            // half the intended size on a 2x retina display (the "tiny font"). The
            // mouse sx/sy scaling in process_event handles the inverse for input.
            int lw = 0;
            int lh = 0;
            SDL_GetWindowSize( g_window, &lw, &lh );
            if( lw > 0 ) {
                g_density_ratio = static_cast<float>( w ) / static_cast<float>( lw );
                g_context->SetDensityIndependentPixelRatio( g_density_ratio * g_ui_scale );
            }
        }
        apply_crt();
        g_context->Update();
    }
}

void prepare( SDL_GPUCommandBuffer *cb )
{
    const bool doc = g_ready && any_open() && g_context != nullptr;
    const bool wt = world_text_have();
    if( !doc && !wt ) {
        return;
    }
    // Pre-render OUTSIDE the render pass so geometry compiles immediately (not
    // deferred by begin_render_pass). Then upload_pending uploads the compiled
    // data to GPU buffers. The real render pass's ctx->Render() reuses the cached
    // handles and renders on this same frame. World text compiles the same way.
    if( doc ) {
        g_context->Render();
    }
    if( wt ) {
        build_world_text();
    }
    g_render->upload_pending( cb );
}

void render_in_pass( SDL_GPURenderPass *rp, SDL_GPUCommandBuffer *cb )
{
    const bool doc = any_open();
    const bool wt = !g_world_geom.empty();
    if( !g_ready || g_window == nullptr || g_context == nullptr || ( !doc && !wt ) ) {
        return;
    }
    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels( g_window, &w, &h );
    const auto uw = static_cast<std::uint32_t>( w > 0 ? w : 1 );
    const auto uh = static_cast<std::uint32_t>( h > 0 ? h : 1 );
    // RmlUi context is sized in physical pixels, so projection == target.
    g_render->begin_render_pass( rp, cb, uw, uh, uw, uh );
    // World text draws FIRST so menu documents composite on top of it (SCT sits
    // on the map, under any open UI).
    for( world_text_geom &item : g_world_geom ) {
        item.geom.Render( item.pos, item.texture );
    }
    if( doc ) {
        g_context->Render();  // all shown documents, z-ordered by open order
    }
    g_render->end_render_pass();

    // D3D12 in-pass-upload watch: a non-zero textures count means a glyph atlas
    // uploaded mid-pass (the documented hazard). Geometry is deferred so a
    // non-zero compiles count is safe. Logs only on change.
    static std::uint32_t last_c = 0;
    static std::uint32_t last_t = 0;
    const std::uint32_t c = g_render->compiles_in_pass();
    const std::uint32_t t = g_render->textures_in_pass();
    if( c != last_c || t != last_t ) {
        last_c = c;
        last_t = t;
        DebugLogFL( DL::Info, DC::Main )
                << "rmlui in-pass uploads: compiles=" << c << " textures=" << t
                << " (0 = safe; geometry deferred, textures are the D3D12 watch)";
    }
}

void world_text_begin()
{
    g_world_text.clear();
}

void world_text_add( float screen_x, float screen_y, const std::string &utf8,
                     unsigned int rgba )
{
    if( utf8.empty() ) {
        return;
    }
    g_world_text.push_back( world_text_item{ screen_x, screen_y, utf8, rgba } );
}

bool world_text_active()
{
    return world_text_have();
}

int &world_text_px()
{
    return g_world_text_px;
}

float &world_text_dx()
{
    return g_world_text_dx;
}

float &world_text_dy()
{
    return g_world_text_dy;
}

}  // namespace rmlui_layer
