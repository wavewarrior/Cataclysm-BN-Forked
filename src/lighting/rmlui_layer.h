#pragma once
#ifndef CATA_SRC_LIGHTING_RMLUI_LAYER_H
#define CATA_SRC_LIGHTING_RMLUI_LAYER_H

#include <string>

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

namespace lighting
{
class gpu_device;
}  // namespace lighting

namespace Rml
{
class Context;
class ElementDocument;
}  // namespace Rml

// Top-level namespace to mirror imgui_layer (call sites read `rmlui_layer::init`
// next to `imgui_layer::init`).
namespace rmlui_layer
{

// Initialise RmlUi + interfaces + a window-sized shared context + a font. Takes
// the live render_state gpu_device (window + swapchain format come from it).
// Idempotent via ready(); any failure degrades to ready()==false (no crash).
bool init( lighting::gpu_device &device );

// Destroy the context, interfaces and RmlUi core. Safe when not ready.
void shutdown();

// True once init succeeded.
bool ready();

// Whether the layer should consume this frame's input/draw: ready && at least
// one document is currently open.
bool active();

// The shared context (nullptr until ready()). Callers use it to create
// data-models (context()->CreateDataModel) for their documents.
Rml::Context *context();

// Physical/logical pixel ratio last applied to the context (the HiDPI density
// ratio set in new_frame). 1.0 until the first frame. Callers that size things
// in layout pixels from RCSS `dp` lengths multiply by this (e.g. uilist's
// virtualized-row pitch). See SetDensityIndependentPixelRatio in new_frame.
float density_ratio();

// Load + show an .rml document into the shared context, tracking it so active()
// reports it. Returns the document (owned by the context) or nullptr on failure.
// Pair with close_document() — typically via an RAII wrapper in the owner's
// query() scope. Build the document's data-model BEFORE calling this.
Rml::ElementDocument *open_document( const std::string &rml_path );

// Hide, unload and untrack a document opened with open_document(). Safe with
// nullptr or an already-closed document.
void close_document( Rml::ElementDocument *doc );

// Translate one SDL event into RmlUi input. Returns true if RmlUi consumed it
// (so the game should ignore it). Mouse only — keyboard falls through to the
// game's input_context, which owns menu navigation.
bool process_event( const SDL_Event &ev );

// Advance the shared context one frame (Context::Update). Cheap; no GPU work.
void new_frame();

// Upload this frame's geometry OUTSIDE the render pass (D3D12-safe).
void prepare( SDL_GPUCommandBuffer *cb );

// Record RmlUi draws INTO the open swapchain pass (Context::Render).
void render_in_pass( SDL_GPURenderPass *rp, SDL_GPUCommandBuffer *cb );

}  // namespace rmlui_layer

#endif  // CATA_SRC_LIGHTING_RMLUI_LAYER_H
