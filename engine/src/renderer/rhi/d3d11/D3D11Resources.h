#pragma once

#include "NaraGame/core/Platform.h"

#if defined(NARAGAME_PLATFORM_WINDOWS)

#include "NaraGame/renderer/rhi/RHIResources.h"

#include <d3d11.h>
#include <wrl/client.h>

namespace NaraGame {

template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

// --- Buffer ---------------------------------------------------------------
class D3D11Buffer final : public RHIBuffer {
public:
    D3D11Buffer(ID3D11Device* device, ID3D11DeviceContext* context, const BufferDesc& desc);

    void Update(const void* data, size_t size, size_t offset) override;
    size_t Size() const override { return m_size; }

    ID3D11Buffer* Native() const { return m_buffer.Get(); }

private:
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11Buffer> m_buffer;
    size_t m_size = 0;
    bool m_dynamic = false;
};

// --- Texture --------------------------------------------------------------
class D3D11Texture final : public RHITexture {
public:
    D3D11Texture(ID3D11Device* device, ID3D11DeviceContext* context, const TextureDesc& desc);

    uint32_t Width() const override { return m_width; }
    uint32_t Height() const override { return m_height; }
    TextureFilter Filter() const override { return m_filter; }

    bool UpdatePixels(const void* pixels) override;

    ID3D11ShaderResourceView* SRV() const { return m_srv.Get(); }
    // Non-null only when the texture was created with renderTarget = true.
    ID3D11RenderTargetView* RTV() const { return m_rtv.Get(); }
    // Non-null only for depth-format textures; shadow-map RTs use this
    // when set as the bound depth target.
    ID3D11DepthStencilView* DSV() const { return m_dsv.Get(); }

private:
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11Texture2D> m_texture;
    ComPtr<ID3D11ShaderResourceView> m_srv;
    ComPtr<ID3D11RenderTargetView> m_rtv;     // RT-only (colour)
    ComPtr<ID3D11DepthStencilView> m_dsv;     // RT-only (depth format)
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    TextureFilter m_filter = TextureFilter::Linear;
};

// --- Shader ---------------------------------------------------------------
class D3D11Shader final : public RHIShader {
public:
    D3D11Shader(ID3D11Device* device, const ShaderDesc& desc);

    bool Valid() const override { return m_vs && m_ps; }

    ID3D11VertexShader* VS() const { return m_vs.Get(); }
    ID3D11PixelShader* PS() const { return m_ps.Get(); }
    ID3DBlob* VSBytecode() const { return m_vsBlob.Get(); }

private:
    ComPtr<ID3D11VertexShader> m_vs;
    ComPtr<ID3D11PixelShader> m_ps;
    ComPtr<ID3DBlob> m_vsBlob;
};

// --- Pipeline -------------------------------------------------------------
class D3D11Pipeline final : public RHIPipeline {
public:
    D3D11Pipeline(ID3D11Device* device, const PipelineDesc& desc);

    void Bind(ID3D11DeviceContext* context) const;

private:
    ComPtr<ID3D11VertexShader> m_vs;
    ComPtr<ID3D11PixelShader> m_ps;
    ComPtr<ID3D11InputLayout> m_inputLayout;
    ComPtr<ID3D11BlendState> m_blendState;
    // Per-pipeline depth + raster state so 2D and 3D pipelines can flip
    // depth on/off and cull mode independently. The 2D pipelines
    // (depthTest = false, cullMode = None) get equivalent state to the
    // pre-3D global default.
    ComPtr<ID3D11DepthStencilState> m_depthState;
    ComPtr<ID3D11RasterizerState> m_rasterState;
    D3D11_PRIMITIVE_TOPOLOGY m_topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
};

} // namespace NaraGame

#endif // NARAGAME_PLATFORM_WINDOWS
