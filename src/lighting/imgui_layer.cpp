#include "imgui_layer.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <utility>

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

// Registry of player-facing ImGui draw callbacks (e.g. uilist).
// Each entry has a unique integer handle for removal.
struct callback_entry {
    int handle;
    std::function<void()> fn;
};
std::vector<callback_entry> g_draw_callbacks;
int g_next_handle = 1;
}  // namespace

void set_dev_ui( std::function<void()> fn )
{
    g_dev_ui = std::move( fn );
}

auto push_draw_callback( std::function<void()> fn ) -> int
{
    const int handle = g_next_handle++;
    g_draw_callbacks.push_back( { handle, std::move( fn ) } );
    return handle;
}

void remove_draw_callback( int handle )
{
    const auto it = std::find_if( g_draw_callbacks.begin(), g_draw_callbacks.end(),
    [handle]( const callback_entry &e ) {
        return e.handle == handle;
    } );
    if( it != g_draw_callbacks.end() ) {
        g_draw_callbacks.erase( it );
    }
}

static auto has_active_menus() -> bool
{
    return g_visible || !g_draw_callbacks.empty();
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

bool active()
{
    return g_ready && has_active_menus();
}

bool process_event( const SDL_Event &ev )
{
    // Gate on visible() OR non-empty menu registry: WantCapture* is only
    // recomputed inside new_frame(), which doesn't run while no menu is open.
    // Without a menu, skip feeding events to avoid dangling WantCapture flags.
    if( !g_ready || !has_active_menus() ) {
        return false;
    }
    ImGui_ImplSDL3_ProcessEvent( &ev );
    const ImGuiIO &io = ImGui::GetIO();
    // For player-facing menus (registry) ImGui should only capture mouse,
    // not keyboard — game owns keyboard navigation.
    if( !g_draw_callbacks.empty() ) {
        return io.WantCaptureMouse;
    }
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

    // Run player-facing menu callbacks (e.g. uilist).
    // These use ImGuiWindowFlags_NoNav to keep keyboard with the game.
    for( auto &entry : g_draw_callbacks ) {
        if( entry.fn ) {
            entry.fn();
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
