#pragma once

#include "NaraGame/math/Math.h"
#include "NaraGame/renderer/TextureRegion.h"
#include "NaraGame/renderer/rhi/RHITypes.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// windows.h maps DrawText to DrawTextA/W. A TU that includes
// <windows.h> (e.g. via EntryPoint.h) before this header would see
// both the declaration below and every call site macro-renamed to
// DrawTextW. Scrub the macro; call the ANSI/wide winapi functions
// explicitly if you ever need them in the same TU.
#ifdef DrawText
#  undef DrawText
#endif

namespace NaraGame {

class RHIDevice;
class RHIBuffer;
class RHIShader;
class RHIPipeline;
class RHITexture;
class BitmapFont;
class Font;
class MSDFFont;

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

    // Textured arbitrary quad. Corners are TL, TR, BR, BL in the same
    // coordinate space as Begin(). This is an affine per-triangle warp (not
    // perspective-correct interpolation), intended for pseudo-3D road strips,
    // billboards and other Mode-7-style effects.
    void DrawQuad(const Vec2& topLeft, const Vec2& topRight,
                  const Vec2& bottomRight, const Vec2& bottomLeft,
                  RHITexture* texture,
                  const Color& tint = Color::White(),
                  const Vec4& uvRect = {0.0f, 0.0f, 1.0f, 1.0f});

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

    // MSDF text -- stays sharp at any size because the fragment shader
    // reconstructs glyph edges from a distance field. `pixelSize` is the
    // em-box height in screen pixels (note: pixel size, not a multiplier --
    // MSDF has no native baked size to scale from). Triggers a pipeline
    // switch inside the batch; sprites drawn after revert to the sprite
    // pipeline at the next DrawQuad.
    void DrawText(const MSDFFont& font, const std::string& text, const Vec2& position,
                  const Color& color = Color::White(), float pixelSize = 32.0f);

    // Bind an offscreen render target (texture created with renderTarget=true).
    // Flushes the current batch, switches the device's colour attachment, and
    // swaps the camera UBO to a 1:1 ortho sized to the texture so DrawQuad
    // coordinates land where you'd expect inside the RT. Pass nullptr to
    // restore the backbuffer; the camera the most recent Begin() set up
    // becomes active again automatically.
    //
    // Typical pattern inside one Begin/End:
    //   r.Begin(ScreenCamera(screenW, screenH));
    //   r.DrawQuad(...);             // backbuffer
    //   r.SetRenderTarget(rt);
    //   r.Clear(skyBlue);
    //   r.DrawQuad(...);             // lands in rt
    //   r.SetRenderTarget(nullptr);
    //   r.DrawQuad(rt, ...);         // sample rt on the backbuffer
    //   r.End();
    void SetRenderTarget(RHITexture* target);

    // Clear the currently bound colour attachment. Convenience wrapper around
    // RHIDevice::Clear that also flushes the batch first so a Clear after
    // mid-frame draws doesn't surprise you by wiping them.
    void Clear(const Color& color);

    // Select the blend mode for subsequent sprite quads. Default Alpha is
    // src-over; Additive ignores the destination's RGB and just sums (good
    // for glows / sparks); Multiply darkens the destination by the source
    // (shadows, colour grades, fog overlays). Only the plain Sprite pipeline
    // exposes blend variants -- MSDF text and FogSprite stay on Alpha to
    // avoid pipeline-mode-x-blend-mode explosion. Changing modes flushes
    // the current batch.
    void SetBlendMode(BlendMode mode);

