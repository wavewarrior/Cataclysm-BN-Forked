#pragma once
#ifndef CATA_SRC_LIGHTING_RMLUI_LAYER_H
#define CATA_SRC_LIGHTING_RMLUI_LAYER_H

#include <string>
#include <vector>

// RmlUi UI layer — sibling of imgui_layer, bolted beside the SDL_GPU renderer.
// Own RenderInterface (its own pipeline + premult blend), own input handling,
// shares the single swapchain pass via the same end_pass overlay hook as ImGui.
// Target: player-facing menus styled with RML+RCSS. NOT a sprite_batcher layer.
//
// Model: ONE shared Rml::Context "main" hosting a STACK of documents. A menu's
// query() loop opens a document (open_document), drives it via a data-model each
// tick, and closes it on exit (close_document) — nested/modal menus stack as
// multiple shown documents, z-ordered by open order. The layer is "active"
// whenever ≥1 document is open. Keyboard stays with the game's input_context;
// RmlUi only renders + handles mouse, exactly as ImGui does for dev panels.
//
// Fail-safe like imgui_layer: any init failure leaves ready()==false and the
// game runs without it.

struct SDL_GPUCommandBuffer;
struct SDL_GPURenderPass;
union SDL_Event;

namespace lighting {
class gpu_device;
} // namespace lighting

namespace Rml {
class Context;
class ElementDocument;
} // namespace Rml

