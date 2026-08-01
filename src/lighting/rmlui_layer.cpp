#include "rmlui_layer.h"

#include "debug.h"
#include "gpu_device.h"
#include "path_info.h"
#include "menu_plexus.h"
#include "rml_length.h"
#include "rmlui_proc_texture.h"
#include "rmlui_render_interface.h"
#include "rmlui_system_interface.h"
#include "ui_theme.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Debugger.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

// Lighting/ files must define dbg themselves (not globally available).
#define dbg(x) DebugLogFL((x), DC::SDL)

// --- Runic-frame debug knobs (see process_event: F9/F10/F11/F12) ---
// FRAME_INSET as a live knob; F9/F10 nudge it so the frame placement can be
// dialled in-game. g_frame_markers overlays a magenta cross at the panel-box
// centre (so the rune cluster's centring vs the panel vs the screen is visible).
// g_rml_debugger toggles RmlUi's built-in element inspector.
static bool g_frame_markers = false;
static bool g_rml_debugger = false;
// Set once the Debugger plugin has been Initialise()d, which happens on the first
// F11 rather than at context creation — Initialise makes its beacon element
// visible immediately and permanently.
static bool g_rml_debugger_ready = false;

namespace rmlui_layer {

namespace {

bool g_ready = false;
// init() tried once (success or failure); don't re-attempt every frame.
bool g_attempted = false;
// Window kept for per-frame context sizing + the render-pass projection.
SDL_Window* g_window = nullptr;

// Interfaces are held by raw pointer inside RmlUi, so they must outlive
// Rml::Shutdown(). Owned here; destroyed AFTER Rml::Shutdown() in shutdown().
std::unique_ptr<lighting::rmlui_render_interface> g_render;
std::unique_ptr<lighting::rmlui_system_interface> g_system;

// Owned by RmlUi core; destroyed by Rml::Shutdown(). Not deleted manually.
Rml::Context* g_context = nullptr;

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
std::vector<Rml::ElementDocument*> g_open_docs;

// Subset of g_open_docs that are PASSIVE (render-only, e.g. the Tier-7 sidebar
// HUD): they paint every frame but must NOT capture game input. A persistent
// passive doc keeps any_open() true (so the context renders) yet leaves
// any_interactive_open() false (so world mouse falls through to the game).
std::vector<Rml::ElementDocument*> g_passive_docs;

bool any_open() { return !g_open_docs.empty(); }

// True iff at least one open doc is INTERACTIVE (not in g_passive_docs). Modal
// screens are interactive; the HUD is not. process_event gates mouse capture on
// this so an always-open HUD doesn't swallow look/examine clicks.
bool any_interactive_open() {
    for (Rml::ElementDocument* doc : g_open_docs) {
        if (std::find(g_passive_docs.begin(), g_passive_docs.end(), doc) == g_passive_docs.end()) {
            return true;
        }
    }
    return false;
}

// --- World-space text layer (§7) state ---
// One submitted text item (physical-px top-left + utf8 + 0xRRGGBBAA colour).
struct world_text_item {
    float x = 0.f;
    float y = 0.f;
    std::string text;
    unsigned int rgba = 0xFFFFFFFFu;
};
// Floating combat text items (Phase 5): arcing damage/healing numbers.
struct combat_text_item {
    float x = 0.f, y = 0.f;       // current screen position (logical px)
    float ox = 0.f, oy = 0.f;     // initial position
    std::string text;
    unsigned int rgba = 0xFFFFFFFFu;
    float font_scale = 1.0f;
    float lifetime_ms = 1200.f;
    float age_ms = 0.f;
    float vx = 0.f, vy = -30.f;   // velocity px/sec
    float ay = 5.f;               // gravity px/sec^2
};
std::vector<combat_text_item> g_combat_text;
// This frame's submitted items (cleared by world_text_begin, drained next frame).
std::vector<world_text_item> g_world_text;
// Persistent single HUD line (e.g. FPS overlay). SET each frame via set_hud_text
// (not appended) so it never accumulates, and kept OUTSIDE the world_text
// begin/clear cycle so it renders every frame regardless of menus or combat text.
world_text_item g_hud_text;
bool g_hud_active = false;
// Geometry compiled from g_world_text in prepare(), drawn in render_in_pass().
// Rml::Geometry is a move-only render resource; clearing this vector releases the
// GPU geometry (deferred free, rolled forward by g_render->begin_frame()).
struct world_text_geom {
    Rml::Geometry geom;
    Rml::Texture texture;
    Rml::Vector2f pos;
};
std::vector<world_text_geom> g_world_geom;

// --- Plexus background (main menu only) ------------------------------------
// The plexus simulation runs on CPU (menu_plexus.cpp) producing an RGBA pixel
// buffer.  We upload it as a raw TextureHandle and draw a compiled fullscreen
// quad BEFORE world text and documents so the semi-transparent menu panel
// composites on top.  Uses the low-level RenderInterface directly (Rml::Texture
// has private constructors; only RenderManager/CallbackTexture can make one).
Rml::CompiledGeometryHandle g_plexus_geom_handle = 0;
Rml::TextureHandle g_plexus_tex_handle = 0;
unsigned g_plexus_uploaded_gen = 0;
int g_plexus_tex_w = 0;
int g_plexus_tex_h = 0;

auto plexus_active() -> bool {
    return g_ready && g_context && lighting::g_plexus_visible &&
           lighting::plexus_width() > 0 && lighting::plexus_height() > 0;
}

// Advance the plexus simulation (wall-clock gated) and upload the pixel buffer
// to a GPU texture + (re)compile a fullscreen quad.  Called from prepare()
// (OUTSIDE the render pass) so stepping and uploading are atomic — the render
// pass always draws the freshest frame, independent of game-loop speed.
void rebuild_plexus_geom() {
    if( !plexus_active() ) { return; }

    // Wall-clock gate: advance the simulation at a fixed ~20fps regardless of
    // how fast the game loop or GPU frame rate runs.
    {
        static auto last_step = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        if( lighting::plexus_get_config().enabled &&
            now - last_step >= std::chrono::milliseconds( 50 ) ) {
            lighting::plexus_step();
            last_step = now;
        }
    }

    const unsigned gen = lighting::plexus_generation();
    const int pw = lighting::plexus_width();
    const int ph = lighting::plexus_height();
    if( gen == g_plexus_uploaded_gen && pw == g_plexus_tex_w && ph == g_plexus_tex_h ) {
        return;
    }
    // Release prior resources.
    if( g_plexus_tex_handle ) {
        g_render->ReleaseTexture( g_plexus_tex_handle );
        g_plexus_tex_handle = 0;
    }
    if( g_plexus_geom_handle ) {
        g_render->ReleaseGeometry( g_plexus_geom_handle );
        g_plexus_geom_handle = 0;
    }

    const auto &px = lighting::plexus_pixels();
    if( px.empty() ) { return; }
    g_plexus_tex_handle = g_render->GenerateTexture(
                              { reinterpret_cast<const Rml::byte *>( px.data() ),
                                static_cast<std::size_t>( pw * ph * 4 ) },
    { pw, ph } );
    if( !g_plexus_tex_handle ) { return; }

    // Quad spans the full physical screen; the GPU stretches the logical-res
    // texture via UVs 0-1.
    int sw = 0, sh = 0;
    SDL_GetWindowSizeInPixels( g_window, &sw, &sh );
    const auto fw = static_cast<float>( sw > 0 ? sw : pw );
    const auto fh = static_cast<float>( sh > 0 ? sh : ph );
    const Rml::ColourbPremultiplied white{ 255, 255, 255, 255 };
    const Rml::Vertex verts[] = {
        { { 0, 0 }, white, { 0, 0 } },
        { { fw, 0 }, white, { 1, 0 } },
        { { fw, fh }, white, { 1, 1 } },
        { { 0, fh }, white, { 0, 1 } },
    };
    const int idxs[] = { 0, 1, 2, 0, 2, 3 };
    g_plexus_geom_handle = g_render->CompileGeometry(
                               { verts, 4 }, { idxs, 6 } );

    g_plexus_uploaded_gen = gen;
    g_plexus_tex_w = pw;
    g_plexus_tex_h = ph;
}
// World-text tuning (F4 sliders; bake the dialed-in values once settled).
int g_world_text_px = 24;    // font point size
float g_world_text_dx = 0.f; // extra x offset (px); + shifts right
float g_world_text_dy = 0.f; // extra y offset (px); + shifts down
// World text resolves its font via a DIRECT GetFontFaceHandle lookup (no fallback
// resolution like document text gets), so the font is registered under this
// explicit family at init from these retained bytes (RmlUi keeps the data span
// until Shutdown). Looking up the TTF's embedded family name directly fails.
const char* const WORLD_TEXT_FAMILY = "cata-world-text";
std::vector<char> g_world_font_data;

bool world_text_have() { return g_ready && g_context && (!g_world_text.empty() || g_hud_active); }

// Build (compile) geometry for this frame's world-text items via RmlUi's own
// FontEngine + RenderManager. Called from prepare() (outside the render pass) so
// the geometry uploads with the documents' in upload_pending(). Safe to call with
// no items (clears + returns).
void build_world_text() {
    g_world_geom.clear();
    if (!world_text_have()) { return; }
    Rml::FontEngineInterface* fe = Rml::GetFontEngineInterface();
    if (!fe) { return; }
    Rml::RenderManager& rm = g_context->GetRenderManager();
    // Look up our explicitly-registered family (see init). Auto weight matches the
    // bundled Terminus (registered as "Medium" / 500).
    const Rml::FontFaceHandle face = fe->GetFontFaceHandle(
        WORLD_TEXT_FAMILY, Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Auto,
        g_world_text_px);
    if (face == 0) {
        DebugLog(DL::Info, DC::Main) << "world_text: GetFontFaceHandle returned 0 (font not found)";
        return;
    }
    auto emit = [&](const world_text_item& it) {
        const Rml::byte r = (it.rgba >> 24) & 0xFFu;
        const Rml::byte g = (it.rgba >> 16) & 0xFFu;
        const Rml::byte b = (it.rgba >> 8) & 0xFFu;
        const Rml::byte a = it.rgba & 0xFFu;
        // Premultiplied alpha (render interface uses premult blend).
        const Rml::ColourbPremultiplied
            col(static_cast<Rml::byte>(r * a / 255), static_cast<Rml::byte>(g * a / 255),
                static_cast<Rml::byte>(b * a / 255), a);
        static const Rml::String world_text_lang;
        const Rml::TextShapingContext shaping{world_text_lang};
        Rml::TexturedMeshList meshes;
        fe->GenerateString(
            rm, face, Rml::FontEffectsHandle(0), it.text, Rml::Vector2f(0.f, 0.f), col, 1.0f,
            shaping, meshes);
        for (Rml::TexturedMesh& tm : meshes) {
            world_text_geom out;
            out.geom = rm.MakeGeometry(std::move(tm.mesh));
            out.texture = tm.texture;
            // Callers submit screen_x/screen_y in LOGICAL (point) coordinates —
            // the same space the tile sprites use (proj = SDL_GetWindowSize). This
            // geometry, however, renders in the RmlUi context's PHYSICAL-pixel
            // projection (SDL_GetWindowSizeInPixels). Convert logical -> physical
            // via the density ratio so world text aligns with the map instead of
            // clustering toward the top-left on HiDPI displays.
            // GenerateString lays text on the baseline at y=0; nudge down by the
            // point size so screen_y reads as the text's top edge, plus the F4 offsets.
            out.pos = Rml::Vector2f(
                it.x * g_density_ratio + g_world_text_dx,
                it.y * g_density_ratio + static_cast<float>(g_world_text_px) + g_world_text_dy);
            g_world_geom.push_back(std::move(out));
        }
    };
    for (const world_text_item& it : g_world_text) { emit(it); }
    // Combat text: emit with per-item font_scale and age-based alpha fade.
    for( const combat_text_item &it : g_combat_text ) {
        // Age-based alpha: full opacity for first 60%, linear fade to 0 over last 40%.
        const float life_ratio = it.age_ms / it.lifetime_ms;
        const float fade_alpha = life_ratio < 0.6f ? 1.0f : 1.0f - ( life_ratio - 0.6f ) / 0.4f;
        const unsigned int faded_rgba = ( it.rgba & 0xFFFFFF00u )
                                        | ( static_cast<unsigned int>( ( it.rgba & 0xFFu ) * fade_alpha ) );
        const Rml::byte r = ( faded_rgba >> 24 ) & 0xFFu;
        const Rml::byte g = ( faded_rgba >> 16 ) & 0xFFu;
        const Rml::byte b = ( faded_rgba >> 8 ) & 0xFFu;
        const Rml::byte a = faded_rgba & 0xFFu;
        const Rml::ColourbPremultiplied col(
            static_cast<Rml::byte>( r * a / 255 ), static_cast<Rml::byte>( g * a / 255 ),
            static_cast<Rml::byte>( b * a / 255 ), a );
        static const Rml::String world_text_lang;
        const Rml::TextShapingContext shaping{ world_text_lang };
        Rml::TexturedMeshList meshes;
        // Scale the font face by font_scale (crits get 1.5x).
        const float scaled_px = g_world_text_px * it.font_scale;
        const Rml::FontFaceHandle scaled_face = fe->GetFontFaceHandle(
            WORLD_TEXT_FAMILY, Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Auto,
            scaled_px );
        if( scaled_face == 0 ) {
            continue;
        }
        fe->GenerateString( rm, scaled_face, Rml::FontEffectsHandle( 0 ), it.text,
                            Rml::Vector2f( 0.f, 0.f ), col, 1.0f, shaping, meshes );
        for( Rml::TexturedMesh &tm : meshes ) {
            world_text_geom out;
            out.geom = rm.MakeGeometry( std::move( tm.mesh ) );
            out.texture = tm.texture;
            out.pos = Rml::Vector2f(
                it.x * g_density_ratio + g_world_text_dx,
                it.y * g_density_ratio + static_cast<float>( scaled_px ) + g_world_text_dy );
            g_world_geom.push_back( std::move( out ) );
        }
    }
    if (g_hud_active) { emit(g_hud_text); }
    // Compile pass: a Render() with no open pass only compiles geometry CPU-side
    // (begin_render_pass not called yet) so upload_pending() can upload it. The
    // real draw happens again in render_in_pass() with the pass open.
    for (world_text_geom& w : g_world_geom) { w.geom.Render(w.pos, w.texture); }
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

class theme_file_interface: public Rml::FileInterface {
public:
    Rml::FileHandle Open(const Rml::String& path) override {
        std::FILE* fp = std::fopen(path.c_str(), "rb");
        if (fp == nullptr) { return 0; }
        const bool is_rcss = path.size() >= 5 && path.compare(path.size() - 5, 5, ".rcss") == 0;
        if (!is_rcss) { return reinterpret_cast<Rml::FileHandle>(fp); }
        std::string buf;
        std::fseek(fp, 0, SEEK_END);
        const long len = std::ftell(fp);
        std::fseek(fp, 0, SEEK_SET);
        if (len > 0) {
            buf.resize(static_cast<std::size_t>(len));
            const std::size_t rd = std::fread(&buf[0], 1, buf.size(), fp);
            buf.resize(rd);
        }
        std::fclose(fp);
        ui_theme::substitute_tokens(buf);
        mem_file* mf = new mem_file{std::move(buf), 0};
        const Rml::FileHandle h = reinterpret_cast<Rml::FileHandle>(mf);
        mem_handles.insert(h);
        return h;
    }
    void Close(Rml::FileHandle file) override {
        const auto it = mem_handles.find(file);
        if (it != mem_handles.end()) {
            mem_handles.erase(it);
            delete reinterpret_cast<mem_file*>(file);
        } else {
            std::fclose(reinterpret_cast<std::FILE*>(file));
        }
    }
    std::size_t Read(void* buffer, std::size_t size, Rml::FileHandle file) override {
        if (is_mem(file)) {
            mem_file* mf = reinterpret_cast<mem_file*>(file);
            const std::size_t n = std::min(size, mf->data.size() - mf->pos);
            std::memcpy(buffer, mf->data.data() + mf->pos, n);
            mf->pos += n;
            return n;
        }
        return std::fread(buffer, 1, size, reinterpret_cast<std::FILE*>(file));
    }
    bool Seek(Rml::FileHandle file, long offset, int origin) override {
        if (is_mem(file)) {
            mem_file* mf = reinterpret_cast<mem_file*>(file);
            long base = 0;
            if (origin == SEEK_CUR) {
                base = static_cast<long>(mf->pos);
            } else if (origin == SEEK_END) {
                base = static_cast<long>(mf->data.size());
            }
            const long np = base + offset;
            if (np < 0 || np > static_cast<long>(mf->data.size())) { return false; }
            mf->pos = static_cast<std::size_t>(np);
            return true;
        }
        return std::fseek(reinterpret_cast<std::FILE*>(file), offset, origin) == 0;
    }
    std::size_t Tell(Rml::FileHandle file) override {
        if (is_mem(file)) { return reinterpret_cast<mem_file*>(file)->pos; }
        return static_cast<std::size_t>(std::ftell(reinterpret_cast<std::FILE*>(file)));
    }

private:
    std::unordered_set<Rml::FileHandle> mem_handles;
    bool is_mem(Rml::FileHandle f) const { return mem_handles.count(f) > 0; }
};

} // namespace

bool init(lighting::gpu_device& dev) {
    if (g_attempted) { return g_ready; }
    if (!dev.ready()) {
        dbg(DL::Info) << "rmlui_layer: init skipped (device not ready)";
        return false;
    }
    g_attempted = true;
    g_window = dev.window_ptr();

    // Fail-safe: any failure degrades to "no RmlUi", never crashes the game.
    g_render = std::make_unique<lighting::rmlui_render_interface>();
    g_system = std::make_unique<lighting::rmlui_system_interface>();

    // Build the render interface's GPU resources (pipeline, white texture,
    // sampler) before RmlUi can call into it, on the live render_state device.
    if (!g_render->init(dev)) {
        dbg(DL::Error) << "rmlui_layer: render interface init failed";
        g_render.reset();
        g_system.reset();
        return false;
    }

    Rml::SetRenderInterface(g_render.get());
    Rml::SetSystemInterface(g_system.get());

    // Load the theme tokens and install the stylesheet preprocessor BEFORE
    // Initialise, so the very first document's .rcss gets {{token}} substitution.
    ui_theme::load();
    static theme_file_interface g_file_iface;
    Rml::SetFileInterface(&g_file_iface);

    if (!Rml::Initialise()) {
        dbg(DL::Error) << "rmlui_layer: Rml::Initialise failed";
        g_render->shutdown();
        g_render.reset();
        g_system.reset();
        return false;
    }

    int win_w = 0;
    int win_h = 0;
    SDL_GetWindowSizeInPixels(g_window, &win_w, &win_h);
    g_context = Rml::CreateContext("main", Rml::Vector2i(win_w, win_h));
    if (g_context == nullptr) {
        dbg(DL::Error) << "rmlui_layer: CreateContext failed";
        Rml::Shutdown();
        g_render->shutdown();
        g_render.reset();
        g_system.reset();
        return false;
    }

    // The built-in element inspector (box model, computed RCSS, live outlines) is
    // initialised LAZILY, on the first F11 press — see the SDLK_F11 handler.
    //
    // It used to be initialised here, unconditionally, and that was not free:
    // `Rml::Debugger::Initialise` creates the plugin's beacon element, which stays
    // visible even while `SetVisible(false)` hides the inspector itself. The result
    // was a permanent 20x20px saturated-yellow `!` badge in the top-right corner of
    // every screen, off the layout grid and in a colour from no palette in the
    // project. It went unnoticed for as long as that corner was empty; the phosphor
    // HUD puts its safe-mode and hostile readouts there, so the badge
    // began overlapping live text. Note it is NOT a warning indicator that could be
    // silenced by fixing warnings: the beacon is created by Initialise itself. (And
    // RmlUi's warnings would be hard to notice anyway — LogMessage routes them to
    // debug.log under DC::SDL, which is filtered out of the default debug-class set,
    // so they are dropped before they are written.)

    // Registered as a fallback (second arg) so glyphs still resolve even if a
    // document's font-family doesn't match. Family name embedded in the TTF is
    // "Terminus (TTF)" — the RCSS must use that exact string.
    const std::string font = PATH_INFO::fontdir() + "Terminus.ttf";
    if (!Rml::LoadFontFace(font, true)) {
        // Non-fatal — boxes/colours still render; only text would be missing.
        dbg(DL::Warn) << "rmlui_layer: LoadFontFace failed for " << font;
    }

    // Source Code Pro — bundled for the RmlUi HUD. Loaded non-fallback so the
    // HUD document can target it explicitly; Terminus remains the fallback.
    for (const char *f : { "SourceCodePro-Regular.ttf", "SourceCodePro-Semibold.ttf" }) {
        const std::string p = PATH_INFO::fontdir() + f;
        if (!Rml::LoadFontFace(p, false)) {
            dbg(DL::Warn) << "rmlui_layer: LoadFontFace failed for " << p;
        }
    }

    // Also register the same font from memory under an explicit family name for
    // the §7 world-text layer, which resolves via a direct GetFontFaceHandle()
    // lookup (no document-style fallback resolution). The bytes must stay alive
    // until Shutdown, so they live in the file-scope g_world_font_data.
    {
        std::ifstream wf(font, std::ios::binary);
        g_world_font_data
            .assign(std::istreambuf_iterator<char>(wf), std::istreambuf_iterator<char>());
        if (g_world_font_data.empty()
            || !Rml::LoadFontFace(
                Rml::Span<const Rml::byte>(
                    reinterpret_cast<const Rml::byte*>(g_world_font_data.data()),
                    g_world_font_data.size()),
                WORLD_TEXT_FAMILY, Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Auto,
                false)) {
            dbg(DL::Warn) << "rmlui_layer: world-text font register failed";
        }
    }

    g_ready = true;
    dbg(DL::Info) << "rmlui_layer: init ok (" << win_w << "x" << win_h << ")";
    return true;
}

void shutdown() {
    if (!g_attempted) { return; }
    // Tear RmlUi down before the interfaces it points at, then release the
    // render interface's GPU resources before the device is destroyed.
    if (g_ready) { Rml::Shutdown(); }
    g_open_docs.clear();
    g_passive_docs.clear();
    g_context = nullptr;
    // The Debugger plugin is bound to the context it was Initialise()d against, so
    // a new context needs a fresh Initialise. Without clearing this, F11 after a
    // renderer restart would skip it and the inspector would silently never appear.
    g_rml_debugger_ready = false;
    g_rml_debugger = false;
    if (g_render) { g_render->shutdown(); }
    g_render.reset();
    g_system.reset();
    g_window = nullptr;
    g_ready = false;
    g_attempted = false;
}

bool ready() { return g_ready; }

bool active() { return g_ready && any_open(); }

bool capturing_input() { return g_ready && any_interactive_open(); }

Rml::Context* context() { return g_ready ? g_context : nullptr; }

float density_ratio() { return g_density_ratio; }

float& ui_scale() { return g_ui_scale; }

crt_params& crt() { return g_crt; }

void reload_theme() {
    Rml::Factory::ClearStyleSheetCache();
    for (Rml::ElementDocument* doc : g_open_docs) {
        if (doc != nullptr) { doc->ReloadStyleSheet(); }
    }
}

Rml::ElementDocument* open_document(const std::string& rml_path, bool passive) {
    if (!g_ready || g_context == nullptr) { return nullptr; }
    Rml::ElementDocument* doc = g_context->LoadDocument(rml_path);
    if (doc == nullptr) {
        dbg(DL::Warn) << "rmlui_layer: LoadDocument failed for " << rml_path;
        return nullptr;
    }
    doc->Show();
    g_open_docs.push_back(doc);
    if (passive) { g_passive_docs.push_back(doc); }
    dbg(DL::Info) << "rmlui_layer: opened document " << rml_path << " (" << g_open_docs.size()
                  << " open)";
    return doc;
}

void close_document(Rml::ElementDocument* doc) {
    if (doc == nullptr || g_context == nullptr) { return; }
    const auto it = std::find(g_open_docs.begin(), g_open_docs.end(), doc);
    if (it == g_open_docs.end()) {
        return; // already closed / not ours
    }
    g_open_docs.erase(it);
    const auto pit = std::find(g_passive_docs.begin(), g_passive_docs.end(), doc);
    if (pit != g_passive_docs.end()) { g_passive_docs.erase(pit); }
    doc->Hide();
    g_context->UnloadDocument(doc);
}

scoped_documents_hidden::scoped_documents_hidden() {
    if (!g_ready) { return; }
    for (Rml::ElementDocument* doc : g_open_docs) {
        if (doc != nullptr && doc->IsVisible()) {
            doc->Hide();
            hidden_.push_back(doc);
        }
    }
}

scoped_documents_hidden::~scoped_documents_hidden() {
    for (Rml::ElementDocument* doc : hidden_) {
        // Skip anything closed while hidden — close_document() already unloaded it.
        if (std::find(g_open_docs.begin(), g_open_docs.end(), doc) != g_open_docs.end()) {
            doc->Show();
        }
    }
}

namespace {
int mod_state() {
    const SDL_Keymod m = SDL_GetModState();
    int s = 0;
    if (m & SDL_KMOD_CTRL) { s |= Rml::Input::KM_CTRL; }
    if (m & SDL_KMOD_SHIFT) { s |= Rml::Input::KM_SHIFT; }
    if (m & SDL_KMOD_ALT) { s |= Rml::Input::KM_ALT; }
    if (m & SDL_KMOD_GUI) { s |= Rml::Input::KM_META; }
    return s;
}
} // namespace

bool process_event(const SDL_Event& ev) {
    if (!g_ready || !any_open() || g_context == nullptr) { return false; }

    // Debug keys (F9-F12): always active, even with passive-only docs.
    if (ev.type == SDL_EVENT_KEY_DOWN) {
        switch (ev.key.key) {
            case SDLK_F9: {
                int& fi = lighting::runic_cfg().frame_inset;
                fi = fi > 0 ? fi - 1 : 0;
                dbg(DL::Info) << "rmlui frame_inset=" << fi;
                return true;
            }
            case SDLK_F10: {
                int& fi = lighting::runic_cfg().frame_inset;
                fi += 1;
                dbg(DL::Info) << "rmlui frame_inset=" << fi;
                return true;
            }
            case SDLK_F11:
                // Initialise on first use, not at context creation: the plugin's
                // beacon element is visible from the moment Initialise runs, even
                // with the inspector hidden, and it lands on top of whatever a
                // document draws in the top-right corner. See the note beside the
                // context creation above.
                if (!g_rml_debugger_ready) {
                    Rml::Debugger::Initialise(g_context);
                    g_rml_debugger_ready = true;
                }
                g_rml_debugger = !g_rml_debugger;
                Rml::Debugger::SetVisible(g_rml_debugger);
                dbg(DL::Info) << "rmlui debugger=" << g_rml_debugger;
                return true;
            case SDLK_F12:
                g_frame_markers = !g_frame_markers;
                dbg(DL::Info) << "rmlui frame_markers=" << g_frame_markers;
                return true;
            default:
                break;
        }
    }

    // Pixel scale for mouse coordinate conversion. Context is sized in physical
    // pixels; SDL mouse coords are in window points.
    float sx = 1.f;
    float sy = 1.f;
    if (g_window != nullptr) {
        int pw = 0;
        int ph = 0;
        int ww = 0;
        int wh = 0;
        SDL_GetWindowSizeInPixels(g_window, &pw, &ph);
        SDL_GetWindowSize(g_window, &ww, &wh);
        if (ww > 0) { sx = static_cast<float>(pw) / ww; }
        if (wh > 0) { sy = static_cast<float>(ph) / wh; }
    }
    const int mods = mod_state();

    // Mouse motion: always feed so RmlUi hover state stays current (needed for
    // scroll target resolution). Never consumed — game always gets it too.
    if (ev.type == SDL_EVENT_MOUSE_MOTION) {
        g_context->ProcessMouseMove(
            static_cast<int>(ev.motion.x * sx), static_cast<int>(ev.motion.y * sy), mods);
        return false;
    }

    // Mouse wheel: always feed to RmlUi so an interactive doc can scroll, but only
    // consume it when one is actually open. The phosphor HUD's log is sized to its
    // line count and does not scroll, so there is nothing here to protect from the
    // map zoom — this used to consume the wheel over `hud-log-body`, an element the
    // Qud-era scrolling log owned and which no longer exists.
    if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
        g_context->ProcessMouseWheel(-ev.wheel.y, mods);
        return any_interactive_open();
    }

