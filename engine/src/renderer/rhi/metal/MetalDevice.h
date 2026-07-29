#pragma once

#include "NaraGame/core/Platform.h"

#if defined(NARAGAME_PLATFORM_MACOS) || defined(NARAGAME_PLATFORM_IOS)

#include "NaraGame/renderer/rhi/RHIDevice.h"

namespace NaraGame {

class MetalDevice final : public RHIDevice {
public:
    explicit MetalDevice(const RHIDeviceDesc& desc);
    ~MetalDevice() override;

    bool Valid() const { return m_device != nullptr && m_layer != nullptr; }

    void Resize(uint32_t, uint32_t) override;
    void BeginFrame() override;
    void Clear(const Color&) override;
    void EndFrame() override;
    void Present() override;
    void SetRenderTarget(RHITexture*) override {}

    std::unique_ptr<RHIBuffer> CreateBuffer(const BufferDesc&) override;
    std::unique_ptr<RHITexture> CreateTexture(const TextureDesc&) override;
    std::unique_ptr<RHIShader> CreateShader(const ShaderDesc&) override;
    std::unique_ptr<RHIPipeline> CreatePipeline(const PipelineDesc&) override;

    void BindPipeline(RHIPipeline*) override {}
    void BindVertexBuffer(RHIBuffer*, uint32_t, uint32_t) override {}
    void BindIndexBuffer(RHIBuffer*, IndexFormat) override {}
    void BindUniformBuffer(uint32_t, RHIBuffer*) override {}
    void BindTexture(uint32_t, RHITexture*) override {}
    void DrawIndexed(uint32_t, uint32_t) override {}

    RHIBackend Backend() const override { return RHIBackend::Metal; }
    const char* BackendName() const override { return "Metal"; }
    void* GetNativeDevice() const override { return m_device; }

private:
    void* m_layer = nullptr;         // CAMetalLayer*
    void* m_device = nullptr;        // id<MTLDevice>
    void* m_queue = nullptr;         // id<MTLCommandQueue>
    void* m_drawable = nullptr;      // id<CAMetalDrawable>
    void* m_commandBuffer = nullptr; // id<MTLCommandBuffer>
    void* m_encoder = nullptr;       // id<MTLRenderCommandEncoder>
    uint32_t m_width = 0;
    uint32_t m_height = 0;
};

} // namespace NaraGame

#endif
