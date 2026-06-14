// Smoke test for the full Phase 1-7 stack: PBR (cube + sphere grid +
// rotating blue sphere), shadow / hemi / fog (the ground plane reads
// like Phase 4), post-process (HDR + bloom + DoF + ACES), and the new
// Phase 7 skinned mesh -- a 4-joint procedural rod that bends in a
// wave so the deformation is obvious in a still frame.

#include <NaraGame/assets/MeshLoader.h>
#include <NaraGame/core/Application.h>
#include <NaraGame/core/EntryPoint.h>
#include <NaraGame/core/Log.h>
#include <NaraGame/math/Math.h>
#include <NaraGame/renderer/Camera3D.h>
#include <NaraGame/renderer/PostProcess.h>
#include <NaraGame/renderer/Renderer3D.h>
#include <NaraGame/renderer/rhi/RHIDevice.h>
#include <NaraGame/renderer/rhi/RHIResources.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#ifndef NARAGAME_ASSETS_DIR
#define NARAGAME_ASSETS_DIR "assets"
#endif

using namespace NaraGame;

namespace {
constexpr uint32_t kWindowW = 1280;
constexpr uint32_t kWindowH = 720;

constexpr float kPlaneSize = 12.0f;
const Vertex3D kPlaneVerts[4] = {
    {{-kPlaneSize, 0.0f, -kPlaneSize}, {0, 1, 0}, {0.0f, 0.0f}},
    {{ kPlaneSize, 0.0f, -kPlaneSize}, {0, 1, 0}, {1.0f, 0.0f}},
    {{ kPlaneSize, 0.0f,  kPlaneSize}, {0, 1, 0}, {1.0f, 1.0f}},
    {{-kPlaneSize, 0.0f,  kPlaneSize}, {0, 1, 0}, {0.0f, 1.0f}},
};
const uint16_t kPlaneIndices[6] = {0, 2, 1, 0, 3, 2};

constexpr int   kSphereCols = 5;
constexpr float kSphereSpacing = 1.3f;

// Skinned rod geometry. A vertical 4-segment cylinder, each ring of
// 12 verts bound to one joint, with a half-influence ring between
// adjacent joints so the deformation reads as a smooth bend rather
// than a kink. 4 joints, 5 rings, 12 verts per ring = 60 verts.
constexpr int   kRodSides      = 12;
constexpr int   kRodJoints     = 4;
constexpr int   kRodRings      = kRodJoints + 1;
constexpr float kRodRadius     = 0.18f;
constexpr float kRodSegmentLen = 0.40f;
} // namespace

class CubeApp : public Application {
public:
    CubeApp() : Application(MakeConfig()) {}

    void OnStart() override {
        m_renderer.Init(GetDevice());
        m_post.Init(GetDevice(), kWindowW, kWindowH);

        m_white  = MakeSolid(0xFF, 0xFF, 0xFF);
        m_ground = MakeChecker(0x6A, 0x55, 0x3A, 0x3A, 0x2C, 0x1E);

        BufferDesc vb;
        vb.type        = BufferType::Vertex;
        vb.usage       = BufferUsage::Static;
        vb.size        = sizeof(kPlaneVerts);
        vb.initialData = kPlaneVerts;
        m_planeVB = GetDevice().CreateBuffer(vb);
        BufferDesc ib;
        ib.type        = BufferType::Index;
        ib.usage       = BufferUsage::Static;
        ib.size        = sizeof(kPlaneIndices);
        ib.initialData = kPlaneIndices;
        m_planeIB = GetDevice().CreateBuffer(ib);

        const std::string assets = NARAGAME_ASSETS_DIR;
        m_sphere = LoadObj(GetDevice(), assets + "/sphere.obj");

        BuildRod();

        m_camera.SetViewport(float(kWindowW), float(kWindowH));
        m_camera.SetPosition({0.0f, 1.8f, 5.0f});
        m_camera.LookAt({0.0f, 0.4f, 0.0f});
        m_camera.SetFovY(1.0472f);
        m_camera.SetNearFar(0.1f, 100.0f);

        NARAGAME_INFO("Cube: PBR grid + skinned rod (4 joints, wave anim)");
    }

    void OnStop() override {
        m_post.Shutdown();
        m_renderer.Shutdown();
    }

    void OnUpdate(float dt) override {
        m_yaw  += dt * 0.4f;
        if (m_yaw > 6.28318f) m_yaw -= 6.28318f;
        m_time += dt;
    }

