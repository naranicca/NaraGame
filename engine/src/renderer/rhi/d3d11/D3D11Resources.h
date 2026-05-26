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
    D3D11Texture(ID3D11Device* device, const TextureDesc& desc);

    uint32_t Width() const override { return m_width; }
    uint32_t Height() const override { return m_height; }

    ID3D11ShaderResourceView* SRV() const { return m_srv.Get(); }

private:
    ComPtr<ID3D11ShaderResourceView> m_srv;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
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
    D3D11_PRIMITIVE_TOPOLOGY m_topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
};

} // namespace NaraGame

#endif // NARAGAME_PLATFORM_WINDOWS