    // Only PASSIVE docs open and debugger not visible? Fall through so the
    // game keeps clicks/keys. When the debugger is open, allow clicks through
    // so the user can interact with the debugger overlay.
    if (!any_interactive_open() && !g_rml_debugger) { return false; }

    // Interactive mouse handling.
    switch (ev.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const int btn =
                ev.button.button == SDL_BUTTON_LEFT    ? 0
                : ev.button.button == SDL_BUTTON_RIGHT ? 1
                : ev.button.button == SDL_BUTTON_MIDDLE
                    ? 2
                    : 3;
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                g_context->ProcessMouseMove(
                    static_cast<int>(ev.button.x * sx), static_cast<int>(ev.button.y * sy), mods);
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    for (Rml::Element* e = g_context->GetHoverElement(); e != nullptr;
                         e = e->GetParentNode()) {
                        if (e->GetId() == "runic-close") {
                            SDL_Event esc{};
                            esc.type = SDL_EVENT_KEY_DOWN;
                            esc.key.down = true;
                            esc.key.scancode = SDL_SCANCODE_ESCAPE;
                            esc.key.key = SDLK_ESCAPE;
                            SDL_PushEvent(&esc);
                            esc.type = SDL_EVENT_KEY_UP;
                            esc.key.down = false;
                            SDL_PushEvent(&esc);
                            return true;
                        }
                    }
                }
            }
            if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                return !g_context->ProcessMouseButtonDown(btn, mods);
            } else {
                return !g_context->ProcessMouseButtonUp(btn, mods);
            }
        }
        default:
            return false;
    }
}