    void OnRender() override {
        m_post.BindSceneTarget();
        GetDevice().Clear(Color(0.55f, 0.58f, 0.65f, 1.0f));
        GetDevice().ClearDepth(1.0f);

        DirectionalLight light;
        light.direction     = {-0.4f, -1.0f, -0.3f};
        light.skyAmbient    = {0.40f, 0.50f, 0.60f, 1.0f};
        light.groundAmbient = {0.16f, 0.13f, 0.10f, 1.0f};
        light.shadowCenter  = {0.0f, 0.5f, 0.0f};
        light.shadowRadius  = 6.0f;

        FogParams fog;
        fog.color         = {0.55f, 0.58f, 0.65f, 1.0f};
        fog.startDistance = 6.0f;
        fog.endDistance   = 18.0f;

        m_renderer.Begin(m_camera.View(), m_camera.Projection(),
                         light, fog,
                         m_post.SceneColor(), m_post.SceneDepth());

        // Ground plane.
        const Mat4 planeModel = mat4::Translation({0.0f, -0.6f, 0.0f});
        m_renderer.DrawMesh(m_planeVB.get(), m_planeIB.get(),
                            6, IndexFormat::Uint16,
                            planeModel, m_ground.get(), {});

        // 2x5 PBR sphere grid kept from Phase 5.
        if (m_sphere.valid) {
            const float gridX0 = -(kSphereCols - 1) * 0.5f * kSphereSpacing;
            for (int col = 0; col < kSphereCols; ++col) {
                const float roughness = float(col) / float(kSphereCols - 1);
                const float x = gridX0 + col * kSphereSpacing;
                DrawSphere(x, 0.6f,  0.0f, 0.0f, roughness,
                           Color(0.90f, 0.88f, 0.82f, 1.0f));
                DrawSphere(x, 0.6f, -1.4f, 1.0f, roughness,
                           Color(0.95f, 0.65f, 0.45f, 1.0f));
            }
        }

        // Animate the rod's 4 joints and submit through the skinned
        // path. Each joint rotates around X by an offset wave so the
        // rod traces an S-curve over time.
        std::array<Mat4, kRodJoints> jointWorld;
        std::array<Mat4, kRodJoints> skinMats;
        for (int j = 0; j < kRodJoints; ++j) {
            const float angle = 0.55f * std::sin(m_time * 2.4f + j * 1.1f);
            const Mat4 localRot = mat4::RotationZ(angle);
            const Mat4 localTrans = (j == 0)
                ? Mat4::Identity()
                : mat4::Translation({0.0f, kRodSegmentLen, 0.0f});
            const Mat4 local = localTrans * localRot;
            jointWorld[j] = (j == 0) ? local : (jointWorld[j - 1] * local);
            skinMats[j]   = jointWorld[j] * m_rodInverseBind[j];
        }
        const Mat4 rodModel = mat4::Translation({0.0f, -0.6f, 1.6f});
        m_renderer.DrawSkinnedMesh(m_rodVB.get(), m_rodIB.get(),
                                   m_rodIndexCount, IndexFormat::Uint16,
                                   rodModel,
                                   skinMats.data(), kRodJoints,
                                   m_white.get(),
                                   Material{Color(0.85f, 0.30f, 0.35f, 1.0f),
                                            /*metallic=*/0.0f,
                                            /*roughness=*/0.35f});

        m_renderer.End();

        PostProcessParams pp;
        pp.bloomThreshold = 0.9f;
        pp.bloomStrength  = 0.55f;
        pp.focusDistance  = 5.0f;
        pp.focusRange     = 8.0f;
        pp.exposure       = 1.1f;
        m_post.RunPost(pp);
    }

private:
    void BuildRod() {
        // 5 rings of 12 verts. Ring r sits at world-Y = r * segment len.
        // Joint binding: ring 0 -> joint 0, ring 4 -> joint 3, the
        // intermediate rings blend two adjacent joints 50/50.
        std::vector<Vertex3DSkin> verts;
        verts.reserve(kRodRings * kRodSides);
        for (int r = 0; r < kRodRings; ++r) {
            const float y = float(r) * kRodSegmentLen;
            for (int s = 0; s < kRodSides; ++s) {
                const float a = float(s) / float(kRodSides) * 6.28318f;
                Vertex3DSkin v{};
                v.position = {kRodRadius * std::cos(a), y,
                              kRodRadius * std::sin(a)};
                v.normal   = {std::cos(a), 0.0f, std::sin(a)};
                v.uv       = {float(s) / float(kRodSides),
                              float(r) / float(kRodRings - 1)};
                // Joint binding. Ring r maps to two joints: floor(r * (J-1)/R)
                // and the next one; weight by fractional position. For
                // ring r = 0 → (0, 0), r = 4 → (3, 3), middle rings split.
                const float jf = float(r) * float(kRodJoints - 1)
                               / float(kRodRings - 1);
                const int   j0 = int(std::floor(jf));
                const int   j1 = std::min(j0 + 1, kRodJoints - 1);
                const float w1 = jf - float(j0);
                const float w0 = 1.0f - w1;
                v.jointIndices[0] = float(j0);
                v.jointIndices[1] = float(j1);
                v.jointIndices[2] = 0.0f;
                v.jointIndices[3] = 0.0f;
                v.jointWeights[0] = w0;
                v.jointWeights[1] = w1;
                v.jointWeights[2] = 0.0f;
                v.jointWeights[3] = 0.0f;
                verts.push_back(v);
            }
        }
        // Triangulate the side faces between adjacent rings.
        std::vector<uint16_t> indices;
        indices.reserve((kRodRings - 1) * kRodSides * 6);
        for (int r = 0; r < kRodRings - 1; ++r) {
            for (int s = 0; s < kRodSides; ++s) {
                const int sn  = (s + 1) % kRodSides;
                const uint16_t a = uint16_t(r       * kRodSides + s);
                const uint16_t b = uint16_t(r       * kRodSides + sn);
                const uint16_t c = uint16_t((r + 1) * kRodSides + sn);
                const uint16_t d = uint16_t((r + 1) * kRodSides + s);
                // CCW from outside the cylinder so back-face cull keeps
                // the inside hidden. With +Y up and the azimuth angle
                // sweeping CCW around +Y (the convention BuildRod uses),
                // a -> b -> c is the right outward-facing winding.
                indices.push_back(a);
                indices.push_back(b);
                indices.push_back(c);
                indices.push_back(a);
                indices.push_back(c);
                indices.push_back(d);
            }
        }

        BufferDesc vbd;
        vbd.type        = BufferType::Vertex;
        vbd.usage       = BufferUsage::Static;
        vbd.size        = verts.size() * sizeof(Vertex3DSkin);
        vbd.initialData = verts.data();
        m_rodVB = GetDevice().CreateBuffer(vbd);

        BufferDesc ibd;
        ibd.type        = BufferType::Index;
        ibd.usage       = BufferUsage::Static;
        ibd.size        = indices.size() * sizeof(uint16_t);
        ibd.initialData = indices.data();
        m_rodIB = GetDevice().CreateBuffer(ibd);
        m_rodIndexCount = uint32_t(indices.size());

        // Bind pose: joints stacked along +Y. Inverse bind for joint j
        // is a translation by -j * segmentLen along +Y so a vertex at
        // bind position (0, r*L, 0) maps to (0, 0, 0) before the joint's
        // local transform applies.
        for (int j = 0; j < kRodJoints; ++j) {
            const Mat4 bind = mat4::Translation({0.0f, float(j) * kRodSegmentLen, 0.0f});
            // Inverse of a pure translation: negate the translation.
            m_rodInverseBind[j] = mat4::Translation({0.0f, -float(j) * kRodSegmentLen, 0.0f});
            (void)bind;
        }
    }

