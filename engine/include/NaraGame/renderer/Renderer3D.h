#pragma once

#include "NaraGame/math/Mat4.h"
#include "NaraGame/math/Vec2.h"
#include "NaraGame/math/Vec3.h"
#include "NaraGame/renderer/rhi/RHITypes.h"

#include <memory>

namespace NaraGame {

class RHIBuffer;
class RHIDevice;
class RHIPipeline;
class RHIShader;
class RHITexture;

// One Renderer3D vertex. Position + normal in world units, UV in [0, 1].
// Stride 32; the shader's input layout expects this exact ordering.
struct Vertex3D {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
};

// Immediate-mode mesh renderer for the 3D pipeline. Owns one PSO -- a
// textured directional-light shader -- and per-frame + per-draw constant
// buffers. Begin() locks in the camera + light for the current pass;
// DrawMesh() submits one mesh with its model transform and texture.
//
// Renderer3D is a sibling to Renderer2D: both stand on the same RHI and
// can be used in the same frame (e.g. 2D HUD on top of a 3D scene).
class Renderer3D {
public:
    Renderer3D() = default;
    ~Renderer3D();

    Renderer3D(const Renderer3D&) = delete;
    Renderer3D& operator=(const Renderer3D&) = delete;

    void Init(RHIDevice& device);
    void Shutdown();

    // Lock in the view-projection matrix and a single directional light
    // (world space, points FROM light source TOWARD the lit surface --
    // i.e. roughly downward for a sun). Ambient is the colour added to
    // every fragment so shadowed sides don't fall pure black.
    void Begin(const Mat4& view, const Mat4& projection,
               const Vec3& lightDir = {-0.4f, -1.0f, -0.3f},
               const Color& ambient = {0.18f, 0.18f, 0.22f, 1.0f});
    void End();

    // Draw one mesh. `vb` / `ib` are buffers created via CreateBuffer
    // with the right BufferType; the mesh data must use the Vertex3D
    // layout above. `model` is the world-from-local transform; `texture`
    // is sampled as albedo, `tint` multiplies it.
    void DrawMesh(RHIBuffer* vb, RHIBuffer* ib, uint32_t indexCount,
                  IndexFormat indexFormat,
                  const Mat4& model, RHITexture* texture,
                  const Color& tint = Color::White());

private:
    RHIDevice* m_device = nullptr;

    std::unique_ptr<RHIShader>   m_shader;
    std::unique_ptr<RHIPipeline> m_pipeline;
    // Two CBs: per-frame (camera + light) updated in Begin, per-draw
    // (model + tint) updated per DrawMesh. Separating them keeps the
    // per-frame upload to one map per pass instead of per draw.
    std::unique_ptr<RHIBuffer>   m_frameUB;
    std::unique_ptr<RHIBuffer>   m_drawUB;
    // 1x1 white texture, bound when DrawMesh is called with texture =
    // nullptr so the shader always samples something valid.
    std::unique_ptr<RHITexture>  m_whiteTex;
    bool m_inPass = false;
};

} // namespace NaraGame