    // Switch to fog mode: subsequent sprite quads sample `fogTexture` (a low
    // resolution visibility map) per pixel and multiply it into the output,
    // giving pixel-smooth fog of war independent of sprite tile boundaries.
    // The fog texture's R channel is the visibility multiplier in [0, 1].
    //
    // `uRow` and `vRow` together form a 2x3 affine transform that maps each
    // vertex's (x, y) position to fog UV space:
    //   fogU = uRow.x*pos.x + uRow.y*pos.y + uRow.z
    //   fogV = vRow.x*pos.x + vRow.y*pos.y + vRow.z
    // The .w components are unused. The full 2x3 form (instead of just a
    // scale + offset) lets iso games invert their screen->world rotation in
    // the shader so the fog texture stays indexed by tile coordinates.
    //
    // Pass `fogTexture == nullptr` to leave fog mode and resume the regular
    // sprite path (e.g. after the world pass, before drawing the HUD).
    // Both transitions flush the current batch first.
    void SetFog(RHITexture* fogTexture, const Vec4& uRow, const Vec4& vRow);

    RHITexture* WhiteTexture() const { return m_whiteTexture.get(); }

    // Number of Flush() calls (= GPU draw calls) issued between the last
    // Begin and End. Resets on Begin so the count reflects the current
    // batch session. Used by debug overlays.
    uint32_t DrawCallCount() const { return m_drawCalls; }

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

    // Three pipelines share everything (vertex layout, camera UBO, sampler,
    // index buffer) except the fragment shader. The current batch tracks
    // which pipeline its quads belong to; changing modes flushes first so
    // sprite quads, MSDF glyphs, and fog-tinted quads never share a draw call.
    enum class PipelineMode { Sprite, MSDF, FogSprite };
    void EnsureMode(PipelineMode mode);
    // DrawQuad needs a sprite-family pipeline (Sprite or FogSprite). If we're
    // in FogSprite (because the caller did SetFog), keep it; otherwise fall
    // back to Sprite. Used in place of a bare EnsureMode(Sprite) so SetFog
    // isn't undone by the very next DrawQuad.
    void EnsureSpriteMode();

    RHIDevice* m_device = nullptr;
    std::unique_ptr<RHIShader>   m_shader;
    std::unique_ptr<RHIShader>   m_msdfShader;
    std::unique_ptr<RHIShader>   m_fogShader;
    std::unique_ptr<RHIPipeline> m_pipeline;            // Sprite, Alpha
    std::unique_ptr<RHIPipeline> m_pipelineAdditive;    // Sprite, Additive
    std::unique_ptr<RHIPipeline> m_pipelineMultiply;    // Sprite, Multiply
    std::unique_ptr<RHIPipeline> m_msdfPipeline;
    std::unique_ptr<RHIPipeline> m_fogPipeline;
    std::unique_ptr<RHIBuffer>   m_vertexBuffer;
    std::unique_ptr<RHIBuffer>   m_indexBuffer;
    std::unique_ptr<RHIBuffer>   m_cameraBuffer;
    std::unique_ptr<RHIBuffer>   m_fogXformBuffer;
    std::unique_ptr<RHITexture>  m_whiteTexture;

    // Fog state: pointer is not owned (caller manages lifetime). When null,
    // FogSprite mode falls back to Sprite via SetFog(nullptr).
    RHITexture* m_fogTexture = nullptr;

    std::vector<Vertex> m_vertices;
    uint32_t m_quadCount = 0;
    uint32_t m_drawCalls = 0;
    RHITexture* m_currentTexture = nullptr;
    PipelineMode m_currentMode  = PipelineMode::Sprite;
    BlendMode    m_currentBlend = BlendMode::Alpha;     // only meaningful in Sprite mode
    bool m_inScene = false;

    // Cached viewProj from the most recent Begin() so a SetRenderTarget swap
    // can put the camera back when it returns to the backbuffer. Without
    // this, callers would have to re-issue ScreenCamera() themselves after
    // every round trip.
    Mat4 m_savedViewProj{};

    static constexpr uint32_t kMaxQuads = 20000;
    static constexpr uint32_t kMaxVertices = kMaxQuads * 4;
    static constexpr uint32_t kMaxIndices = kMaxQuads * 6;
};

} // namespace NaraGame