namespace {
// Authentic non-uniform CRT flicker — the aleclownes.com/2017 reference opacity
// table (20 steps, random-ish). Stepped (not interpolated) for the stutter; the
// `flicker` slider lerps from 1.0 (off) toward these values (mean ~0.5).
constexpr float CRT_FLICKER[20] = {
    0.27861f, 0.34769f, 0.23604f, 0.90626f, 0.18128f, 0.83891f, 0.65583f,
    0.67807f, 0.26559f, 0.84693f, 0.96019f, 0.08594f, 0.20313f, 0.71988f,
    0.53455f, 0.37288f, 0.71428f, 0.70419f, 0.70030f, 0.24387f,
};

// 0..1 -> 0..255 alpha byte for an #rrggbbaa colour.
unsigned crt_a255(float a01) {
    return static_cast<unsigned>(std::lround(std::clamp(a01, 0.0f, 1.0f) * 255.0f));
}

// Options for composing a runic frame decorator string.
struct runic_frame_opts {
    int pw = 0; // panel border-box width (display px)
    int ph = 0; // panel border-box height (display px)
    int ring_disp = 0; // RUNE_RING * dr
    int need = 0; // minimum dimension threshold
    unsigned seed = 0;
    int t_h = 0; // horizontal template
    int t_v = 1; // vertical template
    unsigned g = 0; // regen cache-bust token
    int FRAME_INSET = 0;
    float dr = 1.0f;
    // Edge suppression: true = skip that edge's decorator segment
    bool no_top = false;
    bool no_bottom = false;
    bool no_left = false;
    bool no_right = false;
};

// Pixel lengths go through rml::px (rml_length.h) — never std::to_string or
// printf "%f", which honour LC_NUMERIC and emit "2,93px" on a comma-decimal
// locale.  Inside a gradient that comma reads as an argument separator, so
// RmlUi rejects the entire declaration.
using rml::px;

/// Compose the runic frame decorator string for a given element size.
/// Returns empty string if the element is too small for the frame.
auto compose_runic_frame( const runic_frame_opts &opts ) -> std::string
{
    if( opts.pw < opts.need || opts.ph < opts.need ) {
        return "";
    }

    const int hlen = static_cast<int>(std::lround((opts.pw - 2 * opts.FRAME_INSET) / opts.dr));
    const int vlen = static_cast<int>(std::lround((opts.ph - 2 * opts.FRAME_INSET) / opts.dr));
    const int far_x = opts.pw - opts.ring_disp - opts.FRAME_INSET;
    const int far_y = opts.ph - opts.ring_disp - opts.FRAME_INSET;


    // Build decorator segments
    std::string out;
    const char *sep = "";

    auto append = [&]( const char *fmt, auto ...args ) {
        if( out.empty() ) {
            sep = "";
        } else {
            sep = ", ";
        }
        char buf[512];
        std::snprintf( buf, sizeof(buf), fmt, args... );
        out += sep;
        out += buf;
    };

    // Edges
    if( !opts.no_top ) {
        append( "image( ?proc:runic-hedge:%d:%u:%d:G%u none scale-none %dpx %dpx ) border-box",
                hlen, opts.seed, opts.t_h, opts.g, opts.FRAME_INSET, opts.FRAME_INSET );
    }
    if( !opts.no_bottom ) {
        append( "image( ?proc:runic-hedge:%d:%u:%d:G%u flip-vertical scale-none %dpx %dpx ) border-box",
                hlen, opts.seed, opts.t_h, opts.g, opts.FRAME_INSET, far_y );
    }
    if( !opts.no_left ) {
        append( "image( ?proc:runic-vedge:%d:%u:%d:G%u none scale-none %dpx %dpx ) border-box",
                vlen, opts.seed, opts.t_v, opts.g, opts.FRAME_INSET, opts.FRAME_INSET );
    }
    if( !opts.no_right ) {
        append( "image( ?proc:runic-vedge:%d:%u:%d:G%u flip-horizontal scale-none %dpx %dpx ) border-box",
                vlen, opts.seed, opts.t_v, opts.g, far_x, opts.FRAME_INSET );
    }

    // Corners: original emits TL, BL, BR only (TR = #runic-close interactive button)
    // Suppression: corner omitted when EITHER adjacent edge is suppressed
    if( !( opts.no_top || opts.no_left ) ) {
        // TL
        append( "image( ?proc:runic-corner:G%u none scale-none %dpx %dpx ) border-box",
                opts.g, opts.FRAME_INSET, opts.FRAME_INSET );
    }
    if( !( opts.no_bottom || opts.no_left ) ) {
        // BL
        append( "image( ?proc:runic-corner:G%u flip-vertical scale-none %dpx %dpx ) border-box",
                opts.g, opts.FRAME_INSET, far_y );
    }
    if( !( opts.no_bottom || opts.no_right ) ) {
        // BR
        append( "image( ?proc:runic-corner:G%u rotate-180 scale-none %dpx %dpx ) border-box",
                opts.g, far_x, far_y );
    }

    return out;
}

// Apply the F4 CRT knobs to every open document as inline RCSS gradient decorators,
// before Update() so they lay out this frame. Scanlines = repeating-linear-gradient
// on #crt-overlay (drawn on top, sized to the window in explicit px since an abspos
// width/height:100% resolves to zero here); the scroll is baked into the stop
// offsets (phase). Vignette = radial-gradient on every .panel.
void apply_crt() {
    const double t = g_system ? g_system->GetElapsedTime() : 0.0;

    // Panel decorator = optional CRT vignette gradient + the procedural runic
    // frame, composed per panel. The frame is 8 stacked image() decorators on the
    // border-box: four edges + four corners (scale-none) anchored to each corner.
    // EDGES ARE PANEL-RELATIVE: the hedge/vedge texture is generated at the panel's
    // border-box length ("?proc:runic-hedge:<len>:<seed>") and placed ONCE
    // (scale-none, centre-aligned) rather than tiled, so each edge lays out
    // symmetrically about its centre (rule lines + rune groups). The seed is
    // derived from the panel size, so all four edges of a panel share one of three
    // layout templates and differently-sized panels differ. apply_crt drives
    // .panel's `decorator` inline every frame, overriding the stylesheet rule. The
    // 24dp transparent .panel border (theme.rcss) reserves the ring. image()
    // shorthand is SPACE-separated: image( src orientation fit align-x align-y ).
    //
    // FRAME_INSET pulls the whole frame inward by N px on every side, so the frame
    // is drawn as if for a rectangle inset by N: edges are generated at
    // (len - 2*INSET) and every piece is offset by INSET via px align (scale-none
    // tiles accept length align + clip). Because the panel-relative edges reserve
    // RING at each end for the corners, the corner<->edge abutment is preserved
    // automatically. Tuned so the rune band's centre (~depth 8 in the RING-deep
    // texture) lands on the panel's border-inner edge — the CRT "screen" rect.
    lighting::runic_params& rcfg = lighting::runic_cfg();
    const int RUNE_RING = rcfg.ring;          // matches the generator's corner/edge depth
    const int FRAME_INSET = rcfg.frame_inset; // inward shift (F9/F10 knob)
    // Tiled fallback for panels too small to inset (size unknown on first frame).
    constexpr char fallback[] =
        "image( ?proc:runic-hedge none repeat-x center top ) border-box, "
        "image( ?proc:runic-hedge flip-vertical repeat-x center bottom ) border-box, "
        "image( ?proc:runic-vedge none repeat-y left center ) border-box, "
        "image( ?proc:runic-vedge flip-horizontal repeat-y right center ) border-box, "
        "image( ?proc:runic-corner none scale-none left top ) border-box, "
        "image( ?proc:runic-corner flip-vertical scale-none left bottom ) border-box, "
        "image( ?proc:runic-corner rotate-180 scale-none right bottom ) border-box";

    for (Rml::ElementDocument* doc : g_open_docs) {
        if (doc == nullptr) { continue; }
        Rml::ElementList panels;
        doc->GetElementsByClassName(panels, "panel");
        for (Rml::Element* pe : panels) {
            // Border-box size from the previous frame's layout (apply runs before
            // Update); panels don't move, so the 1-frame lag is invisible.
            const Rml::Vector2f sz = pe->GetBox().GetSize(Rml::BoxArea::Border);
            const int pw = static_cast<int>(std::lround(sz.x));
            const int ph = static_cast<int>(std::lround(sz.y));
            static int dbg_sweeps = 0;
            if (dbg_sweeps < 12) {
                const Rml::Vector2f ofs = pe->GetAbsoluteOffset(Rml::BoxArea::Border);
                dbg(DL::Info) << "rmlui_frame panel id='" << pe->GetId() << "' tag='"
                              << pe->GetTagName() << "' box=" << pw << "x" << ph << " absofs="
                              << static_cast<int>(ofs.x) << "," << static_cast<int>(ofs.y);
                ++dbg_sweeps;
            }
            const float dr = g_density_ratio * g_ui_scale;
            const int ring_disp = static_cast<int>(std::lround(RUNE_RING * dr));
            const int need = 2 * (FRAME_INSET + ring_disp) + 1;
            const unsigned seed =
                rcfg.use_fixed_seed
                    ? rcfg.seed
                    : ((static_cast<unsigned>(pw) * 73856093u)
                       ^ (static_cast<unsigned>(ph) * 19349663u));
            const int t_h = rcfg.force_template >= 0 ? rcfg.force_template : 0;
            const int t_v = rcfg.force_template >= 0 ? rcfg.force_template : 1;
            const unsigned g = rcfg.regen;

            // Read edge suppression classes
            const bool no_top = pe->IsClassSet("runic-no-top");
            const bool no_bottom = pe->IsClassSet("runic-no-bottom");
            const bool no_left = pe->IsClassSet("runic-no-left");
            const bool no_right = pe->IsClassSet("runic-no-right");

            std::string frame_str = compose_runic_frame( runic_frame_opts {
                .pw = pw, .ph = ph, .ring_disp = ring_disp, .need = need,
                .seed = seed, .t_h = t_h, .t_v = t_v, .g = g,
                .FRAME_INSET = FRAME_INSET, .dr = dr,
                .no_top = no_top, .no_bottom = no_bottom,
                .no_left = no_left, .no_right = no_right
            } );

            char frame[2048];
            if( frame_str.empty() ) {
                (void)std::snprintf(frame, sizeof(frame), "%s", fallback);
            } else {
                (void)std::snprintf(frame, sizeof(frame), "%s", frame_str.c_str());
            }
            // F12: magenta cross at the panel-box centre (painted last = on top),
            // so the rune cluster's centring vs the panel vs the screen is visible.
            char markers[256] = "";
            if (g_frame_markers && pw >= need && ph >= need) {
                (void)std::snprintf(
                    markers, sizeof(markers),
                    ", image( ?proc:dbg-v:%d none scale-none %dpx 0px ) border-box"
                    ", image( ?proc:dbg-h:%d none scale-none 0px %dpx ) border-box",
                    ph, pw / 2 - 1, pw, ph / 2 - 1);
            }
            // Panel background keeps only the CRT vignette; the runic frame is
            // lifted onto a dedicated top element (#runic-frame) so it paints
            // ABOVE the #crt-overlay scanlines (true z-order) instead of as the
            // panel's background, which the later-painted overlay covered.
            if (g_crt.enabled) {
                char vig[96];
                (void)std::snprintf(
                    vig, sizeof(vig), "radial-gradient( farthest-corner, #00000000, #000000%02x )",
                    crt_a255(g_crt.vignette_alpha));
                pe->SetProperty("decorator", vig);
            } else {
                pe->SetProperty("decorator", "none");
            }
            // Top frame layer: created once per document, appended last and given a
            // z-index above .crt's 10, so it sits over the scanline overlay. Sized
            // and positioned to the panel's border-box, so the border-box-anchored
            // image() offsets resolve identically to the old panel decorator.
            Rml::Element* fr = doc->GetElementById("runic-frame");
            if (fr == nullptr) {
                Rml::ElementPtr fp = doc->CreateElement("div");
                fp->SetId("runic-frame");
                fr = doc->AppendChild(std::move(fp));
            }
            if (fr != nullptr) {
                const Rml::Vector2f foff = pe->GetAbsoluteOffset(Rml::BoxArea::Border);
                char fdec[2048];
                (void)std::snprintf(fdec, sizeof(fdec), "%s%s", frame, markers);
                fr->SetProperty("position", "absolute");
                fr->SetProperty("pointer-events", "none");
                fr->SetProperty("z-index", "11");
                fr->SetProperty("left", px(foff.x));
                fr->SetProperty("top", px(foff.y));
                fr->SetProperty("width", px(sz.x));
                fr->SetProperty("height", px(sz.y));
                fr->SetProperty("decorator", fdec);
            }
            // Interactive close button: a clickable X overlaying the top-right
            // corner ornament. Unlike #runic-frame (pointer-events:none) this
            // element opts back INTO hit-testing; process_event turns a left
            // click on it into a synthetic ESC, which the owning screen's
            // input_context reads as its cancel/close (keyboard owns menu input).
            // z-index 12 keeps it above the frame (11) and CRT overlay (10).
            const bool has_frame = pw >= need && ph >= need;
            Rml::Element* cl = doc->GetElementById("runic-close");
            if (has_frame) {
                if (cl == nullptr) {
                    Rml::ElementPtr cp = doc->CreateElement("div");
                    cp->SetId("runic-close");
                    cl = doc->AppendChild(std::move(cp));
                }
                if (cl != nullptr) {
                    // The TR corner piece: this element OWNS the encased-X art (the
                    // frame layer leaves the TR corner empty) and is hit-tested, so a
                    // click reads as close. On :hover it swaps to the inverted variant
                    // (solid ink box, X knocked out) to signal it is interactable.
                    const bool hov = cl->IsPseudoClassSet("hover");
                    const Rml::Vector2f coff = pe->GetAbsoluteOffset(Rml::BoxArea::Border);
                    const int far_x = pw - ring_disp - FRAME_INSET;
                    char xdec[160];
                    (void)std::snprintf(
                        xdec, sizeof(xdec),
                        "image( ?proc:%s:G%u none scale-none 0px 0px ) border-box",
                        hov ? "runic-x-inv" : "runic-x", rcfg.regen);
                    cl->SetProperty("position", "absolute");
                    cl->SetProperty("pointer-events", "auto");
                    cl->SetProperty("z-index", "12");
                    cl->SetProperty("left", px(coff.x + far_x));
                    cl->SetProperty("top", px(coff.y + FRAME_INSET));
                    cl->SetProperty("width", std::to_string(ring_disp) + "px");
                    cl->SetProperty("height", std::to_string(ring_disp) + "px");
                    cl->SetProperty("decorator", xdec);
                }
            } else if (cl != nullptr) {
                cl->SetProperty("decorator", "none");
                cl->SetProperty("pointer-events", "none");
            }
        }
        // Second sweep: per-element runic frames for HUD regions (class "runic-region").
        // Unlike .panel, these get NO #runic-close button and NO vignette. Frame layers
        // are per-element ("runic-frame-<id>") so each region composes independently.
        {
            Rml::ElementList regions;
            doc->GetElementsByClassName(regions, "runic-region");
            for (Rml::Element* reg : regions) {
                const Rml::Vector2f rsz = reg->GetBox().GetSize(Rml::BoxArea::Border);
                const int rpw = static_cast<int>(std::lround(rsz.x));
                const int rph = static_cast<int>(std::lround(rsz.y));
                const float dr = g_density_ratio * g_ui_scale;
                const int ring_disp = static_cast<int>(std::lround(RUNE_RING * dr));
                const int need = 2 * (FRAME_INSET + ring_disp) + 1;

                const unsigned seed =
                    rcfg.use_fixed_seed
                        ? rcfg.seed
                        : ((static_cast<unsigned>(rpw) * 73856093u)
                           ^ (static_cast<unsigned>(rph) * 19349663u));
                const int t_h = rcfg.force_template >= 0 ? rcfg.force_template : 0;
                const int t_v = rcfg.force_template >= 0 ? rcfg.force_template : 1;
                const unsigned g = rcfg.regen;

                const bool no_top = reg->IsClassSet("runic-no-top");
                const bool no_bottom = reg->IsClassSet("runic-no-bottom");
                const bool no_left = reg->IsClassSet("runic-no-left");
                const bool no_right = reg->IsClassSet("runic-no-right");

                std::string frame_str = compose_runic_frame( runic_frame_opts {
                    .pw = rpw, .ph = rph, .ring_disp = ring_disp, .need = need,
                    .seed = seed, .t_h = t_h, .t_v = t_v, .g = g,
                    .FRAME_INSET = FRAME_INSET, .dr = dr,
                    .no_top = no_top, .no_bottom = no_bottom,
                    .no_left = no_left, .no_right = no_right
                } );

                const std::string frame_id = "runic-frame-" + std::string( reg->GetId() );
                Rml::Element* fr = doc->GetElementById( frame_id.c_str() );
                if (fr == nullptr) {
                    Rml::ElementPtr fp = doc->CreateElement("div");
                    fp->SetId( frame_id.c_str() );
                    fr = doc->AppendChild(std::move(fp));
                }
                if (fr != nullptr) {
                    const Rml::Vector2f roff = reg->GetAbsoluteOffset(Rml::BoxArea::Border);
                    fr->SetProperty("position", "absolute");
                    fr->SetProperty("pointer-events", "none");
                    fr->SetProperty("z-index", "11");
                    fr->SetProperty("left", px(roff.x));
                    fr->SetProperty("top", px(roff.y));
                    fr->SetProperty("width", px(rsz.x));
                    fr->SetProperty("height", px(rsz.y));
                    if (frame_str.empty()) {
                        fr->SetProperty("decorator", "none");
                    } else {
                        fr->SetProperty("decorator", frame_str.c_str());
                    }
                }
            }
        }
        // Third sweep: corroded edge rules for strips (runic-edge-bottom/top).
        // Paints a thin decorative line along the strip's edge using the runic-rule
        {
            std::unordered_set<Rml::Element*> edge_set;
            Rml::ElementList edges_tmp;
            doc->GetElementsByClassName(edges_tmp, "runic-edge-bottom");
            for (Rml::Element* e : edges_tmp) { edge_set.insert(e); }
            edges_tmp.clear();
            doc->GetElementsByClassName(edges_tmp, "runic-edge-top");
            for (Rml::Element* e : edges_tmp) { edge_set.insert(e); }
            for (Rml::Element* ed : edge_set) {
                const Rml::Vector2f esz = ed->GetBox().GetSize(Rml::BoxArea::Border);
                const int epw = static_cast<int>(std::lround(esz.x));
                const float dr = g_density_ratio * g_ui_scale;
                const int len = static_cast<int>(std::lround(epw / dr));
                if (len < 1) { continue; }

                const unsigned seed = rcfg.use_fixed_seed
                    ? rcfg.seed
                    : (static_cast<unsigned>(epw) * 73856093u);
                const unsigned g = rcfg.regen;

                const bool flip = ed->IsClassSet("runic-edge-top");
                const std::string dec = std::format(
                    "image( ?proc:runic-rule:{}:{}:G{} {} scale-none 0px {}px ) border-box",
                    len, seed, g,
                    flip ? "flip-vertical" : "none",
                    flip ? 0 : (esz.y - 4));

                const std::string frame_id = "runic-frame-" + std::string(ed->GetId());
                Rml::Element* fr = doc->GetElementById(frame_id.c_str());
                if (fr == nullptr) {
                    Rml::ElementPtr fp = doc->CreateElement("div");
                    fp->SetId(frame_id.c_str());
                    fr = doc->AppendChild(std::move(fp));
                }
                if (fr != nullptr) {
                    const Rml::Vector2f eoff = ed->GetAbsoluteOffset(Rml::BoxArea::Border);
                    fr->SetProperty("position", "absolute");
                    fr->SetProperty("pointer-events", "none");
                    fr->SetProperty("z-index", "11");
                    fr->SetProperty("left", px(eoff.x));
                    fr->SetProperty("top", px(eoff.y));
                    fr->SetProperty("width", px(esz.x));
                    fr->SetProperty("height", px(esz.y));
                    fr->SetProperty("decorator", dec.c_str());
                }
            }
        }
        Rml::Element* overlay = doc->GetElementById("crt-overlay");
        if (overlay == nullptr) { continue; }
        // Mask the scanlines to the active panel (the "device screen"): size the
        // overlay to the panel's border-box rect, not the whole window. Geometry is
        // from the previous frame's layout (apply runs before Update), but panels
        // don't move so the 1-frame lag is invisible; a zero box (first frame /
        // unlaid-out) just hides the overlay until it settles.
        Rml::Element* panel = panels.empty() ? nullptr : panels.front();
        Rml::Vector2f poff;
        Rml::Vector2f psz;
        if (panel != nullptr) {
            poff = panel->GetAbsoluteOffset(Rml::BoxArea::Border);
            psz = panel->GetBox().GetSize(Rml::BoxArea::Border);
        }
        if (!g_crt.enabled || panel == nullptr || psz.x <= 0.f || psz.y <= 0.f) {
            overlay->SetProperty("display", "none");
            continue;
        }
        const float pitch = std::max(2.0f, g_crt.scanline_pitch);
        const float thick = std::clamp(g_crt.scanline_thickness, 0.5f, pitch - 0.5f);
        const float gap = pitch - thick;
        const float phase = static_cast<float>(std::fmod(t * g_crt.roll_speed, pitch));
        const unsigned sa = crt_a255(g_crt.scanline_alpha);
        // Flicker = a uniform black layer (under the scanlines) whose alpha pulses
        // per the random table. Modulating a thin scanline's opacity is invisible;
        // pulsing a full-panel tint reads as whole-screen CRT flicker. 0 -> no pulse.
        const int idx = static_cast<int>(std::fmod(t * 24.0, 20.0));
        const float ftab = CRT_FLICKER[std::clamp(idx, 0, 19)];
        const unsigned fa = crt_a255(g_crt.flicker * (1.0f - ftab));
        const std::string dec = std::format(
            "linear-gradient( 0deg, #000000{:02x}, #000000{:02x} ), "
            "repeating-linear-gradient( 0deg, "
            "#00000000 {:.2f}px, #00000000 {:.2f}px, "
            "#000000{:02x} {:.2f}px, #000000{:02x} {:.2f}px )",
            fa, fa, phase, phase + gap, sa, phase + gap, sa, phase + pitch);
        overlay->SetProperty("display", "block");
        overlay->SetProperty("position", "absolute");
        overlay->SetProperty("left", px(poff.x));
        overlay->SetProperty("top", px(poff.y));
        overlay->SetProperty("width", px(psz.x));
        overlay->SetProperty("height", px(psz.y));
        overlay->SetProperty("decorator", dec);
        overlay->SetProperty("opacity", "1.0");
    }
}
} // namespace

