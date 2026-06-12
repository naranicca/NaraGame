// Smoke test for the Renderer3D pipeline. A single textured cube spins
// in place under one directional light. There is no game logic, no
// input handling, no asset files -- the cube mesh is hard-coded with
// flat-shaded normals (24 verts / 36 indices) and the albedo texture is
// a 16x16 checkerboard built in code so the engine's 3D path can be
// exercised without dragging in mesh / texture loaders.

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

using namespace NaraGame;

namespace {

constexpr uint32_t kWindowW = 1280;
constexpr uint32_t kWindowH = 720;

// 24 verts: 4 per face × 6 faces. Flat shading needs one normal per
// face, so each face owns its own 4 corner verts (no sharing across
// faces). Faces wound CCW when viewed from outside so the Back cull in
// Renderer3D's default pipeline keeps the inside surfaces hidden.
const Vertex3D kCubeVerts[24] = {
    // +X (right)
    {{ 0.5f, -0.5f, -0.5f}, { 1, 0, 0}, {0, 1}},
    {{ 0.5f,  0.5f, -0.5f}, { 1, 0, 0}, {0, 0}},
    {{ 0.5f,  0.5f,  0.5f}, { 1, 0, 0}, {1, 0}},
    {{ 0.5f, -0.5f,  0.5f}, { 1, 0, 0}, {1, 1}},
    // -X (left)
    {{-0.5f, -0.5f,  0.5f}, {-1, 0, 0}, {0, 1}},
    {{-0.5f,  0.5f,  0.5f}, {-1, 0, 0}, {0, 0}},
    {{-0.5f,  0.5f, -0.5f}, {-1, 0, 0}, {1, 0}},
    {{-0.5f, -0.5f, -0.5f}, {-1, 0, 0}, {1, 1}},
    // +Y (top)
    {{-0.5f,  0.5f, -0.5f}, { 0, 1, 0}, {0, 1}},
    {{-0.5f,  0.5f,  0.5f}, { 0, 1, 0}, {0, 0}},
    {{ 0.5f,  0.5f,  0.5f}, { 0, 1, 0}, {1, 0}},
    {{ 0.5f,  0.5f, -0.5f}, { 0, 1, 0}, {1, 1}},
    // -Y (bottom)
    {{-0.5f, -0.5f,  0.5f}, { 0,-1, 0}, {0, 1}},
    {{-0.5f, -0.5f, -0.5f}, { 0,-1, 0}, {0, 0}},
    {{ 0.5f, -0.5f, -0.5f}, { 0,-1, 0}, {1, 0}},
    {{ 0.5f, -0.5f,  0.5f}, { 0,-1, 0}, {1, 1}},
    // +Z (front)
    {{ 0.5f, -0.5f,  0.5f}, { 0, 0, 1}, {0, 1}},
    {{ 0.5f,  0.5f,  0.5f}, { 0, 0, 1}, {0, 0}},
    {{-0.5f,  0.5f,  0.5f}, { 0, 0, 1}, {1, 0}},
    {{-0.5f, -0.5f,  0.5f}, { 0, 0, 1}, {1, 1}},
    // -Z (back)
    {{-0.5f, -0.5f, -0.5f}, { 0, 0,-1}, {0, 1}},
    {{-0.5f,  0.5f, -0.5f}, { 0, 0,-1}, {0, 0}},
    {{ 0.5f,  0.5f, -0.5f}, { 0, 0,-1}, {1, 0}},
    {{ 0.5f, -0.5f, -0.5f}, { 0, 0,-1}, {1, 1}},
};

// Two triangles per face, 6 faces.
const uint16_t kCubeIndices[36] = {
     0,  1,  2,    0,  2,  3,    // +X
     4,  5,  6,    4,  6,  7,    // -X
     8,  9, 10,    8, 10, 11,    // +Y
    12, 13, 14,   12, 14, 15,    // -Y
    16, 17, 18,   16, 18, 19,    // +Z
    20, 21, 22,   20, 22, 23,    // -Z
};

} // namespace

class CubeApp : public Application {
public:
    CubeApp() : Application(MakeConfig()) {}

    void OnStart() override {
        m_renderer.Init(GetDevice());

        // 16x16 checkerboard. Even cells are warm orange, odd cells are
        // cool grey, so each face of the cube reads as having its own
        // up-direction. RGBA8 packed row-major top-to-bottom.
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

        BufferDesc vb;
        vb.type        = BufferType::Vertex;
        vb.usage       = BufferUsage::Static;
        vb.size        = sizeof(kCubeVerts);
        vb.initialData = kCubeVerts;
        m_vb = GetDevice().CreateBuffer(vb);

        BufferDesc ib;
        ib.type        = BufferType::Index;
        ib.usage       = BufferUsage::Static;
        ib.size        = sizeof(kCubeIndices);
        ib.initialData = kCubeIndices;
        m_ib = GetDevice().CreateBuffer(ib);

        m_camera.SetViewport(float(kWindowW), float(kWindowH));
        m_camera.SetPosition({2.0f, 1.5f, 3.0f});
        m_camera.LookAt({0.0f, 0.0f, 0.0f});
        m_camera.SetFovY(1.0472f);          // 60 deg
        m_camera.SetNearFar(0.1f, 50.0f);

        NARAGAME_INFO("Cube: spawned 1 textured cube, 1 directional light");
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
        // Combined yaw + pitch so the texture isn't always seen straight-
        // on; pitch is half the yaw so every face cycles through view.
        const Mat4 model = mat4::RotationY(m_yaw) * mat4::RotationX(m_yaw * 0.5f);
        m_renderer.DrawMesh(m_vb.get(), m_ib.get(),
                            /*indexCount=*/36, IndexFormat::Uint16,
                            model, m_albedo.get());
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
    std::unique_ptr<RHIBuffer>  m_vb;
    std::unique_ptr<RHIBuffer>  m_ib;
    float                       m_yaw = 0.0f;
};

Application* NaraGame::CreateApplication(int /*argc*/, char** /*argv*/) {
    return new CubeApp();
}
