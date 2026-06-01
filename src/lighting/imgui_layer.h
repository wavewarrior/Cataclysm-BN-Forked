#pragma once
#ifndef CATA_SRC_LIGHTING_IMGUI_LAYER_H
#define CATA_SRC_LIGHTING_IMGUI_LAYER_H

#include <functional>

// Dear ImGui dev/debug-only UI layer.
//
// A PARALLEL UI stack bolted beside the SDL_GPU renderer — own pipeline, own
// font atlas, own (prebaked) shaders, own input. NOT a layer over sprite_batcher.
// Allowed ONLY for dev/debug/author-facing surfaces with no existing catacurses
// screen (see memory: plan-imgui-dev-ui). Player-facing UI stays catacurses.
//
// Inert until init() succeeds on a real window+device (gated like
// render_state::ready()), so the test binary links it but never runs it.
//
// Header is self-contained: SDL types are forward-declared, no SDL include.

struct SDL_Window;
struct SDL_GPUDevice;
struct SDL_GPUCommandBuffer;
struct SDL_GPURenderPass;
union SDL_Event;

namespace imgui_layer
{

// Create the ImGui context + SDL3/SDL_GPU backends. Queries the swapchain
// format from (device, window) itself, so the header stays SDL-enum-free.
// Fail-safe: any failure leaves ready()==false and the game runs without dev UI.
bool init( SDL_Window *window, SDL_GPUDevice *device );

// Destroy backends + context. Safe to call when not ready.
void shutdown();

// True once init() succeeded. All other calls are no-ops while false.
bool ready();

// True when any dev tool is open (drives the on-demand frame loop + whether a
// frame must be produced). Reference so callers can toggle it.
bool &visible();

// Feed one SDL event to ImGui. Returns true if ImGui wants to consume it
// (io.WantCaptureMouse/Keyboard) — caller must then NOT translate it to a game
// input event.
bool process_event( const SDL_Event &ev );

// Register the dev-UI draw callback — invoked by new_frame() while visible().
// One slot for now (lighting HUD); becomes the dev_tools registry in P2.
void set_dev_ui( std::function<void()> fn );

// Begin an ImGui frame and build this frame's dev widgets. Must be paired with
// prepare()+render_in_pass() the same frame (ImGui asserts otherwise).
void new_frame();

// ImGui::Render() + upload vertex/index buffers on `cb`. MUST run OUTSIDE any
// render pass (it opens its own GPU copy pass). Call before Pass B begins.
void prepare( SDL_GPUCommandBuffer *cb );

// Emit ImGui draw commands INTO an already-open render pass (Pass B / swapchain).
// Wired through sprite_batcher::end_pass's overlay hook so it shares the one
// swapchain pass (D3D12 drops a second pass on the same target).
void render_in_pass( SDL_GPURenderPass *rp, SDL_GPUCommandBuffer *cb );

}  // namespace imgui_layer

#endif  // CATA_SRC_LIGHTING_IMGUI_LAYER_H
