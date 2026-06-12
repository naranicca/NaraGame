#include "renderer/rhi/d3d11/D3D11Device.h"

#if defined(NARAGAME_PLATFORM_WINDOWS)

#include "renderer/rhi/d3d11/D3D11Resources.h"

#include "NaraGame/core/Log.h"

#include <cstdlib> // _countof

namespace NaraGame {

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
        NARAGAME_WARN("D3D11 debug layer unavailable; creating device without it");
        flags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            requested, static_cast<UINT>(_countof(requested)), D3D11_SDK_VERSION,
            &scd, m_swapChain.GetAddressOf(), m_device.GetAddressOf(),
            &obtained, m_context.GetAddressOf());
    }

    if (FAILED(hr)) {
        NARAGAME_ERROR("D3D11CreateDeviceAndSwapChain failed (0x%08lX)",
                   static_cast<unsigned long>(hr));
        return;
    }

    CreateRenderTarget();
    CreateDefaultStates();
    NARAGAME_INFO("D3D11 device created (feature level 0x%04X)", static_cast<unsigned>(obtained));
}

D3D11Device::~D3D11Device() = default;

void D3D11Device::CreateDefaultStates() {
    D3D11_SAMPLER_DESC sd{};
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    m_device->CreateSamplerState(&sd, m_linearSampler.GetAddressOf());
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    m_device->CreateSamplerState(&sd, m_pointSampler.GetAddressOf());
}

void D3D11Device::CreateRenderTarget() {
    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()))))
        return;
    m_device->CreateRenderTargetView(backBuffer.Get(), nullptr,
                                     m_renderTarget.ReleaseAndGetAddressOf());

    // Matching depth buffer for the swap chain. Sized to the backbuffer
    // so any 3D pipeline binding the default RTV also gets a valid DSV
    // without bookkeeping in the caller. D24_UNORM_S8 is the workhorse
    // depth format on D3D11 and matches our TextureFormat::Depth24.
    D3D11_TEXTURE2D_DESC dt{};
    dt.Width            = m_width;
    dt.Height           = m_height;
    dt.MipLevels        = 1;
    dt.ArraySize        = 1;
    dt.Format           = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dt.SampleDesc.Count = 1;
    dt.Usage            = D3D11_USAGE_DEFAULT;
    dt.BindFlags        = D3D11_BIND_DEPTH_STENCIL;
    m_device->CreateTexture2D(&dt, nullptr,
                              m_depthTex.ReleaseAndGetAddressOf());
    if (m_depthTex)
        m_device->CreateDepthStencilView(m_depthTex.Get(), nullptr,
                                         m_depthView.ReleaseAndGetAddressOf());
}

void D3D11Device::Resize(uint32_t width, uint32_t height) {
    if (!m_swapChain || (width == m_width && height == m_height))
        return;

    m_width = width;
    m_height = height;

    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_renderTarget.Reset();
    m_depthView.Reset();
    m_depthTex.Reset();
    m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    CreateRenderTarget();
}

void D3D11Device::BeginFrame() {
    if (!m_context || !m_renderTarget)
        return;

    // Reset to backbuffer at the top of every frame so a target left bound
    // by the previous frame's last draw doesn't leak. Games re-bind via
    // SetRenderTarget if they want offscreen work this frame. The depth
    // view goes along with the backbuffer RTV; pipelines that disable
    // depth simply never touch it.
    m_currentRTV = m_renderTarget.Get();
    m_currentDSV = m_depthView.Get();
    ID3D11RenderTargetView* targets[] = {m_currentRTV};
    m_context->OMSetRenderTargets(1, targets, m_currentDSV);

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(m_width);
    viewport.Height = static_cast<float>(m_height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);

    // Raster + depth state are now per-pipeline; BindPipeline sets them.
    // Until a pipeline binds, the context still has whatever state the
    // last frame left around, which is fine because Clear() doesn't use
    // raster state.

    // Default sampler so any draw that fires before a BindTexture (e.g. a
    // colored quad using the renderer's white texture) still samples cleanly.
    ID3D11SamplerState* samplers[] = {m_linearSampler.Get()};
    m_context->PSSetSamplers(0, 1, samplers);
}

