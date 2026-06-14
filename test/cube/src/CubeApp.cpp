// Smoke test for the Phase 5 PBR pipeline. A 2x5 grid of spheres sits
// above a ground plane: the top row is dielectric (metallic = 0) and
// the bottom row is metallic (metallic = 1); roughness goes 0 -> 1
// left to right. The directional light + hemi ambient + shadow + fog
// from Phase 4 all carry over, so a single render frame shows the full
// stack of features.

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
// CCW-from-above winding so the back-face cull doesn't drop the top.
const uint16_t kPlaneIndices[6] = {0, 2, 1, 0, 3, 2};

constexpr int   kSphereCols = 5;        // roughness 0 .. 1 across
constexpr float kSphereSpacing = 1.3f;
constexpr float kRowHeight     = 0.0f;  // both rows on the same plane
} // namespace

class CubeApp : public Application {
public:
    CubeApp() : Application(MakeConfig()) {}

    void OnStart() override {
        m_renderer.Init(GetDevice());
        m_post.Init(GetDevice(), kWindowW, kWindowH);

        // 16x16 white texture so the PBR material's albedoFactor /
        // metallic-roughness scalars drive the look entirely. Real
        // textured spheres land in Phase 5.5 once the loader knows
        // about MR maps.
        m_white = MakeSolid(0xFF, 0xFF, 0xFF);
        // Dark earth checker for the ground, same as Phase 4.
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

        m_camera.SetViewport(float(kWindowW), float(kWindowH));
        m_camera.SetPosition({0.0f, 1.8f, 5.0f});
        m_camera.LookAt({0.0f, 0.4f, 0.0f});
        m_camera.SetFovY(1.0472f);          // 60 deg
        m_camera.SetNearFar(0.1f, 100.0f);

        NARAGAME_INFO("Cube: 2x5 PBR sphere grid (dielectric / metal x roughness)");
    }

    void OnStop() override {
        m_post.Shutdown();
        m_renderer.Shutdown();
    }

    void OnUpdate(float dt) override {
        m_yaw += dt * 0.4f;
        if (m_yaw > 6.28318f) m_yaw -= 6.28318f;
    }

    void OnRender() override {
        // Bind the HDR scene RT + its matching depth so the 3D pass
        // writes into the post-process domain instead of the LDR
        // backbuffer.
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

        // Ground plane below the spheres.
        const Mat4 planeModel = mat4::Translation({0.0f, -0.6f, 0.0f});
        m_renderer.DrawMesh(m_planeVB.get(), m_planeIB.get(),
                            6, IndexFormat::Uint16,
                            planeModel, m_ground.get(), {});

        if (m_sphere.valid) {
            const float gridX0 = -(kSphereCols - 1) * 0.5f * kSphereSpacing;
            for (int col = 0; col < kSphereCols; ++col) {
                const float roughness = float(col) / float(kSphereCols - 1);
                const float x = gridX0 + col * kSphereSpacing;
                // Top row: dielectric (cream albedo, no metal). Bottom
                // row: copper-tinted metal so the F0 = albedo path is
                // visible.
                DrawSphere(x, 0.6f,  0.0f, 0.0f, roughness,
                           Color(0.90f, 0.88f, 0.82f, 1.0f));
                DrawSphere(x, 0.6f, -1.4f, 1.0f, roughness,
                           Color(0.95f, 0.65f, 0.45f, 1.0f));
            }
            // Spin a single sphere on the camera-facing edge so the
            // shadow lighting changes obviously over time.
            const float ax = 0.0f, az = 1.6f;
            const Mat4 spin = mat4::Translation({ax, 0.0f, az})
                            * mat4::RotationY(m_yaw);
            m_renderer.DrawMesh(m_sphere.vertexBuffer.get(),
                                m_sphere.indexBuffer.get(),
                                m_sphere.indexCount, m_sphere.indexFormat,
                                spin, m_white.get(),
                                Material{Color(0.20f, 0.60f, 0.85f, 1.0f),
                                         /*metallic=*/0.0f,
                                         /*roughness=*/0.35f});
        }
        m_renderer.End();

        // Bright pass + 2-pass Gaussian blur + ACES composite (with DoF)
        // into the backbuffer. Knobs are tuned so the metallic spheres'
        // highlights bloom subtly and the far edges of the plane fall
        // out of focus.
        PostProcessParams pp;
        pp.bloomThreshold = 0.9f;
        pp.bloomStrength  = 0.55f;
        pp.focusDistance  = 5.0f;
        pp.focusRange     = 8.0f;
        pp.exposure       = 1.1f;
        m_post.RunPost(pp);
    }

private:
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
    float                       m_yaw = 0.0f;
};

Application* NaraGame::CreateApplication(int /*argc*/, char** /*argv*/) {
    return new CubeApp();
}
