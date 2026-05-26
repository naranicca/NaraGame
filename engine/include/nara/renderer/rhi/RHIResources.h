#pragma once

#include "nara/renderer/rhi/RHITypes.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace nara {

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
    const void* pixels = nullptr;  // tightly packed, top-to-bottom
};

class RHITexture {
public:
    virtual ~RHITexture() = default;
    virtual uint32_t Width() const = 0;
    virtual uint32_t Height() const = 0;
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
};

class RHIPipeline {
public:
    virtual ~RHIPipeline() = default;
};

} // namespace nara
