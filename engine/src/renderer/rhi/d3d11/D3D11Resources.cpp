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

    const bool isDepth = (desc.format == TextureFormat::Depth24);
    const bool isHDR   = (desc.format == TextureFormat::RGBA16F);
    const bool isCube  = desc.isCube;

    D3D11_TEXTURE2D_DESC td{};
    td.Width = desc.width;
    td.Height = desc.height;
    td.MipLevels = 1;
    td.ArraySize = isCube ? 6u : 1u;
    td.MiscFlags = isCube ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0u;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    // Depth textures hold the raw typeless format on the resource so we
    // can put both a DSV (D24S8 view) and an SRV (R24X8 view) on it; the
    // SRV makes the depth readable in a later pass (shadow mapping).
    if (isDepth) {
        td.Format    = DXGI_FORMAT_R24G8_TYPELESS;
        td.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    } else if (isHDR) {
        td.Format    = DXGI_FORMAT_R16G16B16A16_FLOAT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (desc.renderTarget) td.BindFlags |= D3D11_BIND_RENDER_TARGET;
    } else {
        td.Format    = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (desc.renderTarget) td.BindFlags |= D3D11_BIND_RENDER_TARGET;
    }

    // Cubemap upload: 6 contiguous face buffers in desc.pixels, laid out
    // +X, -X, +Y, -Y, +Z, -Z. Each face is desc.width * desc.height
    // pixels at (isHDR ? 8 : 4) bytes per pixel.
    const uint32_t bytesPerPixel = isHDR ? 8u : 4u;
    const uint32_t pitch         = desc.width * bytesPerPixel;
    const uint32_t faceBytes     = pitch * desc.height;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem     = desc.pixels;
    init.SysMemPitch = pitch;

    D3D11_SUBRESOURCE_DATA cubeInit[6]{};
    if (isCube) {
        for (uint32_t f = 0; f < 6; ++f) {
            cubeInit[f].pSysMem =
                static_cast<const uint8_t*>(desc.pixels) + size_t(f) * faceBytes;
            cubeInit[f].SysMemPitch = pitch;
        }
    }

    // Render targets and depth textures start undefined and get filled by
    // GPU writes; CPU pixels are accepted for RGBA8 and RGBA16F sources
    // (the latter expects pre-converted half-float data).
    const D3D11_SUBRESOURCE_DATA* initPtr = nullptr;
    if (desc.pixels && !desc.renderTarget && !isDepth) {
        initPtr = isCube ? cubeInit : &init;
    }

    HRESULT hr =
        device->CreateTexture2D(&td, initPtr, m_texture.GetAddressOf());
    if (FAILED(hr)) {
        NARAGAME_ERROR("CreateTexture2D failed (0x%08lX)", static_cast<unsigned long>(hr));
        return;
    }

    if (isDepth) {
        // SRV samples the depth as a single-channel float in the .r slot.
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format        = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MipLevels = 1;
        hr = device->CreateShaderResourceView(m_texture.Get(), &sd,
                                              m_srv.GetAddressOf());
        if (FAILED(hr))
            NARAGAME_ERROR("CreateShaderResourceView(depth) failed (0x%08lX)",
                           static_cast<unsigned long>(hr));
        // DSV uses the matching depth-stencil typed format.
        D3D11_DEPTH_STENCIL_VIEW_DESC dd{};
        dd.Format        = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        hr = device->CreateDepthStencilView(m_texture.Get(), &dd,
                                            m_dsv.GetAddressOf());
        if (FAILED(hr))
            NARAGAME_ERROR("CreateDepthStencilView failed (0x%08lX)",
                           static_cast<unsigned long>(hr));
        return;
    }

    if (isCube) {
        // Cubemap SRV so the shader can declare `TextureCube` and sample
        // by 3D direction. Mip levels = 1 to match the texture, matching
        // the single-level upload above.
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = td.Format;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        sd.TextureCube.MipLevels = 1;
        sd.TextureCube.MostDetailedMip = 0;
        hr = device->CreateShaderResourceView(m_texture.Get(), &sd,
                                              m_srv.GetAddressOf());
        if (FAILED(hr))
            NARAGAME_ERROR("CreateShaderResourceView(cube) failed (0x%08lX)",
                           static_cast<unsigned long>(hr));
    } else {
        hr = device->CreateShaderResourceView(m_texture.Get(), nullptr, m_srv.GetAddressOf());
        if (FAILED(hr))
            NARAGAME_ERROR("CreateShaderResourceView failed (0x%08lX)", static_cast<unsigned long>(hr));
    }

    if (desc.renderTarget) {
        hr = device->CreateRenderTargetView(m_texture.Get(), nullptr, m_rtv.GetAddressOf());
        if (FAILED(hr))
            NARAGAME_ERROR("CreateRenderTargetView failed (0x%08lX)", static_cast<unsigned long>(hr));
    }
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

    // Blend state. Alpha channel stays src-over (ONE / INV_SRC_ALPHA) for
    // every enabled mode so a buffer downstream still has usable alpha.
    D3D11_BLEND_DESC blend{};
    auto& rt = blend.RenderTarget[0];
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    switch (desc.blend) {
        case BlendMode::Alpha:
            rt.BlendEnable    = TRUE;
            rt.SrcBlend       = D3D11_BLEND_SRC_ALPHA;
            rt.DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
            rt.BlendOp        = D3D11_BLEND_OP_ADD;
            rt.SrcBlendAlpha  = D3D11_BLEND_ONE;
            rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            rt.BlendOpAlpha   = D3D11_BLEND_OP_ADD;
            break;
        case BlendMode::Additive:
            // src.rgb * src.a + dst.rgb -- glow / spark / explosion lighting
            rt.BlendEnable    = TRUE;
            rt.SrcBlend       = D3D11_BLEND_SRC_ALPHA;
            rt.DestBlend      = D3D11_BLEND_ONE;
            rt.BlendOp        = D3D11_BLEND_OP_ADD;
            rt.SrcBlendAlpha  = D3D11_BLEND_ONE;
            rt.DestBlendAlpha = D3D11_BLEND_ONE;
            rt.BlendOpAlpha   = D3D11_BLEND_OP_ADD;
            break;
        case BlendMode::Multiply:
            // src.rgb * dst.rgb -- shadow / colour grade / fog overlay
            rt.BlendEnable    = TRUE;
            rt.SrcBlend       = D3D11_BLEND_DEST_COLOR;
            rt.DestBlend      = D3D11_BLEND_ZERO;
            rt.BlendOp        = D3D11_BLEND_OP_ADD;
            rt.SrcBlendAlpha  = D3D11_BLEND_ONE;
            rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            rt.BlendOpAlpha   = D3D11_BLEND_OP_ADD;
            break;
        case BlendMode::None:
        default:
            // Pass-through: src replaces dst with no blending.
            rt.BlendEnable = FALSE;
            break;
    }
    device->CreateBlendState(&blend, m_blendState.GetAddressOf());

    // Depth state. 2D pipelines pass depthTest = false here and end up
    // with "depth disabled, no write" -- equivalent to the global state
    // the device used before this refactor. 3D pipelines flip both bools
    // and pick a comparison mode.
    D3D11_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable    = desc.depthTest ? TRUE : FALSE;
    ds.DepthWriteMask = desc.depthWrite ? D3D11_DEPTH_WRITE_MASK_ALL
                                        : D3D11_DEPTH_WRITE_MASK_ZERO;
    switch (desc.depthCompare) {
        case CompareFunc::Less:         ds.DepthFunc = D3D11_COMPARISON_LESS;          break;
        case CompareFunc::LessEqual:    ds.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;    break;
        case CompareFunc::Greater:      ds.DepthFunc = D3D11_COMPARISON_GREATER;       break;
        case CompareFunc::GreaterEqual: ds.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL; break;
        case CompareFunc::Equal:        ds.DepthFunc = D3D11_COMPARISON_EQUAL;         break;
        case CompareFunc::NotEqual:     ds.DepthFunc = D3D11_COMPARISON_NOT_EQUAL;     break;
        case CompareFunc::Always:       ds.DepthFunc = D3D11_COMPARISON_ALWAYS;        break;
        case CompareFunc::Never:        ds.DepthFunc = D3D11_COMPARISON_NEVER;         break;
    }
    device->CreateDepthStencilState(&ds, m_depthState.GetAddressOf());

    // Rasterizer state. Cull None is the 2D default; Back is the 3D
    // default (front face = CCW in world space, matching glTF / most
    // exporter conventions). DepthClipEnable stays on so geometry past
    // the far plane is dropped cleanly.
    D3D11_RASTERIZER_DESC rs{};
    rs.FillMode        = D3D11_FILL_SOLID;
    switch (desc.cullMode) {
        case CullMode::None:  rs.CullMode = D3D11_CULL_NONE;  break;
        case CullMode::Back:  rs.CullMode = D3D11_CULL_BACK;  break;
        case CullMode::Front: rs.CullMode = D3D11_CULL_FRONT; break;
    }
    rs.FrontCounterClockwise = TRUE; // glTF / OpenGL convention
    rs.DepthClipEnable       = TRUE;
    device->CreateRasterizerState(&rs, m_rasterState.GetAddressOf());

    m_topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

void D3D11Pipeline::Bind(ID3D11DeviceContext* context) const {
    context->IASetInputLayout(m_inputLayout.Get());
    context->IASetPrimitiveTopology(m_topology);
    context->VSSetShader(m_vs.Get(), nullptr, 0);
    context->PSSetShader(m_ps.Get(), nullptr, 0);

    const float blendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context->OMSetBlendState(m_blendState.Get(), blendFactor, 0xFFFFFFFF);
    context->OMSetDepthStencilState(m_depthState.Get(), 0);
    context->RSSetState(m_rasterState.Get());
}

} // namespace NaraGame

#endif // NARAGAME_PLATFORM_WINDOWS
