// Smoke test for Renderer3D + the .obj mesh loader + the Phase 4
// shadow / hemi-ambient / fog pipeline. A flat ground plane sits below
// a rotating cube and sphere; both meshes cast shadows onto the plane.
// Hemi ambient tints surfaces facing up vs down, and linear fog fades
// the plane's far edge into the background colour.
//
// There's no input or game logic; the camera is fixed. F2 would have
// been useful here but the test app doesn't pull in DebugDraw.

#include <NaraGame/assets/MeshLoader.h>
#include <NaraGame/core/Application.h>
#include <NaraGame/core/EntryPoint.h>
#include <NaraGame/core/Log.h>
#include <NaraGame/math/Math.h>
#include <NaraGame/renderer/Camera3D.h>
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

// 16x16 ground plane mesh. Big enough that the shadows under the
// rotating shapes always land on it, small enough that its far edges
// don't fall completely into fog. Four corners share a single up-
// facing normal; UVs tile the checker ~one cell per world unit so the
// perspective foreshortening reads clearly.
constexpr float kPlaneSize = 8.0f;
const Vertex3D kPlaneVerts[4] = {
    {{-kPlaneSize, 0.0f, -kPlaneSize}, {0, 1, 0}, {0.0f, 0.0f}},
    {{ kPlaneSize, 0.0f, -kPlaneSize}, {0, 1, 0}, {1.0f, 0.0f}},
    {{ kPlaneSize, 0.0f,  kPlaneSize}, {0, 1, 0}, {1.0f, 1.0f}},
    {{-kPlaneSize, 0.0f,  kPlaneSize}, {0, 1, 0}, {0.0f, 1.0f}},
};
// Reversed winding (CCW when viewed FROM ABOVE, which is what the
// camera sees) so the back-face cull doesn't drop the top of the plane.
const uint16_t kPlaneIndices[6] = {0, 2, 1, 0, 3, 2};
} // namespace

class CubeApp : public Application {
public:
    CubeApp() : Application(MakeConfig()) {}

    void OnStart() override {
        m_renderer.Init(GetDevice());

        // 16x16 checkerboard albedo for the cube + sphere (warm
        // orange / dark grey) and a dark earth checker for the ground
        // plane -- darker than the sky-blue fog so far edges still
        // stand out against the background.
        m_albedo  = MakeChecker(0xE5, 0x9A, 0x4A, 0x30, 0x33, 0x3A);
        m_ground  = MakeChecker(0x6A, 0x55, 0x3A, 0x3A, 0x2C, 0x1E);

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
        m_cube   = LoadObj(GetDevice(), assets + "/cube.obj");
        m_sphere = LoadObj(GetDevice(), assets + "/sphere.obj");

        m_camera.SetViewport(float(kWindowW), float(kWindowH));
        m_camera.SetPosition({0.0f, 3.5f, 5.5f});
        m_camera.LookAt({0.0f, -0.2f, 0.0f});
        m_camera.SetFovY(1.0472f);          // 60 deg
        m_camera.SetNearFar(0.1f, 100.0f);

        NARAGAME_INFO("Cube: plane + cube + sphere, dir light + hemi + fog");
    }

    void OnStop() override {
        m_renderer.Shutdown();
    }

    void OnUpdate(float dt) override {
        m_yaw += dt * 0.6f;
        if (m_yaw > 6.28318f) m_yaw -= 6.28318f;
    }

    void OnRender() override {
        // Sky-blue clear so the fog colour blends into the background.
        GetDevice().Clear(Color(0.55f, 0.58f, 0.65f, 1.0f));
        GetDevice().ClearDepth(1.0f);

        DirectionalLight light;
        light.direction      = {-0.5f, -1.0f, -0.3f};
        light.skyAmbient     = {0.45f, 0.55f, 0.65f, 1.0f};   // cool top
        light.groundAmbient  = {0.20f, 0.16f, 0.12f, 1.0f};   // warm dark bottom
        light.shadowCenter   = {0.0f, 0.3f, 0.0f};
        light.shadowRadius   = 6.0f;

        FogParams fog;
        fog.color            = {0.55f, 0.58f, 0.65f, 1.0f};   // matches clear
        fog.startDistance    = 6.0f;
        fog.endDistance      = 16.0f;

        m_renderer.Begin(m_camera.View(), m_camera.Projection(), light, fog);

        // Ground plane sits at y = -0.6 so the cube and sphere (radius
        // 0.5 around their origin) just rest on it.
        const Mat4 planeModel = mat4::Translation({0.0f, -0.6f, 0.0f});
        m_renderer.DrawMesh(m_planeVB.get(), m_planeIB.get(),
                            6, IndexFormat::Uint16,
                            planeModel, m_ground.get());

        if (m_cube.valid) {
            const Mat4 model = mat4::Translation({-1.4f, 0.0f, 0.0f})
                             * mat4::RotationY(m_yaw)
                             * mat4::RotationX(m_yaw * 0.5f);
            m_renderer.DrawMesh(m_cube.vertexBuffer.get(),
                                m_cube.indexBuffer.get(),
                                m_cube.indexCount, m_cube.indexFormat,
                                model, m_albedo.get());
        }
        if (m_sphere.valid) {
            const Mat4 model = mat4::Translation({1.4f, 0.0f, 0.0f})
                             * mat4::RotationY(m_yaw * 0.7f);
            m_renderer.DrawMesh(m_sphere.vertexBuffer.get(),
                                m_sphere.indexBuffer.get(),
                                m_sphere.indexCount, m_sphere.indexFormat,
                                model, m_albedo.get());
        }
        m_renderer.End();
    }

private:
    static AppConfig MakeConfig() {
        AppConfig c;
        c.title  = "NaraGame - Cube";
        c.width  = kWindowW;
        c.height = kWindowH;
        return c;
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
    Camera3D                    m_camera;
    std::unique_ptr<RHITexture> m_albedo;
    std::unique_ptr<RHITexture> m_ground;
    std::unique_ptr<RHIBuffer>  m_planeVB;
    std::unique_ptr<RHIBuffer>  m_planeIB;
    Mesh                        m_cube;
    Mesh                        m_sphere;
    float                       m_yaw = 0.0f;
};

Application* NaraGame::CreateApplication(int /*argc*/, char** /*argv*/) {
    return new CubeApp();
}
