#pragma once
#ifndef CATA_SRC_LIGHTING_RMLUI_RENDER_INTERFACE_H
#define CATA_SRC_LIGHTING_RMLUI_RENDER_INTERFACE_H

#include <RmlUi/Core/RenderInterface.h>
#include <cstdint>
#include <memory>

// RmlUi RenderInterface over SDL_GPU. Sibling of imgui_layer's render path: its
// own small pipeline (textured triangles + premultiplied-alpha blend), its own
// vertex/index buffers, draws INTO the shared swapchain pass via the end_pass
// overlay hook (D3D12 single-pass invariant).
//
// Upload-timing-safe (the #1 D3D12 risk): CompileGeometry copies to CPU-side
// storage only; the GPU upload happens in upload_pending() (called from the
// layer's prepare(), OUTSIDE the render pass). RenderGeometry only records draws
// of already-uploaded buffers — geometry compiled mid-Render() (caret/hover)
// uploads on the next frame's prepare() and is skipped until then (1-frame
// pop-in). Textures upload immediately on their own command buffer (safe for the
// static spike, where they are generated during Update() before the pass);
// in-pass texture/geometry generation is COUNTED for the Phase-5 D3D12 gate.

struct SDL_GPURenderPass;
struct SDL_GPUCommandBuffer;

namespace lighting {

class gpu_device;

class rmlui_render_interface: public Rml::RenderInterface {
public:
    rmlui_render_interface();
    ~rmlui_render_interface() override;

    // Build pipeline + 1x1 white texture + sampler for dev's swapchain
    // format. Returns false on failure (layer degrades to no-RmlUi).
    bool init(gpu_device& dev);
    void shutdown();

    // Roll deferred GPU-resource frees forward one frame.
    void begin_frame();
    // Upload geometry compiled since the last call. MUST run outside a render
    // pass — `cb` is the frame's pre-pass command buffer.
    void upload_pending(SDL_GPUCommandBuffer* cb);
    // Bracket Context::Render(): record the open pass + projection the in-pass
    // overrides draw into. target_* = physical swapchain px (viewport);
    // proj_* = logical coordinate extent the document is laid out in.
    void begin_render_pass(
        SDL_GPURenderPass* rp, SDL_GPUCommandBuffer* cb, std::uint32_t target_w,
        std::uint32_t target_h, std::uint32_t proj_w, std::uint32_t proj_h);
    void end_render_pass();

    // Phase-5 D3D12 gate instrumentation: counts of compiles / texture
    // generations that fired while a render pass was open (the upload-in-pass
    // hazard). Zero is the pass condition.
    std::uint32_t compiles_in_pass() const noexcept;
    std::uint32_t textures_in_pass() const noexcept;

    // --- Rml::RenderInterface (required) ---
    Rml::CompiledGeometryHandle CompileGeometry(
        Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
    void RenderGeometry(
        Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
        Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

    Rml::TextureHandle LoadTexture(
        Rml::Vector2i& texture_dimensions, const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(
        Rml::Span<const Rml::byte> source, Rml::Vector2i source_dimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;

    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;

    // Gradient decorators (linear/radial/conic + repeating). Without these,
    // RmlUi gradient decorators silently render nothing on this backend.
    Rml::CompiledShaderHandle CompileShader(
        const Rml::String& name, const Rml::Dictionary& parameters) override;
    void RenderShader(
        Rml::CompiledShaderHandle shader, Rml::CompiledGeometryHandle geometry,
        Rml::Vector2f translation, Rml::TextureHandle texture) override;
    void ReleaseShader(Rml::CompiledShaderHandle shader) override;

private:
    struct impl;
    std::unique_ptr<impl> p;
};

} // namespace lighting

#endif // CATA_SRC_LIGHTING_RMLUI_RENDER_INTERFACE_H
