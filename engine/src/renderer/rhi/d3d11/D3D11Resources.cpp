#include "renderer/rhi/d3d11/D3D11Resources.h"

#if defined(NARAGAME_PLATFORM_WINDOWS)

#include "NaraGame/core/Log.h"

#include <cstring>
#include <d3dcompiler.h>

namespace NaraGame {

static DXGI_FORMAT ToDXGI(VertexFormat format) {
    switch (format) {
        case VertexFormat::Float:      return DXGI_FORMAT_R32_FLOAT;
        case VertexFormat::Float2:     return DXGI_FORMAT_R32G32_FLOAT;
        case VertexFormat::Float3:     return DXGI_FORMAT_R32G32B32_FLOAT;
        case VertexFormat::Float4:     return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case VertexFormat::UByte4Norm: return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
    return DXGI_FORMAT_UNKNOWN;
}

// ===========================================================================
// D3D11Buffer
// ===========================================================================
D3D11Buffer::D3D11Buffer(ID3D11Device* device, ID3D11DeviceContext* context,
                         const BufferDesc& desc)
    : m_context(context), m_size(desc.size), m_dynamic(desc.usage == BufferUsage::Dynamic) {

    UINT byteWidth = static_cast<UINT>(desc.size);
    if (desc.type == BufferType::Uniform)
        byteWidth = (byteWidth + 15u) & ~15u; // constant buffers: 16-byte multiple

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = byteWidth;
    bd.Usage = m_dynamic ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
    bd.CPUAccessFlags = m_dynamic ? D3D11_CPU_ACCESS_WRITE : 0;
    switch (desc.type) {
        case BufferType::Vertex:  bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; break;
        case BufferType::Index:   bd.BindFlags = D3D11_BIND_INDEX_BUFFER; break;
        case BufferType::Uniform: bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER; break;
    }

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = desc.initialData;
    const HRESULT hr =
        device->CreateBuffer(&bd, desc.initialData ? &init : nullptr, m_buffer.GetAddressOf());
    if (FAILED(hr))
        NARAGAME_ERROR("CreateBuffer failed (0x%08lX)", static_cast<unsigned long>(hr));
}

void D3D11Buffer::Update(const void* data, size_t size, size_t offset) {
    if (!m_buffer || data == nullptr)
        return;

    if (m_dynamic) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(m_context->Map(m_buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            std::memcpy(static_cast<char*>(mapped.pData) + offset, data, size);
        m_context->Unmap(m_buffer.Get(), 0);
    } else {
        m_context->UpdateSubresource(m_buffer.Get(), 0, nullptr, data, 0, 0);
    }
}

// ===========================================================================
// D3D11Texture
// ===========================================================================
D3D11Texture::D3D11Texture(ID3D11Device* device, ID3D11DeviceContext* context,
                           const TextureDesc& desc)
    : m_context(context), m_width(desc.width), m_height(desc.height), m_filter(desc.filter) {

    D3D11_TEXTURE2D_DESC td{};
    td.Width = desc.width;
    td.Height = desc.height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = desc.pixels;
    init.SysMemPitch = desc.width * 4; // RGBA8

    HRESULT hr =
        device->CreateTexture2D(&td, desc.pixels ? &init : nullptr, m_texture.GetAddressOf());
    if (FAILED(hr)) {
        NARAGAME_ERROR("CreateTexture2D failed (0x%08lX)", static_cast<unsigned long>(hr));
        return;
    }

    hr = device->CreateShaderResourceView(m_texture.Get(), nullptr, m_srv.GetAddressOf());
    if (FAILED(hr))
        NARAGAME_ERROR("CreateShaderResourceView failed (0x%08lX)", static_cast<unsigned long>(hr));
}

bool D3D11Texture::UpdatePixels(const void* pixels) {
    if (!pixels || !m_texture || !m_context) return false;
    // RGBA8 only (matches the DXGI_FORMAT in the constructor). Caller must
    // hand us tightly packed top-down pixel data of exactly the texture's
    // current width x height; a mismatched size would silently land on the
    // wrong rows.
    m_context->UpdateSubresource(m_texture.Get(), 0, nullptr, pixels, m_width * 4, 0);
    return true;
}

// ===========================================================================
// D3D11Shader
// ===========================================================================
static ComPtr<ID3DBlob> CompileHLSL(const std::string& source, const std::string& entry,
                                    const char* target, const char* debugName) {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if !defined(NDEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;
    const HRESULT hr =
        D3DCompile(source.data(), source.size(), debugName, nullptr, nullptr, entry.c_str(),
                   target, flags, 0, code.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(hr)) {
        NARAGAME_ERROR("HLSL compile failed [%s/%s]: %s", debugName, target,
                   errors ? static_cast<const char*>(errors->GetBufferPointer()) : "(no message)");
        return nullptr;
    }
    return code;
}

D3D11Shader::D3D11Shader(ID3D11Device* device, const ShaderDesc& desc) {
    m_vsBlob = CompileHLSL(desc.vertexSource, desc.vertexEntry, "vs_5_0", desc.debugName.c_str());
    ComPtr<ID3DBlob> psBlob =
        CompileHLSL(desc.pixelSource, desc.pixelEntry, "ps_5_0", desc.debugName.c_str());

    if (m_vsBlob)
        device->CreateVertexShader(m_vsBlob->GetBufferPointer(), m_vsBlob->GetBufferSize(),
                                   nullptr, m_vs.GetAddressOf());
    if (psBlob)
        device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr,
                                  m_ps.GetAddressOf());
}

// ===========================================================================
// D3D11Pipeline
// ===========================================================================
D3D11Pipeline::D3D11Pipeline(ID3D11Device* device, const PipelineDesc& desc) {
    auto* shader = static_cast<D3D11Shader*>(desc.shader);
    if (shader == nullptr || !shader->Valid() || shader->VSBytecode() == nullptr) {
        NARAGAME_ERROR("CreatePipeline: invalid shader");
        return;
    }
    m_vs = shader->VS();
    m_ps = shader->PS();

    // Input layout (validated against the vertex shader bytecode).
    std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
    elements.reserve(desc.vertexLayout.attributes.size());
    for (const VertexAttribute& attr : desc.vertexLayout.attributes) {
        D3D11_INPUT_ELEMENT_DESC e{};
        e.SemanticName = attr.semantic;
        e.SemanticIndex = attr.semanticIndex;
        e.Format = ToDXGI(attr.format);
        e.InputSlot = 0;
        e.AlignedByteOffset = attr.offset;
        e.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
        elements.push_back(e);
    }
    const HRESULT hr = device->CreateInputLayout(
        elements.data(), static_cast<UINT>(elements.size()),
        shader->VSBytecode()->GetBufferPointer(), shader->VSBytecode()->GetBufferSize(),
        m_inputLayout.GetAddressOf());
    if (FAILED(hr))
        NARAGAME_ERROR("CreateInputLayout failed (0x%08lX)", static_cast<unsigned long>(hr));

    // Blend state.
    D3D11_BLEND_DESC blend{};
    auto& rt = blend.RenderTarget[0];
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (desc.blend == BlendMode::Alpha) {
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
        rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        rt.BlendOp = D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D11_BLEND_ONE;
        rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    }
    device->CreateBlendState(&blend, m_blendState.GetAddressOf());

    m_topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

void D3D11Pipeline::Bind(ID3D11DeviceContext* context) const {
    context->IASetInputLayout(m_inputLayout.Get());
    context->IASetPrimitiveTopology(m_topology);
    context->VSSetShader(m_vs.Get(), nullptr, 0);
    context->PSSetShader(m_ps.Get(), nullptr, 0);

    const float blendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context->OMSetBlendState(m_blendState.Get(), blendFactor, 0xFFFFFFFF);
}

} // namespace NaraGame

#endif // NARAGAME_PLATFORM_WINDOWS
