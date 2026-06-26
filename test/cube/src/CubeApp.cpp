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
        // .glb single-binary container -- packed by pack-glb.ps1 from the
        // sphere.gltf / sphere.bin pair. Loading .glb exercises the
        // binary-container path inside LoadGltf; the source .gltf still
        // lives next to it as a human-readable backup.
        m_sphere = LoadGltf(GetDevice(), assets + "/sphere.glb");
        // Same swap on the skinned path: rigged-rod.glb carries skin +
        // animation as a single file.
        m_rigged = LoadGltfSkinned(GetDevice(),
                                   assets + "/rigged-rod.glb");
        // textured-cube.glb exercises the glTF material path: an embedded
        // PNG (image bufferView in the .glb's BIN chunk) feeds the
        // baseColorTexture; the loader populates m_texturedCube.material
        // directly and we hand it to DrawMesh as-is.
        m_texturedCube = LoadGltf(GetDevice(),
                                  assets + "/textured-cube.glb");

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
        Material groundMat;
        groundMat.baseColorTex = m_ground.get();
        groundMat.roughness    = 0.85f;
        m_renderer.DrawMesh(m_planeVB.get(), m_planeIB.get(),
                            6, IndexFormat::Uint16,
                            planeModel, groundMat);

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

        // Sample the rigged rod's animation at the current time and
        // submit through the skinned path. Joint matrices come out
        // ready for the GPU (jointWorld * inverseBind already folded).
        if (m_rigged.valid && !m_rigged.joints.empty()) {
            std::vector<Mat4> skinMats(m_rigged.joints.size());
            const int animIdx = m_rigged.animations.empty() ? -1 : 0;
            SampleSkinnedMeshAnim(m_rigged, animIdx, m_time, skinMats.data());
            const Mat4 rodModel = mat4::Translation({0.0f, -0.6f, 1.6f});
            Material rodMat;
            rodMat.albedoFactor = Color(0.85f, 0.30f, 0.35f, 1.0f);
            rodMat.metallic     = 0.0f;
            rodMat.roughness    = 0.35f;
            m_renderer.DrawSkinnedMesh(
                m_rigged.vertexBuffer.get(), m_rigged.indexBuffer.get(),
                m_rigged.indexCount, m_rigged.indexFormat,
                rodModel, skinMats.data(), int(m_rigged.joints.size()),
                rodMat);
        }

        // Textured cube -- proves the new glTF material path: the .glb
        // carries an embedded PNG (image bufferView) + a pbrMetallic-
        // Roughness material block, and we draw it straight from
        // m_texturedCube.material without overriding anything.
        if (m_texturedCube.valid) {
            const Mat4 cubeModel = mat4::Translation({-2.8f, 0.0f, 0.5f})
                                 * mat4::RotationY(m_yaw)
                                 * mat4::Scale({0.7f, 0.7f, 0.7f});
            m_renderer.DrawMesh(m_texturedCube.vertexBuffer.get(),
                                m_texturedCube.indexBuffer.get(),
                                m_texturedCube.indexCount,
                                m_texturedCube.indexFormat,
                                cubeModel, m_texturedCube.material);
        }

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
    void DrawSphere(float x, float y, float z, float metallic, float roughness,
                    const Color& albedo) {
        const Mat4 model = mat4::Translation({x, y, z})
                         * mat4::Scale({0.45f, 0.45f, 0.45f});
        Material mat;
        mat.albedoFactor = albedo;
        mat.metallic     = metallic;
        mat.roughness    = roughness;
        m_renderer.DrawMesh(m_sphere.vertexBuffer.get(),
                            m_sphere.indexBuffer.get(),
                            m_sphere.indexCount, m_sphere.indexFormat,
                            model, mat);
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
    Mesh                        m_texturedCube;
    SkinnedMesh                 m_rigged;
    float                       m_yaw  = 0.0f;
    float                       m_time = 0.0f;
};

Application* NaraGame::CreateApplication(int /*argc*/, char** /*argv*/) {
    return new CubeApp();
}