void new_frame() {
    if (!g_ready) { return; }
    // Drain deferred GPU-resource frees every frame, even with nothing open.
    g_render->begin_frame();
    if (any_open() && g_context != nullptr && g_window != nullptr) {
        int w = 0;
        int h = 0;
        SDL_GetWindowSizeInPixels(g_window, &w, &h);
        if (w > 0 && h > 0) {
            g_context->SetDimensions(Rml::Vector2i(w, h));
            // Context is sized in PHYSICAL pixels so the GPU projection matches
            // the framebuffer. Set the density ratio = physical / logical so RCSS
            // `dp` units scale with HiDPI — without this, px-sized fonts render at
            // half the intended size on a 2x retina display (the "tiny font"). The
            // mouse sx/sy scaling in process_event handles the inverse for input.
            int lw = 0;
            int lh = 0;
            SDL_GetWindowSize(g_window, &lw, &lh);
            if (lw > 0) {
                g_density_ratio = static_cast<float>(w) / static_cast<float>(lw);
                g_context->SetDensityIndependentPixelRatio(g_density_ratio * g_ui_scale);
            }
        }
        apply_crt();
        g_context->Update();
    }
}

void prepare(SDL_GPUCommandBuffer* cb) {
    const bool doc = g_ready && any_open() && g_context != nullptr;
    const bool wt = world_text_have();
    const bool px = plexus_active();
    if (!doc && !wt && !px) { g_world_geom.clear(); return; }
    // Pre-render OUTSIDE the render pass so geometry compiles immediately (not
    // deferred by begin_render_pass). Then upload_pending uploads the compiled
    // data to GPU buffers. The real render pass's ctx->Render() reuses the cached
    // handles and renders on this same frame. World text compiles the same way.
    if (doc) { g_context->Render(); }
    if (wt) { build_world_text(); }
    // Plexus: upload the pixel buffer to a GPU texture (issues a copy pass).
    if (px) { rebuild_plexus_geom(); }
    g_render->upload_pending(cb);
}

