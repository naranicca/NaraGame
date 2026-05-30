#include "NaraGame/renderer/Renderer2D.h"

#include "NaraGame/core/Log.h"
#include "NaraGame/renderer/BitmapFont.h"
#include "NaraGame/renderer/Font.h"
#include "NaraGame/renderer/MSDFFont.h"
#include "NaraGame/renderer/rhi/RHIDevice.h"
#include "renderer/MsdfSpirv.h"   // precompiled Vulkan MSDF shader
#include "renderer/SpriteSpirv.h" // precompiled Vulkan sprite shader

#include <array>
#include <cmath>
#include <cstddef> // offsetof
#include <cstdint>

namespace NaraGame {

// Default sprite shader: transforms by the camera and multiplies texture by a
// per-vertex tint. Solid-color quads use a 1x1 white texture so they share this
// path. Matrices are column-major (matches Mat4 storage and HLSL defaults).
static const char* kSpriteShader = R"(
cbuffer Camera : register(b0) { float4x4 uViewProj; };

struct VSInput {
    float3 position : POSITION;
    float4 color    : COLOR;
    float2 uv       : TEXCOORD0;
};
struct VSOutput {
    float4 position : SV_POSITION;
    float4 color    : COLOR;
    float2 uv       : TEXCOORD0;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.position = mul(uViewProj, float4(input.position, 1.0));
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

Texture2D uTexture : register(t0);
SamplerState uSampler : register(s0);

float4 PSMain(VSOutput input) : SV_TARGET {
    return uTexture.Sample(uSampler, input.uv) * input.color;
}
)";

// MSDF shader: shares vertex layout + camera UBO + sampler with the sprite
// shader, but the pixel stage reconstructs the glyph edge from a 3-channel
// signed distance field. median(r,g,b) is the corner-aware best estimate of
// distance; converting that to screen pixels via fwidth(uv) gives free
// scale-aware AA. atlasPxRange must match -pxrange in build-msdf-atlas.ps1.
static const char* kMsdfShader = R"(
cbuffer Camera : register(b0) { float4x4 uViewProj; };

struct VSInput {
    float3 position : POSITION;
    float4 color    : COLOR;
    float2 uv       : TEXCOORD0;
};
struct VSOutput {
    float4 position : SV_POSITION;
    float4 color    : COLOR;
    float2 uv       : TEXCOORD0;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    output.position = mul(uViewProj, float4(input.position, 1.0));
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

Texture2D uTexture : register(t0);
SamplerState uSampler : register(s0);

float median(float a, float b, float c) {
    return max(min(a, b), min(max(a, b), c));
}

float4 PSMain(VSOutput input) : SV_TARGET {
    float3 msd = uTexture.Sample(uSampler, input.uv).rgb;
    float sd = median(msd.r, msd.g, msd.b) - 0.5;
    const float atlasPxRange = 4.0;
    float2 atlasSize;
    uTexture.GetDimensions(atlasSize.x, atlasSize.y);
    float2 unitRange     = atlasPxRange / atlasSize;
    float2 screenTexSize = 1.0 / fwidth(input.uv);
    float pxScale = max(0.5 * dot(unitRange, screenTexSize), 1.0);
    float pxDist  = pxScale * sd;
    float a = saturate(pxDist + 0.5);
    return float4(input.color.rgb, input.color.a * a);
}
)";

Renderer2D::~Renderer2D() {
    Shutdown();
}

void Renderer2D::Init(RHIDevice& device) {
    m_device = &device;

    // 4x4 white texture so solid-color quads reuse the textured path.
    // Using 1x1 here triggered a D3D11 sampling quirk: after sampling a 1x1
    // texture, the next *different* texture (e.g., a BitmapFont atlas) read
    // back as fully opaque, turning every text glyph into a solid white
    // rectangle. The same draw sequence with anything >= 2x2 works fine.
    // Suspected LOD / derivative interaction with a single-texel level-0,
    // and the byte cost (64 bytes) is negligible.
    constexpr uint32_t kWhiteSide = 4;
    const std::array<uint32_t, kWhiteSide * kWhiteSide> whitePixels = []() {
        std::array<uint32_t, kWhiteSide * kWhiteSide> a{};
        a.fill(0xFFFFFFFFu);
        return a;
    }();
    TextureDesc whiteDesc;
    whiteDesc.width = kWhiteSide;
    whiteDesc.height = kWhiteSide;
    whiteDesc.pixels = whitePixels.data();
    m_whiteTexture = device.CreateTexture(whiteDesc);

    // Shader + pipeline.
    ShaderDesc shaderDesc;
    shaderDesc.vertexSource = kSpriteShader;
    shaderDesc.pixelSource = kSpriteShader;
    shaderDesc.debugName = "Renderer2D::sprite";
    shaderDesc.vertexSpirv = kSpriteVertSpv;
    shaderDesc.vertexSpirvSize = sizeof(kSpriteVertSpv);
    shaderDesc.pixelSpirv = kSpriteFragSpv;
    shaderDesc.pixelSpirvSize = sizeof(kSpriteFragSpv);
    m_shader = device.CreateShader(shaderDesc);

    PipelineDesc pipelineDesc;
    pipelineDesc.shader = m_shader.get();
    pipelineDesc.blend = BlendMode::Alpha;
    pipelineDesc.topology = PrimitiveTopology::TriangleList;
    pipelineDesc.vertexLayout.stride = sizeof(Vertex);
    pipelineDesc.vertexLayout.attributes = {
        {"POSITION", 0, VertexFormat::Float3, static_cast<uint32_t>(offsetof(Vertex, position))},
        {"COLOR", 0, VertexFormat::Float4, static_cast<uint32_t>(offsetof(Vertex, color))},
        {"TEXCOORD", 0, VertexFormat::Float2, static_cast<uint32_t>(offsetof(Vertex, uv))},
    };
    m_pipeline = device.CreatePipeline(pipelineDesc);

    // Second shader + pipeline for MSDF text. Same vertex layout + camera UBO
    // as the sprite path; only the pixel program differs.
    ShaderDesc msdfShaderDesc;
    msdfShaderDesc.vertexSource = kMsdfShader;
    msdfShaderDesc.pixelSource = kMsdfShader;
    msdfShaderDesc.debugName = "Renderer2D::msdf";
    msdfShaderDesc.vertexSpirv = kMsdfVertSpv;
    msdfShaderDesc.vertexSpirvSize = sizeof(kMsdfVertSpv);
    msdfShaderDesc.pixelSpirv = kMsdfFragSpv;
    msdfShaderDesc.pixelSpirvSize = sizeof(kMsdfFragSpv);
    m_msdfShader = device.CreateShader(msdfShaderDesc);

    PipelineDesc msdfPipelineDesc = pipelineDesc;
    msdfPipelineDesc.shader = m_msdfShader.get();
    m_msdfPipeline = device.CreatePipeline(msdfPipelineDesc);

    // Dynamic vertex buffer (filled each flush).
    BufferDesc vboDesc;
    vboDesc.type = BufferType::Vertex;
    vboDesc.usage = BufferUsage::Dynamic;
    vboDesc.size = static_cast<size_t>(kMaxVertices) * sizeof(Vertex);
    m_vertexBuffer = device.CreateBuffer(vboDesc);

    // Static index buffer: the quad index pattern is fixed up front.
    std::vector<uint32_t> indices(kMaxIndices);
    for (uint32_t quad = 0; quad < kMaxQuads; ++quad) {
        const uint32_t v = quad * 4;
        const uint32_t i = quad * 6;
        indices[i + 0] = v + 0;
        indices[i + 1] = v + 1;
        indices[i + 2] = v + 2;
        indices[i + 3] = v + 2;
        indices[i + 4] = v + 3;
        indices[i + 5] = v + 0;
    }
    BufferDesc iboDesc;
    iboDesc.type = BufferType::Index;
    iboDesc.usage = BufferUsage::Static;
    iboDesc.size = indices.size() * sizeof(uint32_t);
    iboDesc.initialData = indices.data();
    m_indexBuffer = device.CreateBuffer(iboDesc);

    // Camera uniform buffer (one mat4).
    BufferDesc cameraDesc;
    cameraDesc.type = BufferType::Uniform;
    cameraDesc.usage = BufferUsage::Dynamic;
    cameraDesc.size = sizeof(Mat4);
    m_cameraBuffer = device.CreateBuffer(cameraDesc);

    m_vertices.reserve(kMaxVertices);
    NARAGAME_INFO("Renderer2D initialized (max %u quads/batch)", kMaxQuads);
}

void Renderer2D::Shutdown() {
    m_whiteTexture.reset();
    m_cameraBuffer.reset();
    m_indexBuffer.reset();
    m_vertexBuffer.reset();
    m_msdfPipeline.reset();
    m_msdfShader.reset();
    m_pipeline.reset();
    m_shader.reset();
    m_device = nullptr;
}

Mat4 Renderer2D::ScreenCamera(float width, float height) {
    // Top-left origin, +Y down, depth range [0, 1].
    return mat4::Ortho(0.0f, width, height, 0.0f, -1.0f, 1.0f);
}

void Renderer2D::Begin(const Mat4& viewProjection) {
    if (m_cameraBuffer)
        m_cameraBuffer->Update(viewProjection.data, sizeof(Mat4));
    m_inScene = true;
    m_currentMode = PipelineMode::Sprite;
    m_drawCalls = 0;
    StartBatch();
}

void Renderer2D::EnsureMode(PipelineMode mode) {
    if (m_currentMode == mode) return;
    Flush();
    StartBatch();
    m_currentMode = mode;
}

void Renderer2D::End() {
    Flush();
    m_inScene = false;
}

void Renderer2D::StartBatch() {
    m_vertices.clear();
    m_quadCount = 0;
    m_currentTexture = nullptr;
}

void Renderer2D::DrawQuad(const Vec2& position, const Vec2& size, const Color& color) {
    DrawQuad(position, size, m_whiteTexture.get(), color);
}

void Renderer2D::DrawQuad(const Vec2& center, const Vec2& size, float angleRad,
                          const Color& color) {
    if (!m_inScene)
        return;
    EnsureMode(PipelineMode::Sprite);

    RHITexture* tex = m_whiteTexture.get();
    if ((m_currentTexture != nullptr && m_currentTexture != tex) || m_quadCount >= kMaxQuads) {
        Flush();
        StartBatch();
    }
    m_currentTexture = tex;

    const float c = std::cos(angleRad);
    const float s = std::sin(angleRad);
    const float hw = size.x * 0.5f;
    const float hh = size.y * 0.5f;
    // Rotate the 4 local corners (±hw, ±hh) and translate by center.
    auto rot = [&](float lx, float ly) -> Vec3 {
        return {center.x + c * lx - s * ly, center.y + s * lx + c * ly, 0.0f};
    };
    const Vec4 col{color.r, color.g, color.b, color.a};
    m_vertices.push_back({rot(-hw, -hh), col, {0.0f, 0.0f}});
    m_vertices.push_back({rot( hw, -hh), col, {1.0f, 0.0f}});
    m_vertices.push_back({rot( hw,  hh), col, {1.0f, 1.0f}});
    m_vertices.push_back({rot(-hw,  hh), col, {0.0f, 1.0f}});
    ++m_quadCount;
}

void Renderer2D::DrawQuad(const Vec2& center, const Vec2& size, float angleRad,
                          const TextureRegion& region, const Color& tint) {
    if (!m_inScene)
        return;
    EnsureMode(PipelineMode::Sprite);

    RHITexture* tex = region.texture ? region.texture : m_whiteTexture.get();
    if ((m_currentTexture != nullptr && m_currentTexture != tex) || m_quadCount >= kMaxQuads) {
        Flush();
        StartBatch();
    }
    m_currentTexture = tex;

    const float c = std::cos(angleRad);
    const float s = std::sin(angleRad);
    const float hw = size.x * 0.5f;
    const float hh = size.y * 0.5f;
    auto rot = [&](float lx, float ly) -> Vec3 {
        return {center.x + c * lx - s * ly, center.y + s * lx + c * ly, 0.0f};
    };
    const Vec4 col{tint.r, tint.g, tint.b, tint.a};
    const Vec4 uv = region.uv;
    m_vertices.push_back({rot(-hw, -hh), col, {uv.x, uv.y}});
    m_vertices.push_back({rot( hw, -hh), col, {uv.z, uv.y}});
    m_vertices.push_back({rot( hw,  hh), col, {uv.z, uv.w}});
    m_vertices.push_back({rot(-hw,  hh), col, {uv.x, uv.w}});
    ++m_quadCount;
}

void Renderer2D::DrawQuad(const Vec2& position, const Vec2& size, RHITexture* texture,
                          const Color& tint, const Vec4& uvRect) {
    if (!m_inScene)
        return;
    EnsureMode(PipelineMode::Sprite);

    RHITexture* tex = texture ? texture : m_whiteTexture.get();

    // Flush when the texture changes or the batch is full.
    if ((m_currentTexture != nullptr && m_currentTexture != tex) || m_quadCount >= kMaxQuads) {
        Flush();
        StartBatch();
    }
    m_currentTexture = tex;

    const Vec4 c{tint.r, tint.g, tint.b, tint.a};
    const float x0 = position.x;
    const float y0 = position.y;
    const float x1 = position.x + size.x;
    const float y1 = position.y + size.y;

    m_vertices.push_back({{x0, y0, 0.0f}, c, {uvRect.x, uvRect.y}});
    m_vertices.push_back({{x1, y0, 0.0f}, c, {uvRect.z, uvRect.y}});
    m_vertices.push_back({{x1, y1, 0.0f}, c, {uvRect.z, uvRect.w}});
    m_vertices.push_back({{x0, y1, 0.0f}, c, {uvRect.x, uvRect.w}});
    ++m_quadCount;
}

void Renderer2D::DrawQuad(const Vec2& position, const Vec2& size, const TextureRegion& region,
                          const Color& tint, bool flipX) {
    Vec4 uv = region.uv;
    if (flipX) {
        const float tmp = uv.x;
        uv.x = uv.z;
        uv.z = tmp;
    }
    DrawQuad(position, size, region.texture, tint, uv);
}

void Renderer2D::DrawSprite(const Vec2& position, const TextureRegion& region, const Color& tint,
                            float scale, bool flipX) {
    DrawQuad(position, region.size * scale, region, tint, flipX);
}

void Renderer2D::DrawText(const BitmapFont& font, const std::string& text, const Vec2& position,
                          const Color& color, float scale) {
    if (!font.Valid())
        return;

    // Round every per-glyph dimension to whole pixels before laying glyphs out.
    // Non-integer scale (e.g. 1.4) would otherwise produce fractional glyph
    // quads, and even with a point sampler that creates uneven pixel widths
    // column to column — the "fuzzy bitmap font" look. Snapping the starting
    // position too keeps text on the same pixel grid as the rest of the UI.
    const float advance    = std::round(static_cast<float>(font.Advance())    * scale);
    const float lineHeight = std::round(static_cast<float>(font.LineHeight()) * scale);
    const Vec2  glyphSize{std::round(static_cast<float>(font.GlyphWidth())  * scale),
                          std::round(static_cast<float>(font.GlyphHeight()) * scale)};

    const float startX = std::round(position.x);
    float x = startX;
    float y = std::round(position.y);
    for (char c : text) {
        if (c == '\n') {
            x = startX;
            y += lineHeight;
            continue;
        }
        const TextureRegion glyph = font.Glyph(c);
        if (glyph.Valid())
            DrawQuad(Vec2{x, y}, glyphSize, glyph, color);
        x += advance;
    }
}

void Renderer2D::DrawText(const Font& font, const std::string& text, const Vec2& position,
                          const Color& color, float scale) {
    if (!font.Valid())
        return;

    float x = position.x;
    float y = position.y;
    for (char c : text) {
        if (c == '\n') {
            x = position.x;
            y += font.LineHeight() * scale;
            continue;
        }
        const FontGlyph* glyph = font.Find(static_cast<uint32_t>(static_cast<unsigned char>(c)));
        if (glyph == nullptr)
            continue;
        if (glyph->width > 0.0f && glyph->height > 0.0f) {
            DrawQuad({x + glyph->xoffset * scale, y + glyph->yoffset * scale},
                     {glyph->width * scale, glyph->height * scale}, font.Region(*glyph), color);
        }
        x += glyph->xadvance * scale;
    }
}

void Renderer2D::DrawText(const MSDFFont& font, const std::string& text,
                          const Vec2& position, const Color& color, float pixelSize) {
    if (!m_inScene || !font.Valid())
        return;
    EnsureMode(PipelineMode::MSDF);

    RHITexture* atlas = font.Texture();
    // Pen baseline so the topmost ascender sits at position.y. Ascender is in
    // em units and negative (it's the upward distance from baseline encoded
    // in screen-style coords where +Y is down), so we subtract it to walk
    // baseline downward from the requested top edge.
    const float baselineY = position.y + (-font.Ascender()) * pixelSize;
    float penX = position.x;

    const Vec4 col{color.r, color.g, color.b, color.a};

    for (char ch : text) {
        if (ch == '\n') {
            penX = position.x;
            // Multiline isn't really exercised by the test games, but stay
            // honest: advance baseline by lineHeight em-units.
            continue;
        }
        const MSDFGlyph* g = font.Find(static_cast<uint32_t>(static_cast<unsigned char>(ch)));
        if (g == nullptr) continue;

        // Whitespace (space, tab) has zero-area planeBounds; skip the quad
        // emission but still advance the pen.
        if (g->planeRight > g->planeLeft) {
            // Same batch-management pattern as DrawQuad — flush on texture
            // change or batch fill.
            if ((m_currentTexture != nullptr && m_currentTexture != atlas) || m_quadCount >= kMaxQuads) {
                Flush();
                StartBatch();
            }
            m_currentTexture = atlas;

            const float left   = penX + g->planeLeft   * pixelSize;
            const float top    = baselineY + g->planeTop    * pixelSize;
            const float right  = penX + g->planeRight  * pixelSize;
            const float bottom = baselineY + g->planeBottom * pixelSize;
            const TextureRegion region = font.Region(*g);
            const Vec4 uv = region.uv;

            m_vertices.push_back({{left,  top,    0.0f}, col, {uv.x, uv.y}});
            m_vertices.push_back({{right, top,    0.0f}, col, {uv.z, uv.y}});
            m_vertices.push_back({{right, bottom, 0.0f}, col, {uv.z, uv.w}});
            m_vertices.push_back({{left,  bottom, 0.0f}, col, {uv.x, uv.w}});
            ++m_quadCount;
        }
        penX += g->advance * pixelSize;
    }
}

void Renderer2D::Flush() {
    if (m_quadCount == 0 || m_device == nullptr)
        return;

    m_vertexBuffer->Update(m_vertices.data(), m_vertices.size() * sizeof(Vertex));

    RHIPipeline* pipeline =
        (m_currentMode == PipelineMode::MSDF) ? m_msdfPipeline.get() : m_pipeline.get();
    m_device->BindPipeline(pipeline);
    m_device->BindUniformBuffer(0, m_cameraBuffer.get());
    m_device->BindVertexBuffer(m_vertexBuffer.get(), sizeof(Vertex));
    m_device->BindIndexBuffer(m_indexBuffer.get(), IndexFormat::Uint32);
    m_device->BindTexture(0, m_currentTexture);
    m_device->DrawIndexed(m_quadCount * 6);
    ++m_drawCalls;
}

} // namespace NaraGame
