#include "imgui_layer.h"

#include <SDL3/SDL.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"

#include "debug.h"

// Lighting/ files must define dbg themselves (not globally available).
#define dbg( x ) DebugLogFL( ( x ), DC::SDL )

namespace imgui_layer
{
namespace
{
bool g_ready = false;
bool g_visible = false;
// init() tried once (success OR failure). Stops the lazy refresh_display caller
// from re-attempting + re-logging a failed init every single frame — matters on
// the D3D12 path where init is most likely to fail.
bool g_attempted = false;
// Dev-UI draw callback (lighting HUD). Owns no ImGui state itself.
std::function<void()> g_dev_ui;
}  // namespace

void set_dev_ui( std::function<void()> fn )
{
    g_dev_ui = std::move( fn );
}

bool init( SDL_Window *window, SDL_GPUDevice *device )
{
    if( g_ready ) {
        return true;
    }
    if( g_attempted ) {
        // Already failed once — stay failed quietly (no per-frame retry/log).
        return false;
    }
    g_attempted = true;
    if( window == nullptr || device == nullptr ) {
        dbg( DL::Warn ) << "imgui_layer: init skipped (null window/device)";
        return false;
    }

    // Fail-safe: a backend failure must degrade to "no dev UI", never crash the
    // game (mirrors init_render_state_on's swallow-and-flag behaviour).
    try {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        // Don't persist window layout to an imgui.ini in the working dir — dev
        // tools don't need it and it would pollute the repo root.
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        // Multi-viewport intentionally OFF (spawns extra swapchains; least
        // battle-tested path of the SDL_GPU backend).

        if( !ImGui_ImplSDL3_InitForSDLGPU( window ) ) {
            dbg( DL::Error ) << "imgui_layer: ImGui_ImplSDL3_InitForSDLGPU failed";
            ImGui::DestroyContext();
            return false;
        }

        ImGui_ImplSDLGPU3_InitInfo info = {};
        info.Device = device;
        info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat( device, window );
        info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
        if( !ImGui_ImplSDLGPU3_Init( &info ) ) {
            dbg( DL::Error ) << "imgui_layer: ImGui_ImplSDLGPU3_Init failed";
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
            return false;
        }
    } catch( const std::exception &e ) {
        dbg( DL::Error ) << "imgui_layer: init threw: " << e.what();
        return false;
    } catch( ... ) {
        dbg( DL::Error ) << "imgui_layer: init threw (unknown)";
        return false;
    }

    g_ready = true;
    dbg( DL::Info ) << "imgui_layer: init OK";
    return true;
}

void shutdown()
{
    if( !g_ready ) {
        return;
    }
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    g_ready = false;
    g_attempted = false;
}

bool ready()
{
    return g_ready;
}

bool &visible()
{
    return g_visible;
}

bool process_event( const SDL_Event &ev )
{
    // Gate on visible(), NOT just ready(): WantCapture* is only recomputed in
    // new_frame(), which doesn't run while the panel is hidden. Without this,
    // closing the panel while hovering it leaves the flags stuck true and every
    // event keeps getting captured → game input frozen until reopened.
    if( !g_ready || !g_visible ) {
        return false;
    }
    ImGui_ImplSDL3_ProcessEvent( &ev );
    const ImGuiIO &io = ImGui::GetIO();
    return io.WantCaptureMouse || io.WantCaptureKeyboard;
}

void new_frame()
{
    if( !g_ready ) {
        return;
    }
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    if( g_visible ) {
        if( g_dev_ui ) {
            g_dev_ui();
        } else {
            // Fallback when no dev tool is registered yet (proves the stack).
            ImGui::ShowDemoWindow( &g_visible );
        }
    }
}

void prepare( SDL_GPUCommandBuffer *cb )
{
    if( !g_ready ) {
        return;
    }
    ImGui::Render();
    ImGui_ImplSDLGPU3_PrepareDrawData( ImGui::GetDrawData(), cb );
}

void render_in_pass( SDL_GPURenderPass *rp, SDL_GPUCommandBuffer *cb )
{
    if( !g_ready ) {
        return;
    }
    ImGui_ImplSDLGPU3_RenderDrawData( ImGui::GetDrawData(), cb, rp );
}

}  // namespace imgui_layer