    void DrawSphere(float x, float y, float z, float metallic, float roughness,
                    const Color& albedo) {
        const Mat4 model = mat4::Translation({x, y, z})
                         * mat4::Scale({0.45f, 0.45f, 0.45f});
        m_renderer.DrawMesh(m_sphere.vertexBuffer.get(),
                            m_sphere.indexBuffer.get(),
                            m_sphere.indexCount, m_sphere.indexFormat,
                            model, m_white.get(),
                            Material{albedo, metallic, roughness});
    }

    static AppConfig MakeConfig() {
        AppConfig c;
        c.title  = "NaraGame - Cube";
        c.width  = kWindowW;
        c.height = kWindowH;
        return c;
    }

    std::unique_ptr<RHITexture> MakeSolid(uint8_t r, uint8_t g, uint8_t b) {
        std::array<uint8_t, 16 * 16 * 4> pixels{};
        for (size_t i = 0; i < pixels.size(); i += 4) {
            pixels[i + 0] = r;
            pixels[i + 1] = g;
            pixels[i + 2] = b;
            pixels[i + 3] = 0xFF;
        }
        TextureDesc td;
        td.width  = 16;
        td.height = 16;
        td.format = TextureFormat::RGBA8;
        td.filter = TextureFilter::Linear;
        td.pixels = pixels.data();
        return GetDevice().CreateTexture(td);
    }
    std::unique_ptr<RHITexture> MakeChecker(uint8_t r0, uint8_t g0, uint8_t b0,
                                            uint8_t r1, uint8_t g1, uint8_t b1) {
        std::array<uint8_t, 16 * 16 * 4> pixels{};
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                const bool light = ((x ^ y) & 1) == 0;
                const size_t i = (y * 16 + x) * 4;
                pixels[i + 0] = light ? r0 : r1;
                pixels[i + 1] = light ? g0 : g1;
                pixels[i + 2] = light ? b0 : b1;
                pixels[i + 3] = 0xFF;
            }
        }
        TextureDesc td;
        td.width  = 16;
        td.height = 16;
        td.format = TextureFormat::RGBA8;
        td.filter = TextureFilter::Point;
        td.pixels = pixels.data();
        return GetDevice().CreateTexture(td);
    }

    Renderer3D                  m_renderer;
    PostProcess                 m_post;
    Camera3D                    m_camera;
    std::unique_ptr<RHITexture> m_white;
    std::unique_ptr<RHITexture> m_ground;
    std::unique_ptr<RHIBuffer>  m_planeVB;
    std::unique_ptr<RHIBuffer>  m_planeIB;
    Mesh                        m_sphere;
    // Rod geometry + inverse bind matrices.
    std::unique_ptr<RHIBuffer>  m_rodVB;
    std::unique_ptr<RHIBuffer>  m_rodIB;
    uint32_t                    m_rodIndexCount = 0;
    std::array<Mat4, kRodJoints> m_rodInverseBind{};
    float                       m_yaw  = 0.0f;
    float                       m_time = 0.0f;
};

Application* NaraGame::CreateApplication(int /*argc*/, char** /*argv*/) {
    return new CubeApp();
}
