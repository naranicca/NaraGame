#pragma once

#include "NaraGame/math/Math.h"
#include "NaraGame/renderer/TextureRegion.h"
#include "NaraGame/renderer/rhi/RHITypes.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace NaraGame {

class RHIDevice;
class RHIBuffer;
class RHIShader;
class RHIPipeline;
class RHITexture;
class BitmapFont;
class Font;

// Batched 2D sprite renderer built on the RHI. Accumulates quads into a single
// dynamic vertex buffer and flushes them in as few draw calls as possible
// (one per texture switch). Coordinates follow the camera you pass to Begin();
// ScreenCamera() sets up a top-left origin with +Y pointing down.
class Renderer2D {
public:
    Renderer2D() = default;
    ~Renderer2D();

    Renderer2D(const Renderer2D&) = delete;
    Renderer2D& operator=(const Renderer2D&) = delete;

    void Init(RHIDevice& device);
    void Shutdown();

    void Begin(const Mat4& viewProjection);
    void End();

    // Solid-color quad: top-left corner `position`, extent `size`.
    void DrawQuad(const Vec2& position, const Vec2& size, const Color& color);

    // Solid-color quad rotated around its CENTER. `angleRad` is screen-space
    // radians: positive rotates the +x side toward +y (down on screen);
    // negative tilts the +x side toward -y (up on screen). Used by Flappy
    // for the bird's pitch — same draw path as the white-texture quad above.
    void DrawQuad(const Vec2& center, const Vec2& size, float angleRad, const Color& color);

    // Textured quad rotated around its CENTER. Same rotation convention as
    // the colored overload; sprite UVs come from the region.
    void DrawQuad(const Vec2& center, const Vec2& size, float angleRad,
                  const TextureRegion& region, const Color& tint = Color::White());

    // Textured quad with optional tint and UV sub-rect (u0, v0, u1, v1).
    void DrawQuad(const Vec2& position, const Vec2& size, RHITexture* texture,
                  const Color& tint = Color::White(), const Vec4& uvRect = {0.0f, 0.0f, 1.0f, 1.0f});

    // Draw a sprite-sheet / atlas region at an explicit size. `flipX` mirrors
    // horizontally (e.g. for left/right facing).
    void DrawQuad(const Vec2& position, const Vec2& size, const TextureRegion& region,
                  const Color& tint = Color::White(), bool flipX = false);

    // Draw a region at its native pixel size (optionally scaled).
    void DrawSprite(const Vec2& position, const TextureRegion& region,
                    const Color& tint = Color::White(), float scale = 1.0f, bool flipX = false);

    // Draw a line of text with a bitmap font. `position` is the top-left of the
    // first glyph; '\n' starts a new line. Glyphs share the font texture, so a
    // whole string is one batched draw.
    void DrawText(const BitmapFont& font, const std::string& text, const Vec2& position,
                  const Color& color = Color::White(), float scale = 1.0f);

    // Proportional (variable-width) text using a BMFont-loaded Font.
    void DrawText(const Font& font, const std::string& text, const Vec2& position,
                  const Color& color = Color::White(), float scale = 1.0f);

    RHITexture* WhiteTexture() const { return m_whiteTexture.get(); }

    // Orthographic camera with (0,0) at the top-left and +Y downward.
    static Mat4 ScreenCamera(float width, float height);

private:
    struct Vertex {
        Vec3 position;
        Vec4 color;
        Vec2 uv;
    };

    void StartBatch();
    void Flush();

    RHIDevice* m_device = nullptr;
    std::unique_ptr<RHIShader> m_shader;
    std::unique_ptr<RHIPipeline> m_pipeline;
    std::unique_ptr<RHIBuffer> m_vertexBuffer;
    std::unique_ptr<RHIBuffer> m_indexBuffer;
    std::unique_ptr<RHIBuffer> m_cameraBuffer;
    std::unique_ptr<RHITexture> m_whiteTexture;

    std::vector<Vertex> m_vertices;
    uint32_t m_quadCount = 0;
    RHITexture* m_currentTexture = nullptr;
    bool m_inScene = false;

    static constexpr uint32_t kMaxQuads = 20000;
    static constexpr uint32_t kMaxVertices = kMaxQuads * 4;
    static constexpr uint32_t kMaxIndices = kMaxQuads * 6;
};

} // namespace NaraGame
