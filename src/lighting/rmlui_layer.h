#pragma once
#ifndef CATA_SRC_LIGHTING_RMLUI_LAYER_H
#define CATA_SRC_LIGHTING_RMLUI_LAYER_H

// RmlUi UI layer — sibling of imgui_layer, bolted beside the SDL_GPU renderer.
// Own RenderInterface (its own pipeline + premult blend in Phase 2), own input,
// shares the single swapchain pass via the same end_pass overlay hook as ImGui.
// Spike target: player-facing menus styled with RML+RCSS. NOT a sprite_batcher layer.
//
// 2026-06 spike, Phase 1: lifecycle only (init/shutdown). Render + input land
// in Phases 2-3. Fail-safe like imgui_layer: any init failure leaves
// ready()==false and the game runs without it.

struct SDL_Window;
struct SDL_GPUDevice;
struct SDL_GPUCommandBuffer;
struct SDL_GPURenderPass;
union SDL_Event;

// Top-level namespace to mirror imgui_layer (call sites read `rmlui_layer::init`
// next to `imgui_layer::init`).
namespace rmlui_layer
{

// Initialise RmlUi + interfaces + a window-sized context + a font. Idempotent
// via ready(); any failure degrades to ready()==false (no crash).
bool init( SDL_Window *window, SDL_GPUDevice *device );

// Destroy the context, interfaces and RmlUi core. Safe when not ready.
void shutdown();

// True once init succeeded.
bool ready();

// Caller-togglable visibility of the spike UI.
bool &visible();

// Whether the layer should consume this frame's input/draw (ready && visible).
bool active();

// Translate one SDL event into RmlUi input. Returns true if RmlUi consumed it
// (so the game should ignore it). PHASE 3 — Phase 1 returns false.
bool process_event( const SDL_Event &ev );

// Advance the RmlUi context one frame (Context::Update). Cheap; no GPU work.
void new_frame();

// Upload this frame's geometry OUTSIDE the render pass (D3D12-safe). PHASE 2.
void prepare( SDL_GPUCommandBuffer *cb );

// Record RmlUi draws INTO the open swapchain pass (Context::Render). PHASE 2.
void render_in_pass( SDL_GPURenderPass *rp, SDL_GPUCommandBuffer *cb );

}  // namespace rmlui_layer

#endif  // CATA_SRC_LIGHTING_RMLUI_LAYER_H
