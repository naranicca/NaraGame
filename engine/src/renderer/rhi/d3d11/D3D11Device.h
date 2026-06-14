#pragma once

#include "NaraGame/core/Platform.h"

#if defined(NARAGAME_PLATFORM_WINDOWS)

#include "NaraGame/renderer/rhi/RHIDevice.h"

#include <d3d11.h>
#include <wrl/client.h>

namespace NaraGame {

// Direct3D 11 implementation of the RHI. Owns the device, immediate context,
// swap chain and back-buffer render target view.
class D3D11Device final : public RHIDevice {
public:
    explicit D3D11Device(const RHIDeviceDesc& desc);
    ~D3D11Device() override;

    void Resize(uint32_t width, uint32_t height) override;

    void BeginFrame() override;
    void Clear(const Color& color) override;
    void ClearDepth(float depth = 1.0f) override;
    void EndFrame() override;
    void Present() override;

    void SetRenderTarget(RHITexture* target) override;
    void SetRenderTargets(RHITexture* colorTex, RHITexture* depthTex) override;

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
    ComPtr<ID3D11RenderTargetView> m_renderTarget;   // backbuffer RTV
    // Depth buffer matching the swapchain. Recreated on Resize. Every
    // 3D pipeline gets to see this DSV in BeginFrame so depth testing
    // just works; 2D pipelines that disable depth simply don't touch it.
    ComPtr<ID3D11Texture2D>        m_depthTex;
    ComPtr<ID3D11DepthStencilView> m_depthView;
    ID3D11RenderTargetView* m_currentRTV = nullptr;  // either backbuffer or an RT texture's RTV
    ID3D11DepthStencilView* m_currentDSV = nullptr;  // either the backbuffer depth or a target's DSV

    // Default samplers (one per filter mode). BindTexture picks the
    // matching one from the texture's TextureDesc::filter. Both clamp
    // on every axis. Per-pipeline raster and depth states live on each
    // D3D11Pipeline -- the device no longer keeps a global default.
    ComPtr<ID3D11SamplerState> m_linearSampler;
    ComPtr<ID3D11SamplerState> m_pointSampler;
};

} // namespace NaraGame

#endif // NARAGAME_PLATFORM_WINDOWS
