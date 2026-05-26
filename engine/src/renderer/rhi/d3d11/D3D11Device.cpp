#include "renderer/rhi/d3d11/D3D11Device.h"

#if defined(NARA_PLATFORM_WINDOWS)

#include "renderer/rhi/d3d11/D3D11Resources.h"

#include "nara/core/Log.h"

#include <cstdlib> // _countof

namespace nara {

D3D11Device::D3D11Device(const RHIDeviceDesc& desc)
    : m_hwnd(static_cast<HWND>(desc.nativeWindowHandle))
    , m_width(desc.width)
    , m_height(desc.height)
    , m_vsync(desc.vsync) {

    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = m_width;
    scd.BufferDesc.Height = m_height;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = m_hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    UINT flags = 0;
#if !defined(NDEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    const D3D_FEATURE_LEVEL requested[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL obtained{};

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        requested, static_cast<UINT>(_countof(requested)), D3D11_SDK_VERSION,
        &scd, m_swapChain.GetAddressOf(), m_device.GetAddressOf(),
        &obtained, m_context.GetAddressOf());

    // The debug layer requires the "Graphics Tools" feature; retry without it.
    if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
        NARA_WARN("D3D11 debug layer unavailable; creating device without it");
        flags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            requested, static_cast<UINT>(_countof(requested)), D3D11_SDK_VERSION,
            &scd, m_swapChain.GetAddressOf(), m_device.GetAddressOf(),
            &obtained, m_context.GetAddressOf());
    }

    if (FAILED(hr)) {
        NARA_ERROR("D3D11CreateDeviceAndSwapChain failed (0x%08lX)",
                   static_cast<unsigned long>(hr));
        return;
    }

    CreateRenderTarget();
    CreateDefaultStates();
    NARA_INFO("D3D11 device created (feature level 0x%04X)", static_cast<unsigned>(obtained));
}

D3D11Device::~D3D11Device() = default;

void D3D11Device::CreateDefaultStates() {
    D3D11_RASTERIZER_DESC rs{};
    rs.FillMode = D3D11_FILL_SOLID;
    rs.CullMode = D3D11_CULL_NONE; // 2D quads: don't worry about winding
    rs.DepthClipEnable = TRUE;
    m_device->CreateRasterizerState(&rs, m_rasterizer.GetAddressOf());

    D3D11_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = FALSE; // painter's order for 2D
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    m_device->CreateDepthStencilState(&ds, m_depthState.GetAddressOf());

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    m_device->CreateSamplerState(&sd, m_sampler.GetAddressOf());
}

void D3D11Device::CreateRenderTarget() {
    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()))))
        return;
    m_device->CreateRenderTargetView(backBuffer.Get(), nullptr,
                                     m_renderTarget.ReleaseAndGetAddressOf());
}

void D3D11Device::Resize(uint32_t width, uint32_t height) {
    if (!m_swapChain || (width == m_width && height == m_height))
        return;

    m_width = width;
    m_height = height;

    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_renderTarget.Reset();
    m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    CreateRenderTarget();
}

void D3D11Device::BeginFrame() {
    if (!m_context || !m_renderTarget)
        return;

    ID3D11RenderTargetView* targets[] = {m_renderTarget.Get()};
    m_context->OMSetRenderTargets(1, targets, nullptr);

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);

    m_context->RSSetState(m_rasterizer.Get());
    m_context->OMSetDepthStencilState(m_depthState.Get(), 0);
    ID3D11SamplerState* samplers[] = {m_sampler.Get()};
    m_context->PSSetSamplers(0, 1, samplers);
}

void D3D11Device::Clear(const Color& color) {
    if (!m_context || !m_renderTarget)
        return;
    const float rgba[4] = {color.r, color.g, color.b, color.a};
    m_context->ClearRenderTargetView(m_renderTarget.Get(), rgba);
}

void D3D11Device::EndFrame() {}

void D3D11Device::Present() {
    if (m_swapChain)
        m_swapChain->Present(m_vsync ? 1 : 0, 0);
}

// --- Resource creation -----------------------------------------------------

std::unique_ptr<RHIBuffer> D3D11Device::CreateBuffer(const BufferDesc& desc) {
    return std::make_unique<D3D11Buffer>(m_device.Get(), m_context.Get(), desc);
}

std::unique_ptr<RHITexture> D3D11Device::CreateTexture(const TextureDesc& desc) {
    return std::make_unique<D3D11Texture>(m_device.Get(), desc);
}

std::unique_ptr<RHIShader> D3D11Device::CreateShader(const ShaderDesc& desc) {
    return std::make_unique<D3D11Shader>(m_device.Get(), desc);
}

std::unique_ptr<RHIPipeline> D3D11Device::CreatePipeline(const PipelineDesc& desc) {
    return std::make_unique<D3D11Pipeline>(m_device.Get(), desc);
}

// --- Binding + draw --------------------------------------------------------

void D3D11Device::BindPipeline(RHIPipeline* pipeline) {
    if (pipeline)
        static_cast<D3D11Pipeline*>(pipeline)->Bind(m_context.Get());
}

void D3D11Device::BindVertexBuffer(RHIBuffer* buffer, uint32_t stride, uint32_t offset) {
    ID3D11Buffer* native = buffer ? static_cast<D3D11Buffer*>(buffer)->Native() : nullptr;
    ID3D11Buffer* buffers[] = {native};
    const UINT strides[] = {stride};
    const UINT offsets[] = {offset};
    m_context->IASetVertexBuffers(0, 1, buffers, strides, offsets);
}

void D3D11Device::BindIndexBuffer(RHIBuffer* buffer, IndexFormat format) {
    ID3D11Buffer* native = buffer ? static_cast<D3D11Buffer*>(buffer)->Native() : nullptr;
    const DXGI_FORMAT dxgi =
        (format == IndexFormat::Uint16) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
    m_context->IASetIndexBuffer(native, dxgi, 0);
}

void D3D11Device::BindUniformBuffer(uint32_t slot, RHIBuffer* buffer) {
    ID3D11Buffer* native = buffer ? static_cast<D3D11Buffer*>(buffer)->Native() : nullptr;
    ID3D11Buffer* buffers[] = {native};
    m_context->VSSetConstantBuffers(slot, 1, buffers);
    m_context->PSSetConstantBuffers(slot, 1, buffers);
}

void D3D11Device::BindTexture(uint32_t slot, RHITexture* texture) {
    ID3D11ShaderResourceView* srv =
        texture ? static_cast<D3D11Texture*>(texture)->SRV() : nullptr;
    ID3D11ShaderResourceView* views[] = {srv};
    m_context->PSSetShaderResources(slot, 1, views);
}

void D3D11Device::DrawIndexed(uint32_t indexCount, uint32_t firstIndex) {
    m_context->DrawIndexed(indexCount, firstIndex, 0);
}

} // namespace nara

#endif // NARA_PLATFORM_WINDOWS
