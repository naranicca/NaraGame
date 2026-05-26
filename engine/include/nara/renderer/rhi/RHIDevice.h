#pragma once

#include "nara/renderer/rhi/RHIResources.h"
#include "nara/renderer/rhi/RHITypes.h"

#include <cstdint>
#include <memory>

namespace nara {

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
    virtual void EndFrame() = 0;
    virtual void Present() = 0;

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
};

} // namespace nara