void D3D11Device::Clear(const Color& color) {
    if (!m_context || !m_currentRTV)
        return;
    const float rgba[4] = {color.r, color.g, color.b, color.a};
    m_context->ClearRenderTargetView(m_currentRTV, rgba);
}

void D3D11Device::ClearDepth(float depth) {
    if (!m_context || !m_currentDSV) return;
    m_context->ClearDepthStencilView(m_currentDSV, D3D11_CLEAR_DEPTH,
                                     depth, 0);
}

void D3D11Device::SetRenderTarget(RHITexture* target) {
    if (!m_context) return;
    if (target == nullptr) {
        // Back to backbuffer + its matching depth buffer.
        m_currentRTV = m_renderTarget.Get();
        m_currentDSV = m_depthView.Get();
        ID3D11RenderTargetView* targets[] = {m_currentRTV};
        m_context->OMSetRenderTargets(1, targets, m_currentDSV);
        D3D11_VIEWPORT vp{};
        vp.Width = static_cast<float>(m_width);
        vp.Height = static_cast<float>(m_height);
        vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;
        m_context->RSSetViewports(1, &vp);
        return;
    }
    auto* tex = static_cast<D3D11Texture*>(target);
    ID3D11RenderTargetView* rtv = tex->RTV();
    ID3D11DepthStencilView* dsv = tex->DSV();
    if (rtv == nullptr && dsv == nullptr) {
        NARAGAME_ERROR("SetRenderTarget: texture wasn't created with renderTarget=true and isn't a depth target");
        return;
    }
    // Unbind the texture from any sampler slot it might still be living in,
    // otherwise D3D11 silently nulls the RTV (an SRV/RTV can't both be bound).
    ID3D11ShaderResourceView* nullSrv[1] = {nullptr};
    for (uint32_t i = 0; i < 4; ++i)
        m_context->PSSetShaderResources(i, 1, nullSrv);

    m_currentRTV = rtv;
    m_currentDSV = dsv;
    ID3D11RenderTargetView* targets[] = {m_currentRTV};
    // Depth-only RT (shadow map): bind null colour + the texture's DSV
    // so the geometry just writes depth. Otherwise bind RTV + the
    // backbuffer depth buffer so a 3D pass into an offscreen colour
    // target still gets z testing.
    if (rtv == nullptr) {
        m_context->OMSetRenderTargets(0, nullptr, dsv);
    } else {
        m_context->OMSetRenderTargets(1, targets,
                                      dsv ? dsv : m_depthView.Get());
        if (dsv == nullptr) m_currentDSV = m_depthView.Get();
    }
    D3D11_VIEWPORT vp{};
    vp.Width  = static_cast<float>(tex->Width());
    vp.Height = static_cast<float>(tex->Height());
    vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);
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
    return std::make_unique<D3D11Texture>(m_device.Get(), m_context.Get(), desc);
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

    // Pick the sampler that matches this texture's filter pref. Renderer2D
    // batches per-texture, so each batch's first BindTexture sets the right
    // sampler before its draw. Slot 0 mirrors the shader's single sSampler.
    const TextureFilter filter =
        texture ? texture->Filter() : TextureFilter::Linear;
    ID3D11SamplerState* samplers[] = {
        filter == TextureFilter::Point ? m_pointSampler.Get()
                                       : m_linearSampler.Get()
    };
    m_context->PSSetSamplers(0, 1, samplers);
}

void D3D11Device::DrawIndexed(uint32_t indexCount, uint32_t firstIndex) {
    m_context->DrawIndexed(indexCount, firstIndex, 0);
}

} // namespace NaraGame

#endif // NARAGAME_PLATFORM_WINDOWS
