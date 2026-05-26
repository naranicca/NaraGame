#pragma once

#include <cstdint>
#include <vector>

namespace NaraGame {

// Linear RGBA color in [0, 1]. Doubles as the clear color.
struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    constexpr Color() = default;
    constexpr Color(float r_, float g_, float b_, float a_ = 1.0f)
        : r(r_), g(g_), b(b_), a(a_) {}

    static constexpr Color Black() { return {0.0f, 0.0f, 0.0f, 1.0f}; }
    static constexpr Color White() { return {1.0f, 1.0f, 1.0f, 1.0f}; }
    static constexpr Color CornflowerBlue() { return {0.392f, 0.584f, 0.929f, 1.0f}; }
};

// Which native API a device is backed by.
enum class RHIBackend { None, D3D11, D3D12, Metal, Vulkan };

// ---------------------------------------------------------------------------
// Resource description enums
// ---------------------------------------------------------------------------

enum class BufferType { Vertex, Index, Uniform };
enum class BufferUsage { Static, Dynamic };       // Dynamic = CPU-updated per frame
enum class IndexFormat { Uint16, Uint32 };
enum class TextureFormat { RGBA8 };
enum class PrimitiveTopology { TriangleList };
enum class BlendMode { None, Alpha };             // Alpha = src-over

// Vertex attribute element type.
enum class VertexFormat { Float, Float2, Float3, Float4, UByte4Norm };

// One input element fed to the vertex shader.
struct VertexAttribute {
    const char* semantic = "";   // HLSL semantic name, e.g. "POSITION"
    uint32_t semanticIndex = 0;  // e.g. TEXCOORD0 -> index 0
    VertexFormat format = VertexFormat::Float;
    uint32_t offset = 0;         // byte offset within a vertex
};

// Describes the layout of one vertex in a vertex buffer.
struct VertexLayout {
    std::vector<VertexAttribute> attributes;
    uint32_t stride = 0;         // bytes per vertex
};

} // namespace NaraGame
