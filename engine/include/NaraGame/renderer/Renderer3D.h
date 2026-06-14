#pragma once

#include "NaraGame/math/Mat4.h"
#include "NaraGame/math/Vec2.h"
#include "NaraGame/math/Vec3.h"
#include "NaraGame/renderer/rhi/RHITypes.h"

#include <memory>
#include <vector>

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

// Skinned vertex format: Vertex3D layout plus four joint indices and
// four blend weights per vertex. Indices ride as floats (cast to int
// in the shader) so we can reuse VertexFormat::Float4 without adding
// a UByte4 path to the RHI. Weights should sum to 1 across the four
// influences; unused slots get index 0 / weight 0.
struct Vertex3DSkin {
    Vec3  position;
    Vec3  normal;
    Vec2  uv;
    float jointIndices[4];
    float jointWeights[4];
};

// Maximum joints supported by the skinning cbuffer. Bumping this means
// editing kMaxJoints in Renderer3D.cpp's HLSL too.
constexpr int kMaxJoints = 64;

// Hemi-ambient directional light. `direction` points FROM the source
// TOWARD the lit surface (so {0, -1, 0} is a sun straight overhead).
// skyAmbient / groundAmbient give the two halves of the hemisphere
// ambient lookup: surfaces facing up bias toward skyAmbient, surfaces
// facing down toward groundAmbient. shadowCenter / shadowRadius pin the
// orthographic light camera used to render the shadow map -- the
// caller is expected to keep them roughly centered on the visible
// gameplay area for the shadow resolution to stay useful.
struct DirectionalLight {
    Vec3  direction      = {-0.4f, -1.0f, -0.3f};
    Color skyAmbient     = {0.45f, 0.50f, 0.60f, 1.0f};
    Color groundAmbient  = {0.15f, 0.12f, 0.10f, 1.0f};
    Vec3  shadowCenter   = {0.0f,  0.0f,  0.0f};
    float shadowRadius   = 10.0f;
};

// Linear depth fog. Fragments closer than startDistance are unaffected;
// fragments farther than endDistance are fully replaced by `color`.
// In between they lerp linearly. Set endDistance < startDistance to
// disable (the saturate in the shader collapses to 0 either side).
struct FogParams {
    Color color         = {0.55f, 0.58f, 0.65f, 1.0f};
    float startDistance = 1000.0f;   // off by default
    float endDistance   = 2000.0f;
};

// PBR material parameters for one mesh. `albedoFactor` tints whatever
// the bound albedo texture samples to (white = pass-through). `metallic`
// is 0 for plain dielectrics (paint, wood, plastic) and 1 for raw
// metals (steel, gold, copper); intermediate values give the dirty-
// metal look but aren't physically meaningful. `roughness` runs from 0
// (mirror-smooth) to 1 (matte) and drives the GGX microfacet width.
// The MVP material has no texture slots yet -- the shader takes its
// metallic / roughness from these scalars; an albedoMap can ride along
// in the existing DrawMesh `texture` parameter.
struct Material {
    Color albedoFactor = Color::White();
    float metallic     = 0.0f;
    float roughness    = 0.5f;
};

