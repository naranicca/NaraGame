#pragma once

#include "NaraGame/renderer/rhi/RHITypes.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace NaraGame {

// ---------------------------------------------------------------------------
// GPU buffer (vertex / index / uniform).
// ---------------------------------------------------------------------------
struct BufferDesc {
    BufferType type = BufferType::Vertex;
    BufferUsage usage = BufferUsage::Static;
    size_t size = 0;               // bytes
    const void* initialData = nullptr;
};

class RHIBuffer {
public:
    virtual ~RHIBuffer() = default;

    // Upload CPU data. Dynamic buffers only support offset == 0 (full discard).
    virtual void Update(const void* data, size_t size, size_t offset = 0) = 0;
    virtual size_t Size() const = 0;
};

// ---------------------------------------------------------------------------
// 2D texture.
// ---------------------------------------------------------------------------
struct TextureDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    TextureFormat format = TextureFormat::RGBA8;
    TextureFilter filter = TextureFilter::Linear;
    const void* pixels = nullptr;  // tightly packed, top-to-bottom
    // When true the texture is created with render-target usage so the
    // device can draw into it via SetRenderTarget(). pixels is ignored on
    // RT textures; they start at undefined contents until first drawn into.
    bool renderTarget = false;
    // When true the texture has 6 array layers laid out as cube faces
    // (+X, -X, +Y, -Y, +Z, -Z in that order) and the SRV is a TextureCube
    // (or TextureCubeArray when arrayLayers > 1). `pixels` is treated as
    // arrayLayers * 6 sequential face buffers, each
    // (width * height * bytesPerPixel) long. width must equal height.
    bool isCube = false;
    // Logical layer count. For 2D arrays this is the array size; for
    // cubes it's the number of cubes (so the D3D11 backend asks for
    // ArraySize = 6 * arrayLayers). 1 keeps the texture single-layer.
    uint32_t arrayLayers = 1;
};

class RHITexture {
public:
    virtual ~RHITexture() = default;
    virtual uint32_t Width() const = 0;
    virtual uint32_t Height() const = 0;
    virtual TextureFilter Filter() const { return TextureFilter::Linear; }

    // Replace pixels in place. The new data MUST match the texture's current
    // width, height, and RGBA8 stride; mismatched calls fail and leave the
    // texture unchanged. Used by asset hot reload so atlases / SRVs that
    // already hold this texture's address stay valid across the swap.
    // Backends that don't implement live updates return false.
    virtual bool UpdatePixels(const void* /*pixels*/) { return false; }
};

// ---------------------------------------------------------------------------
// Shader program (vertex + pixel), compiled from source.
// ---------------------------------------------------------------------------
struct ShaderDesc {
    std::string vertexSource;      // HLSL (used by the D3D11 backend)
    std::string pixelSource;       // HLSL
    std::string vertexEntry = "VSMain";
    std::string pixelEntry = "PSMain";
    std::string debugName = "shader";

    // Precompiled SPIR-V (used by the Vulkan backend). Optional.
    const uint32_t* vertexSpirv = nullptr;
    size_t vertexSpirvSize = 0; // bytes
    const uint32_t* pixelSpirv = nullptr;
    size_t pixelSpirvSize = 0; // bytes
};

class RHIShader {
public:
    virtual ~RHIShader() = default;
    virtual bool Valid() const = 0;
};

// ---------------------------------------------------------------------------
// Pipeline: shader + vertex layout + fixed-function state (PSO equivalent).
// ---------------------------------------------------------------------------
struct PipelineDesc {
    RHIShader* shader = nullptr;
    VertexLayout vertexLayout;
    BlendMode blend = BlendMode::Alpha;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    // depthTest gates whether the fragment runs the depthCompare against
    // the depth buffer. depthWrite gates whether passing fragments write
    // their depth back. Both default to off so 2D pipelines stay painter's-
    // order without touching the depth attachment. 3D pipelines flip
    // both on with Less to get the usual "closer wins, write the new z".
    bool depthTest = false;
    bool depthWrite = false;
    CompareFunc depthCompare = CompareFunc::Less;
    // 2D defaults to no culling so a flipped sprite quad always draws;
    // 3D meshes set this to Back and expect counter-clockwise front faces.
    CullMode cullMode = CullMode::None;
};

class RHIPipeline {
public:
    virtual ~RHIPipeline() = default;
};

} // namespace NaraGame
