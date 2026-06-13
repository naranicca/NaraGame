// Smoke test for Renderer3D + the .obj mesh loader. A cube and a sphere
// load from test/cube/assets, then spin side-by-side in the same scene
// under one directional light. The cube exercises the flat-shaded path
// (one normal per face), the sphere exercises the smooth-shaded path
// (per-vertex normals + many shared vertices via the dedup table).
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

#ifndef NARAGAME_ASSETS_DIR
#define NARAGAME_ASSETS_DIR "assets"
#endif

using namespace NaraGame;

namespace {
constexpr uint32_t kWindowW = 1280;
constexpr uint32_t kWindowH = 720;
} // namespace

class CubeApp : public Application {
public:
    CubeApp() : Application(MakeConfig()) {}

    void OnStart() override {
        m_renderer.Init(GetDevice());

        // 16x16 checkerboard albedo, shared by both meshes. RGBA8, even
        // cells warm orange, odd cells cool grey -- so the faces of the
        // cube and the seams of the sphere both read clearly.
        std::array<uint8_t, 16 * 16 * 4> pixels{};
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                const bool light = ((x ^ y) & 1) == 0;
                const uint8_t r = light ? 0xE5 : 0x30;
                const uint8_t g = light ? 0x9A : 0x33;
                const uint8_t b = light ? 0x4A : 0x3A;
                const size_t i = (y * 16 + x) * 4;
                pixels[i + 0] = r;
                pixels[i + 1] = g;
                pixels[i + 2] = b;
                pixels[i + 3] = 0xFF;
            }
        }
        TextureDesc td;
        td.width  = 16;
        td.height = 16;
        td.format = TextureFormat::RGBA8;
        td.filter = TextureFilter::Point;
        td.pixels = pixels.data();
        m_albedo = GetDevice().CreateTexture(td);

        const std::string assets = NARAGAME_ASSETS_DIR;
        m_cube   = LoadObj(GetDevice(), assets + "/cube.obj");
        m_sphere = LoadObj(GetDevice(), assets + "/sphere.obj");

        m_camera.SetViewport(float(kWindowW), float(kWindowH));
        m_camera.SetPosition({0.0f, 1.2f, 4.5f});
        m_camera.LookAt({0.0f, 0.0f, 0.0f});
        m_camera.SetFovY(1.0472f);          // 60 deg
        m_camera.SetNearFar(0.1f, 50.0f);

        NARAGAME_INFO("Cube: scene = 1 cube (left) + 1 sphere (right)");
    }

    void OnStop() override {
        m_renderer.Shutdown();
    }

    void OnUpdate(float dt) override {
        m_yaw += dt * 0.6f;
        if (m_yaw > 6.28318f) m_yaw -= 6.28318f;
    }

    void OnRender() override {
        GetDevice().Clear(Color(0.10f, 0.11f, 0.14f, 1.0f));
        GetDevice().ClearDepth(1.0f);

        m_renderer.Begin(m_camera.View(), m_camera.Projection(),
                         /*lightDir=*/{-0.4f, -1.0f, -0.3f},
                         /*ambient=*/Color(0.20f, 0.20f, 0.25f, 1.0f));

        if (m_cube.valid) {
            // Cube on the left at x = -1.2, rotating to expose every face.
            const Mat4 model = mat4::Translation({-1.2f, 0.0f, 0.0f})
                             * mat4::RotationY(m_yaw)
                             * mat4::RotationX(m_yaw * 0.5f);
            m_renderer.DrawMesh(m_cube.vertexBuffer.get(),
                                m_cube.indexBuffer.get(),
                                m_cube.indexCount, m_cube.indexFormat,
                                model, m_albedo.get());
        }
        if (m_sphere.valid) {
            // Sphere on the right at x = +1.2, slower rotation so the
            // checkerboard wrap is readable.
            const Mat4 model = mat4::Translation({1.2f, 0.0f, 0.0f})
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

    Renderer3D                  m_renderer;
    Camera3D                    m_camera;
    std::unique_ptr<RHITexture> m_albedo;
    Mesh                        m_cube;
    Mesh                        m_sphere;
    float                       m_yaw = 0.0f;
};

Application* NaraGame::CreateApplication(int /*argc*/, char** /*argv*/) {
    return new CubeApp();
}
