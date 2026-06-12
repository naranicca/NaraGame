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
// RGBA8 is the colour format every 2D/3D pass uses for color buffers and
// asset textures. Depth24 is a 24-bit depth + 8-bit stencil texture --
// used by the swapchain's default depth buffer and by shadow-map RTs.
enum class TextureFormat { RGBA8, Depth24 };
enum class PrimitiveTopology { TriangleList };

// Depth comparison for pipelines that test depth. `Less` matches the
// usual 0=near, 1=far convention -- closer fragments win. `Always`
// disables the test in practice and matches the "no depth" 2D path.
enum class CompareFunc { Less, LessEqual, Greater, GreaterEqual, Equal, NotEqual, Always, Never };

// Triangle culling. 2D pipelines run with None so a sprite quad with
// either winding draws; 3D pipelines default to Back so back faces drop
// out cheaply.
enum class CullMode { None, Back, Front };
// Source-over alpha is the default for sprite batches; Additive glows
// (particles, lights, slash VFX) ignore the destination's alpha and just
// add the source colour; Multiply darkens by the source (shadows, fog
// overlays). All three blend the destination alpha back in so a chained
// pipeline that reads the buffer (e.g. UI on top) still sees usable alpha.
enum class BlendMode { None, Alpha, Additive, Multiply };

// Sampler filter mode chosen per-texture at creation time. Linear blurs on
// upscale (good for soft sprites and photos); Point keeps pixels crisp (right
// for pixel-art bitmap fonts and tile sheets that the engine should never
// interpolate). The D3D11 backend keeps both samplers ready and selects on
// BindTexture; the Vulkan backend currently honors the field via its own
// sampler set (or falls back to Linear if not implemented).
enum class TextureFilter { Linear, Point };

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