// Immediate-mode mesh renderer for the 3D pipeline. Owns one main PSO
// (textured + hemi-ambient + 1 directional light + 1 shadow-map + linear
// fog) plus one depth-only shadow PSO and a 2048x2048 shadow render
// target. Begin() locks in the camera + light + fog for the current
// frame; DrawMesh() queues a mesh that will both cast and receive a
// shadow; End() flushes the queue as a shadow pass + a main pass.
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

    // Lock in the camera, light + ambient hemisphere, and fog for the
    // current frame. The queue starts empty; subsequent DrawMesh calls
    // populate it. End() does the actual GPU work.
    //
    // sceneColor / sceneDepth optionally route the main-pass output to
    // an offscreen render target (e.g. a PostProcess HDR RT + its
    // matching depth). Both null = render straight to the backbuffer
    // (the simple path the cube test app used before Phase 6). If
    // either is null the corresponding default (backbuffer colour or
    // backbuffer depth) is used.
    void Begin(const Mat4& view, const Mat4& projection,
               const DirectionalLight& light = {},
               const FogParams& fog = {},
               RHITexture* sceneColor = nullptr,
               RHITexture* sceneDepth = nullptr);

    // Queue one mesh for both the shadow pass and the main pass. `vb` /
    // `ib` are buffers created via CreateBuffer with the right
    // BufferType; the mesh data must use the Vertex3D layout above.
    // `model` is the world-from-local transform; `texture` is sampled
    // as albedo, `material` drives the BRDF (metallic / roughness /
    // tint). `texture` may be null -- a 2x2 white texture is used in
    // its place so the shader always samples valid data.
    void DrawMesh(RHIBuffer* vb, RHIBuffer* ib, uint32_t indexCount,
                  IndexFormat indexFormat,
                  const Mat4& model, RHITexture* texture,
                  const Material& material = {});

    // Same as DrawMesh but the mesh data uses Vertex3DSkin (per-vertex
    // joint indices + weights) and the caller hands in an array of
    // skin matrices = jointWorld * inverseBindMatrix already composed.
    // `jointCount` <= kMaxJoints; extra indices in the cbuffer stay at
    // identity so a smaller skeleton still works. Both the shadow pass
    // and the main pass replay the skinning, so animated characters
    // cast and receive shadows the same way static meshes do.
    void DrawSkinnedMesh(RHIBuffer* vb, RHIBuffer* ib, uint32_t indexCount,
                         IndexFormat indexFormat,
                         const Mat4& model,
                         const Mat4* jointMatrices, int jointCount,
                         RHITexture* texture,
                         const Material& material = {});

    // Run the shadow pass + the main pass. The main pass binds the
    // backbuffer's RTV + depth view, so the caller is expected to have
    // already called Clear() / ClearDepth() on the device for the
    // current frame.
    void End();

private:
    void RunShadowPass(const Mat4& lightViewProj);
    void RunMainPass();

    RHIDevice* m_device = nullptr;

    std::unique_ptr<RHIShader>   m_shaderMain;
    std::unique_ptr<RHIShader>   m_shaderShadow;
    std::unique_ptr<RHIShader>   m_shaderMainSkin;
    std::unique_ptr<RHIShader>   m_shaderShadowSkin;
    std::unique_ptr<RHIPipeline> m_pipelineMain;
    std::unique_ptr<RHIPipeline> m_pipelineShadow;
    std::unique_ptr<RHIPipeline> m_pipelineMainSkin;
    std::unique_ptr<RHIPipeline> m_pipelineShadowSkin;

    // Per-frame uniforms (camera + light + fog) updated in Begin; per-
    // draw uniforms (model + tint) updated inside the shadow / main pass
    // for each queued mesh.
    std::unique_ptr<RHIBuffer>   m_frameUB;
    std::unique_ptr<RHIBuffer>   m_drawUB;
    // SkinCB carries up to kMaxJoints float4x4 matrices in cbuffer b2.
    // Bound only during skinned draws; the static path leaves it alone.
    std::unique_ptr<RHIBuffer>   m_skinUB;
    // 2x2 white albedo used when DrawMesh receives a null texture.
    std::unique_ptr<RHITexture>  m_whiteTex;
    // Depth-format render target written by the shadow pass and sampled
    // by the main pass as a depth lookup. 2048x2048 is enough for a
    // single ortho fit covering the cube test scene; CSM is a later
    // upgrade.
    std::unique_ptr<RHITexture>  m_shadowMap;

    // Mesh queue collected between Begin and End. Replayed twice (once
    // for the shadow pass, once for the main pass) so the caller only
    // submits each mesh once.
    struct QueuedMesh {
        RHIBuffer*  vb;
        RHIBuffer*  ib;
        uint32_t    indexCount;
        IndexFormat indexFormat;
        Mat4        model;
        RHITexture* texture;
        Material    material;
    };
    std::vector<QueuedMesh> m_queue;

    // Separate queue for skinned meshes so we can swap the PSO (and
    // upload SkinCB) only on the boundary between static and skinned
    // draws. jointStart / jointCount index into m_skinPool below so
    // each queued mesh keeps its own joint matrices around for the
    // shadow + main replay.
    struct QueuedSkinned {
        RHIBuffer*  vb;
        RHIBuffer*  ib;
        uint32_t    indexCount;
        IndexFormat indexFormat;
        Mat4        model;
        RHITexture* texture;
        Material    material;
        int         jointStart;
        int         jointCount;
    };
    std::vector<QueuedSkinned> m_skinQueue;
    std::vector<Mat4>          m_skinPool;
    // Scene target captured in Begin so the main pass can route to it
    // after the shadow pass swaps the RT to the shadow map. Both null =
    // backbuffer + backbuffer depth.
    RHITexture* m_sceneColor = nullptr;
    RHITexture* m_sceneDepth = nullptr;
    bool m_inPass = false;
};

} // namespace NaraGame