void render_in_pass(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cb) {
    const bool doc = any_open();
    const bool wt = !g_world_geom.empty();
    const bool px = plexus_active();
    if (!g_ready || g_window == nullptr || g_context == nullptr || (!doc && !wt && !px)) { return; }
    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels(g_window, &w, &h);
    const auto uw = static_cast<std::uint32_t>(w > 0 ? w : 1);
    const auto uh = static_cast<std::uint32_t>(h > 0 ? h : 1);
    // RmlUi context is sized in physical pixels, so projection == target.
    g_render->begin_render_pass(rp, cb, uw, uh, uw, uh);
    // Plexus background draws FIRST (behind everything).
    if (px && g_plexus_geom_handle && g_plexus_tex_handle) {
        g_render->RenderGeometry(g_plexus_geom_handle, Rml::Vector2f(0, 0), g_plexus_tex_handle);
    }
    // World text next (SCT sits on the map, under any open UI).
    for (world_text_geom& item : g_world_geom) { item.geom.Render(item.pos, item.texture); }
    // Documents on top (menu panel composites over the plexus).
    if (doc) {
        g_context->Render(); // all shown documents, z-ordered by open order
    }
    g_render->end_render_pass();

    // D3D12 in-pass-upload watch: a non-zero textures count means a glyph atlas
    // uploaded mid-pass (the documented hazard). Geometry is deferred so a
    // non-zero compiles count is safe. Logs only on change.
    static std::uint32_t last_c = 0;
    static std::uint32_t last_t = 0;
    const std::uint32_t c = g_render->compiles_in_pass();
    const std::uint32_t t = g_render->textures_in_pass();
    if (c != last_c || t != last_t) {
        last_c = c;
        last_t = t;
        DebugLogFL(DL::Info, DC::Main)
            << "rmlui in-pass uploads: compiles=" << c << " textures=" << t
            << " (0 = safe; geometry deferred, textures are the D3D12 watch)";
    }
}

