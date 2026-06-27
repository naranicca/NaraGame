#pragma once

#include "NaraGame/renderer/rhi/RHIResources.h"
#include "NaraGame/renderer/rhi/RHITypes.h"

#include <cstdint>
#include <memory>

namespace NaraGame {

struct RHIDeviceDesc {
    void* nativeWindowHandle = nullptr; // HWND / NSWindow* / ANativeWindow*
    uint32_t width = 0;
    uint32_t height = 0;
    bool vsync = true;
};

// Render Hardware Interface: the abstraction every native backend implements.
// The skeleton covers swap-chain lifetime + clear; resource/draw APIs (buffers,
// textures, shaders, pipelines, command lists) are layered on top later.
class RHIDevice {
public:
    static std::unique_ptr<RHIDevice> Create(const RHIDeviceDesc& desc);

    virtual ~RHIDevice() = default;

    virtual void Resize(uint32_t width, uint32_t height) = 0;

    virtual void BeginFrame() = 0;
    virtual void Clear(const Color& color) = 0;
    // Wipe the depth attachment to `depth` (0 = near, 1 = far). 3D passes
    // should call this once per frame after BeginFrame before drawing.
    // Default no-op so 2D-only backends (or 2D test apps) compile without
    // implementing it; the D3D11 backend overrides with the real clear.
    virtual void ClearDepth(float /*depth*/ = 1.0f) {}
    virtual void EndFrame() = 0;
    virtual void Present() = 0;

    // Bind a render-target texture (created with TextureDesc::renderTarget
    // = true) as the active colour target. Subsequent draws land in it, and
    // its viewport / scissor match the texture's dimensions. Passing nullptr
    // restores the backbuffer. Clear() after this call wipes the new target.
    // The previous target's contents become readable as a sampled texture.
    virtual void SetRenderTarget(RHITexture* target) = 0;

    // Bind a colour target and a depth target together. Use when the
    // post-process pipeline needs the scene rendered into an HDR colour
    // RT and its own depth RT so a later pass can sample the depth as
    // a texture (DoF, edge detection, ...). Default falls back to
    // SetRenderTarget(colorTex) for backends that haven't implemented
    // the dual-RT path; that loses the custom depth attachment but
    // keeps the colour render working.
    virtual void SetRenderTargets(RHITexture* colorTex, RHITexture* depthTex) {
        (void)depthTex;
        SetRenderTarget(colorTex);
    }

    // --- Resource creation ---
    virtual std::unique_ptr<RHIBuffer> CreateBuffer(const BufferDesc& desc) = 0;
    virtual std::unique_ptr<RHITexture> CreateTexture(const TextureDesc& desc) = 0;
    virtual std::unique_ptr<RHIShader> CreateShader(const ShaderDesc& desc) = 0;
    virtual std::unique_ptr<RHIPipeline> CreatePipeline(const PipelineDesc& desc) = 0;

    // --- Binding + draw (immediate mode) ---
    virtual void BindPipeline(RHIPipeline* pipeline) = 0;
    virtual void BindVertexBuffer(RHIBuffer* buffer, uint32_t stride, uint32_t offset = 0) = 0;
    virtual void BindIndexBuffer(RHIBuffer* buffer, IndexFormat format) = 0;
    virtual void BindUniformBuffer(uint32_t slot, RHIBuffer* buffer) = 0;
    virtual void BindTexture(uint32_t slot, RHITexture* texture) = 0;
    virtual void DrawIndexed(uint32_t indexCount, uint32_t firstIndex = 0) = 0;

    virtual RHIBackend Backend() const = 0;
    virtual const char* BackendName() const = 0;

    // Backend-native handles for runtime tooling (editor GUI, debug
    // overlays). The caller is responsible for knowing which backend it
    // is on and casting accordingly:
    //   D3D11  : GetNativeDevice() -> ID3D11Device*,
    //            GetNativeContext() -> ID3D11DeviceContext*
    //   Vulkan : GetNativeDevice() -> VkDevice (dispatchable handle),
    //            GetNativeContext() -> nullptr
    // Default returns nullptr so a backend that hasn't surfaced these
    // yet still compiles and just disables the tooling that needs them.
    virtual void* GetNativeDevice()  const { return nullptr; }
    virtual void* GetNativeContext() const { return nullptr; }
};

} // namespace NaraGame