// Top-level namespace to mirror imgui_layer (call sites read `rmlui_layer::init`
// next to `imgui_layer::init`).
namespace rmlui_layer {

// Initialise RmlUi + interfaces + a window-sized shared context + a font. Takes
// the live render_state gpu_device (window + swapchain format come from it).
// Idempotent via ready(); any failure degrades to ready()==false (no crash).
bool init(lighting::gpu_device& device);

// Destroy the context, interfaces and RmlUi core. Safe when not ready.
void shutdown();

// True once init succeeded.
bool ready();

// Whether the layer should DRAW this frame: ready && at least one document is
// currently open. This counts always-open PASSIVE documents (the sidebar HUD),
// so it MUST NOT gate input — use capturing_input() for that.
bool active();

// Whether the layer owns this frame's mouse input: ready && at least one
// INTERACTIVE document is open. A passive document paints every frame without
// capturing input, so gating input on active() silently kills every world mouse
// binding the moment the sidebar HUD (passive, defaults ON) is enabled.
bool capturing_input();

// The shared context (nullptr until ready()). Callers use it to create
// data-models (context()->CreateDataModel) for their documents.
Rml::Context* context();

// Physical/logical pixel ratio last applied to the context (the HiDPI density
// ratio set in new_frame). 1.0 until the first frame. Callers that size things
// in layout pixels from RCSS `dp` lengths multiply by this (e.g. uilist's
// virtualized-row pitch). See SetDensityIndependentPixelRatio in new_frame.
float density_ratio();

// User UI-scale multiplier (mutable; default 1.0). Multiplies the HiDPI dp ratio
// applied to the context, scaling font + all dp spacing across every RmlUi panel.
// Driven by an F4 dev slider; does not affect input mapping. <1 shrinks the UI.
float& ui_scale();

// CRT post-effect knobs (F4 dev sliders). Applied every frame in new_frame() to
// each open document as inline RCSS GRADIENT decorators — the render interface now
// implements the shader API (CompileShader/RenderShader), so gradients render:
//   - scanlines -> repeating-linear-gradient on the #crt-overlay element (a bare
//                  click-through <div> opted into, drawn ON TOP so the lines cross
//                  the bright text). Fully parametric: pitch/thickness/darkness are
//                  the gradient stops; scroll shifts the stop offsets each frame.
//   - vignette  -> radial-gradient on every .panel, behind content.
// Tune in F4, then bake the chosen gradient strings into theme.rcss for the ship
// look (they are ordinary RCSS once the values are fixed).
struct crt_params {
    bool enabled = true;
    float scanline_alpha = 0.35f;    // line darkness (0..1)
    float scanline_pitch = 4.0f;     // px, line + gap pitch
    float scanline_thickness = 1.0f; // px, dark-line height within the pitch
    float roll_speed = 8.0f;         // px/sec downward scroll (0 = static)
    float flicker = 0.05f;           // opacity-pulse amplitude (0..1)
    float vignette_alpha = 0.5f;     // corner darkness on .panel (0..1)
};
crt_params& crt();

// Re-apply the current ui_theme to all open documents: clears RmlUi's stylesheet
// cache and reloads each document's stylesheet, so the FileInterface re-substitutes
// {{tokens}} with the latest values. Call after editing theme colours live (F4).
void reload_theme();

// Load + show an .rml document into the shared context, tracking it so active()
// reports it. Returns the document (owned by the context) or nullptr on failure.
// Pair with close_document() — typically via an RAII wrapper in the owner's
// query() scope. Build the document's data-model BEFORE calling this.
//
// passive=true marks the doc render-only (e.g. the Tier-7 sidebar HUD): it paints
// every frame but never captures input — process_event lets mouse/keys fall through
// to the game while only passive docs are open. Default (interactive) modal docs
// capture mouse as before.
Rml::ElementDocument* open_document(const std::string& rml_path, bool passive = false);

// Hide, unload and untrack a document opened with open_document(). Safe with
// nullptr or an already-closed document.
void close_document(Rml::ElementDocument* doc);

// Temporarily hide every open document for this guard's lifetime, then re-show
// exactly the ones it hid. Context::Render() draws every SHOWN document on top
// of whatever curses painted this frame, so a curses-rendered modal opened while
// a document is live is invisible — that is the debugmsg error prompt during data
// loading, drowned by the still-open gui/mainmenu.rml + gui/loading.rml (the
// player can only blind-press space/i). Documents closed while hidden stay
// closed. No-op when !ready() or nothing is open.
class scoped_documents_hidden {
public:
    scoped_documents_hidden();
    ~scoped_documents_hidden();
    scoped_documents_hidden(const scoped_documents_hidden&) = delete;
    auto operator=(const scoped_documents_hidden&) -> scoped_documents_hidden& = delete;

private:
    std::vector<Rml::ElementDocument*> hidden_;
};

// Translate one SDL event into RmlUi input. Returns true if RmlUi consumed it
// (so the game should ignore it). Mouse only — keyboard falls through to the
// game's input_context, which owns menu navigation.
bool process_event(const SDL_Event& ev);

// Advance the shared context one frame (Context::Update). Cheap; no GPU work.
void new_frame();

// Upload this frame's geometry OUTSIDE the render pass (D3D12-safe).
void prepare(SDL_GPUCommandBuffer* cb);

// Record RmlUi draws INTO the open swapchain pass (Context::Render).
void render_in_pass(SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cb);

// --- World-space text layer (§7) ---
// Imperative on-map text rendered through RmlUi's OWN font engine — the glyph
// path that survives the curses rip-out — positioned in physical screen pixels
// (the same space player_to_screen produces; the context is sized in physical
// px so no density division is needed). Currently fed by scrolling combat text
// (SCT); the foundation for future floating damage numbers.
//
// Lifecycle mirrors the document path: items are submitted each frame (cleared
// by world_text_begin), compiled to geometry in prepare(), and drawn in
// render_in_pass() UNDER the menu documents. Unlike documents, world text does
// NOT make the layer active() (that gates input) — the render gate in
// sdl_render_frame uses active() || world_text_active() so combat text shows
// with no menu open without stealing mouse input.

// Clear this frame's submitted world-text items. Call before re-submitting.
void world_text_begin();

// Queue one text item for this frame. screen_x/screen_y = physical-pixel
// position of the text's top-left; rgba = 0xRRGGBBAA (alpha 0xFF = opaque).
void world_text_add(float screen_x, float screen_y, const std::string& utf8, unsigned int rgba);
// --- Floating combat text (Phase 5) ---
// Arcing damage/healing numbers with trajectory, scaling, and lifecycle.
// Extends the world_text layer with per-item physics.

// Options for submitting a combat text item.
struct combat_text_options {
    float x = 0.f;           // initial screen X (logical px)
    float y = 0.f;           // initial screen Y (logical px)
    std::string text;        // text to display
    unsigned int rgba = 0xFFFFFFFFu; // 0xRRGGBBAA
    float font_scale = 1.0f; // 1.0 = normal, 1.5 = crit
    float lifetime_ms = 1200.f;
    float vx = 0.f;          // horizontal velocity px/sec (scatter)
    float vy = -30.f;        // vertical velocity px/sec (negative = up)
    float ay = 5.f;          // vertical acceleration px/sec^2 (gravity)
};

// Submit a floating combat text item.
auto combat_text_add( const combat_text_options &opts ) -> void;
auto combat_text_tick( float dt_ms ) -> void;
auto combat_text_active() -> bool;

// True if any world-text items are queued this frame (render-gate input).
bool world_text_active();

// Persistent single-line HUD overlay (e.g. FPS counter). SET each frame (not
// appended) so it never accumulates, and kept OUTSIDE the world_text begin/clear
// cycle: it renders every frame regardless of menus or combat text, and makes
// world_text_active() true on its own. Pass an empty utf8 to clear it.
void set_hud_text(float screen_x, float screen_y, const std::string& utf8, unsigned int rgba);

// World-text tuning knobs (F4 dev sliders): font point size + extra x/y pixel
// offset applied to every item. Bake the dialed-in values once settled.
int& world_text_px();
float& world_text_dx();
float& world_text_dy();

} // namespace rmlui_layer

#endif // CATA_SRC_LIGHTING_RMLUI_LAYER_H