void world_text_begin() { g_world_text.clear(); }

void set_hud_text(float screen_x, float screen_y, const std::string& utf8, unsigned int rgba) {
    g_hud_active = !utf8.empty();
    g_hud_text = world_text_item{screen_x, screen_y, utf8, rgba};
}

void world_text_add(float screen_x, float screen_y, const std::string& utf8, unsigned int rgba) {
    if (utf8.empty()) { return; }
    g_world_text.push_back(world_text_item{screen_x, screen_y, utf8, rgba});
}
// Combat text: submit a floating damage/healing number.
auto combat_text_add( const combat_text_options &opts ) -> void
{
    g_combat_text.emplace_back( combat_text_item{
        opts.x, opts.y, opts.x, opts.y,
        opts.text, opts.rgba, opts.font_scale,
        opts.lifetime_ms, 0.f, opts.vx, opts.vy, opts.ay } );
}

// Advance combat text items by dt_ms, remove expired.
auto combat_text_tick( float dt_ms ) -> void
{
    const float dt_sec = dt_ms / 1000.0f;
    for( auto &item : g_combat_text ) {
        item.age_ms += dt_ms;
        item.vy += item.ay * dt_sec;
        item.x += item.vx * dt_sec;
        item.y += item.vy * dt_sec;
    }
    std::erase_if( g_combat_text, []( const combat_text_item &it ) {
        return it.age_ms >= it.lifetime_ms;
    } );
}

auto combat_text_active() -> bool
{
    return !g_combat_text.empty();
}

bool world_text_active() { return world_text_have(); }

int& world_text_px() { return g_world_text_px; }

float& world_text_dx() { return g_world_text_dx; }

float& world_text_dy() { return g_world_text_dy; }

} // namespace rmlui_layer
