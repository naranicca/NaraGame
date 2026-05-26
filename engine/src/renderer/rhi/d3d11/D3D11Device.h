#pragma once

#include "nara/core/Platform.h"

#if defined(NARA_PLATFORM_WINDOWS)

#include "nara/renderer/rhi/RHIDevice.h"

#include <d3d11.h>
#include <wrl/client.h>

namespace nara {

// Direct3D 11 implementation of the RHI. Owns the device, immediate context,
// swap chain and back-buffer render target view.
class D3D11Device final : public RHIDevice {
public:
    explicit D3D11Device(const RHIDeviceDesc& desc);
    ~D3D11Device() override;

    void Resize(uint32_t width, uint32_t height) override;

    void BeginFrame() override;
    void Clear(const Color& color) override;
    void EndFrame() override;
    void Present() override;

    std::unique_ptr<RHIBuffer> CreateBuffer(const BufferDesc& desc) override;
    std::unique_ptr<RHITexture> CreateTexture(const TextureDesc& desc) override;
    std::unique_ptr<RHIShader> CreateShader(const ShaderDesc& desc) override;
    std::unique_ptr<RHIPipeline> CreatePipeline(const PipelineDesc& desc) override;

    void BindPipeline(RHIPipeline* pipeline) override;
    void BindVertexBuffer(RHIBuffer* buffer, uint32_t stride, uint32_t offset) override;
    void BindIndexBuffer(RHIBuffer* buffer, IndexFormat format) override;
    void BindUniformBuffer(uint32_t slot, RHIBuffer* buffer) override;
    void BindTexture(uint32_t slot, RHITexture* texture) override;
    void DrawIndexed(uint32_t indexCount, uint32_t firstIndex) override;

    RHIBackend Backend() const override { return RHIBackend::D3D11; }
    const char* BackendName() const override { return "Direct3D 11"; }

private:
    template <typename T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;

    void CreateRenderTarget();
    void CreateDefaultStates();

    HWND m_hwnd = nullptr;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    bool m_vsync = true;

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGISwapChain> m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_renderTarget;

    // Fixed-function states shared by 2D rendering.
    ComPtr<ID3D11RasterizerState> m_rasterizer;   // cull none, solid fill
    ComPtr<ID3D11DepthStencilState> m_depthState; // depth test disabled
    ComPtr<ID3D11SamplerState> m_sampler;         // linear, clamp
};

} // namespace nara

#endif // NARA_PLATFORM_WINDOWS
