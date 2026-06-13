#include "NaraGame/renderer/Renderer3D.h"

#include "NaraGame/core/Log.h"
#include "NaraGame/renderer/rhi/RHIDevice.h"
#include "NaraGame/renderer/rhi/RHIResources.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace NaraGame {

namespace {

constexpr uint32_t kShadowMapSize = 2048;

// CB layout matches HLSL exactly. Default cbuffer matrix layout in HLSL
// is column_major which matches the engine's Mat4 storage
// (data[col*4 + row]), so we just memcpy and call mul(M, v) for M * v.
struct FrameCB {
    float view[16];
    float viewProj[16];
    float lightViewProj[16];
    float lightDirWorld[4];   // .xyz = normalized direction, .w unused
    float skyAmbient[4];      // .rgb top of hemisphere
    float groundAmbient[4];   // .rgb bottom of hemisphere
    float fogColor[4];        // .rgb
    float fogParams[4];       // .x = start, .y = end, .zw unused
};
struct DrawCB {
    float model[16];
    float tint[4];
};

// One shader source with four entry points. The pipeline picks two of
// them: VSMain/PSMain for the lit main pass, VSShadow/PSShadow for the
// depth-only shadow pass.
constexpr const char* kShaderSrc = R"HLSL(
cbuffer FrameCB : register(b0) {
    float4x4 view;
    float4x4 viewProj;
    float4x4 lightViewProj;
    float4   lightDirWorld;
    float4   skyAmbient;
    float4   groundAmbient;
    float4   fogColor;
    float4   fogParams;       // .x start, .y end
};

cbuffer DrawCB : register(b1) {
    float4x4 model;
    float4   tint;
};

struct VSIn {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
};

struct VSOut {
    float4 position      : SV_POSITION;
    float3 worldNormal   : NORMAL;
    float3 viewPos       : TEXCOORD0;
    float2 uv            : TEXCOORD1;
    float4 lightSpacePos : TEXCOORD2;
};

VSOut VSMain(VSIn input) {
    VSOut output;
    float4 wp = mul(model, float4(input.position, 1.0));
    output.position      = mul(viewProj, wp);
    output.viewPos       = mul(view, wp).xyz;
    output.lightSpacePos = mul(lightViewProj, wp);
    // Upper 3x3 of model rotates / scales the normal. Assumes uniform
    // scale; a non-uniform scale would need the inverse-transpose.
    output.worldNormal   = mul((float3x3)model, input.normal);
    output.uv            = input.uv;
    return output;
}

Texture2D    albedo    : register(t0);
SamplerState sampLinear : register(s0);
// Shadow map is a depth texture (R24_UNORM_X8 SRV in the D3D11 backend)
// so the .r channel holds the depth value directly. Sample with the
// default linear sampler -- PCF / comparison sampler is a later upgrade.
Texture2D    shadowMap : register(t1);

float SampleShadow(float4 lightSpacePos) {
    // Perspective divide gets us into NDC [-1, 1].
    float3 lsp = lightSpacePos.xyz / lightSpacePos.w;
    // NDC -> texture UV (0,0 = top-left). NDC y goes up, UV v goes down,
    // so flip v.
    float2 uv = float2(lsp.x * 0.5 + 0.5, 1.0 - (lsp.y * 0.5 + 0.5));
    // Fragments outside the light frustum are treated as unshadowed --
    // otherwise the world beyond the light's coverage area pops black.
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
    if (lsp.z < 0.0 || lsp.z > 1.0) return 1.0;
    float closest = shadowMap.Sample(sampLinear, uv).r;
    // Constant bias works fine at MVP scale; a slope-scaled bias would
    // help at glancing angles.
    const float bias = 0.0015;
    return (lsp.z - bias > closest) ? 0.0 : 1.0;
}

float4 PSMain(VSOut input) : SV_TARGET {
    float3 N = normalize(input.worldNormal);
    // lightDirWorld points FROM source TOWARD surface. Lambert wants
    // the vector toward the source, so negate.
    float3 L = normalize(-lightDirWorld.xyz);
    float  NdotL = saturate(dot(N, L));

    float  shadow = SampleShadow(input.lightSpacePos);
    float4 base   = albedo.Sample(sampLinear, input.uv) * tint;

    // Hemi ambient: top half of the sky lights surfaces facing up;
    // bottom half lights surfaces facing down. Map N.y from [-1, 1]
    // to a [0, 1] blend weight.
    float  hemiK   = saturate(N.y * 0.5 + 0.5);
    float3 hemi    = lerp(groundAmbient.rgb, skyAmbient.rgb, hemiK);

    float3 lit = base.rgb * (hemi + NdotL * shadow);

    // Linear depth fog. viewPos is in view space (camera at origin,
    // looking down -Z), so the distance from camera = -viewPos.z.
    float viewDist = -input.viewPos.z;
    float f = saturate((viewDist - fogParams.x) / (fogParams.y - fogParams.x));
    float3 final = lerp(lit, fogColor.rgb, f);

    return float4(final, base.a);
}

// Shadow pass: project verts into the light's clip space, write depth.
// The pixel shader is empty -- depth-only output, no color writes.
float4 VSShadow(VSIn input) : SV_POSITION {
    float4 wp = mul(model, float4(input.position, 1.0));
    return mul(lightViewProj, wp);
}

void PSShadow() {
    // Depth-only pass; nothing to write.
}
)HLSL";

void CopyMatrix(const Mat4& src, float dst[16]) {
    for (int i = 0; i < 16; ++i) dst[i] = src.data[i];
}

} // namespace

Renderer3D::~Renderer3D() { Shutdown(); }

void Renderer3D::Init(RHIDevice& device) {
    m_device = &device;

    // Main shader (VSMain + PSMain).
    ShaderDesc sdMain;
    sdMain.vertexSource = kShaderSrc;
    sdMain.pixelSource  = kShaderSrc;
    sdMain.vertexEntry  = "VSMain";
    sdMain.pixelEntry   = "PSMain";
    sdMain.debugName    = "Renderer3D.Main";
    m_shaderMain = device.CreateShader(sdMain);
    if (!m_shaderMain || !m_shaderMain->Valid()) {
        NARAGAME_ERROR("Renderer3D: main shader compile failed");
        return;
    }

    // Shadow shader (VSShadow + PSShadow).
    ShaderDesc sdShadow;
    sdShadow.vertexSource = kShaderSrc;
    sdShadow.pixelSource  = kShaderSrc;
    sdShadow.vertexEntry  = "VSShadow";
    sdShadow.pixelEntry   = "PSShadow";
    sdShadow.debugName    = "Renderer3D.Shadow";
    m_shaderShadow = device.CreateShader(sdShadow);
    if (!m_shaderShadow || !m_shaderShadow->Valid()) {
        NARAGAME_ERROR("Renderer3D: shadow shader compile failed");
        return;
    }

    VertexLayout layout;
    layout.stride = sizeof(Vertex3D);
    layout.attributes.push_back({"POSITION", 0, VertexFormat::Float3, 0});
    layout.attributes.push_back({"NORMAL",   0, VertexFormat::Float3,
                                 uint32_t(offsetof(Vertex3D, normal))});
    layout.attributes.push_back({"TEXCOORD", 0, VertexFormat::Float2,
                                 uint32_t(offsetof(Vertex3D, uv))});

    PipelineDesc pdMain;
    pdMain.shader        = m_shaderMain.get();
    pdMain.vertexLayout  = layout;
    pdMain.blend         = BlendMode::None;
    pdMain.depthTest     = true;
    pdMain.depthWrite    = true;
    pdMain.depthCompare  = CompareFunc::Less;
    pdMain.cullMode      = CullMode::Back;
    m_pipelineMain = device.CreatePipeline(pdMain);

    // Shadow PSO: depth-only target, front-face cull to push the
    // shadow acne to the back side of the caster (the classic
    // Peter-panning trade-off; fine at MVP scale). VertexLayout is the
    // same because we still feed the position attribute through the
    // input assembler.
    PipelineDesc pdShadow;
    pdShadow.shader        = m_shaderShadow.get();
    pdShadow.vertexLayout  = layout;
    pdShadow.blend         = BlendMode::None;
    pdShadow.depthTest     = true;
    pdShadow.depthWrite    = true;
    pdShadow.depthCompare  = CompareFunc::Less;
    pdShadow.cullMode      = CullMode::Front;
    m_pipelineShadow = device.CreatePipeline(pdShadow);

    // Per-frame + per-draw constant buffers, dynamic so we can update
    // them every frame / draw with Map(DISCARD).
    BufferDesc fb;
    fb.type  = BufferType::Uniform;
    fb.usage = BufferUsage::Dynamic;
    fb.size  = sizeof(FrameCB);
    m_frameUB = device.CreateBuffer(fb);

    BufferDesc db;
    db.type  = BufferType::Uniform;
    db.usage = BufferUsage::Dynamic;
    db.size  = sizeof(DrawCB);
    m_drawUB = device.CreateBuffer(db);

    // 2x2 white albedo for null-texture draws. >=2x2 sidesteps the
    // 1x1-white-texture bug Renderer2D documents (see brittle alpha
    // sampling note in the engine memory).
    const uint8_t whitePixels[16] = {
        0xFF, 0xFF, 0xFF, 0xFF,  0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF,  0xFF, 0xFF, 0xFF, 0xFF,
    };
    TextureDesc td;
    td.width  = 2;
    td.height = 2;
    td.format = TextureFormat::RGBA8;
    td.filter = TextureFilter::Point;
    td.pixels = whitePixels;
    m_whiteTex = device.CreateTexture(td);

    // Shadow map: square depth texture. The D3D11 backend wires up
    // both a DSV (so SetRenderTarget recognises it as depth-only) and
    // an SRV (so the main pass can sample it as a regular texture).
    TextureDesc sm;
    sm.width        = kShadowMapSize;
    sm.height       = kShadowMapSize;
    sm.format       = TextureFormat::Depth24;
    sm.filter       = TextureFilter::Linear;
    sm.renderTarget = false; // depth bind flag added by the format check
    m_shadowMap = device.CreateTexture(sm);

    NARAGAME_INFO("Renderer3D initialized (shadow %ux%u + hemi ambient + linear fog)",
                  kShadowMapSize, kShadowMapSize);
}

void Renderer3D::Shutdown() {
    m_queue.clear();
    m_shadowMap.reset();
    m_whiteTex.reset();
    m_drawUB.reset();
    m_frameUB.reset();
    m_pipelineShadow.reset();
    m_pipelineMain.reset();
    m_shaderShadow.reset();
    m_shaderMain.reset();
    m_device = nullptr;
}

void Renderer3D::Begin(const Mat4& view, const Mat4& projection,
                       const DirectionalLight& light,
                       const FogParams& fog) {
    if (!m_device || !m_pipelineMain || !m_frameUB) return;
    if (m_inPass) {
        NARAGAME_WARN("Renderer3D::Begin called without matching End");
    }
    m_inPass = true;
    m_queue.clear();

    // Fit an orthographic light camera around the shadow volume. The
    // light "position" doesn't matter physically; place the eye outside
    // the volume so all casters fall in front of the near plane.
    const float r = light.shadowRadius;
    const Vec3& c = light.shadowCenter;
    // Normalize direction; if it degenerates we use a default.
    Vec3 dir = light.direction;
    float dl = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (dl > 1e-6f) { dir.x /= dl; dir.y /= dl; dir.z /= dl; }
    else            { dir = {0.0f, -1.0f, 0.0f}; }
    const Vec3 eye{c.x - dir.x * r * 2.0f,
                   c.y - dir.y * r * 2.0f,
                   c.z - dir.z * r * 2.0f};
    // Up vector: use +Y unless the light is shining straight down/up,
    // in which case pick +Z so LookAt doesn't degenerate.
    Vec3 up{0.0f, 1.0f, 0.0f};
    if (std::abs(dir.y) > 0.999f) up = {0.0f, 0.0f, 1.0f};
    const Mat4 lightView = mat4::LookAt(eye, c, up);
    const Mat4 lightProj = mat4::Ortho(-r, r, -r, r, 0.1f, r * 4.0f);
    const Mat4 lightVP   = lightProj * lightView;

    FrameCB cb;
    CopyMatrix(view,        cb.view);
    CopyMatrix(projection * view, cb.viewProj);
    CopyMatrix(lightVP,     cb.lightViewProj);
    cb.lightDirWorld[0] = dir.x;
    cb.lightDirWorld[1] = dir.y;
    cb.lightDirWorld[2] = dir.z;
    cb.lightDirWorld[3] = 0.0f;
    cb.skyAmbient[0]    = light.skyAmbient.r;
    cb.skyAmbient[1]    = light.skyAmbient.g;
    cb.skyAmbient[2]    = light.skyAmbient.b;
    cb.skyAmbient[3]    = 1.0f;
    cb.groundAmbient[0] = light.groundAmbient.r;
    cb.groundAmbient[1] = light.groundAmbient.g;
    cb.groundAmbient[2] = light.groundAmbient.b;
    cb.groundAmbient[3] = 1.0f;
    cb.fogColor[0]      = fog.color.r;
    cb.fogColor[1]      = fog.color.g;
    cb.fogColor[2]      = fog.color.b;
    cb.fogColor[3]      = 1.0f;
    cb.fogParams[0]     = fog.startDistance;
    cb.fogParams[1]     = fog.endDistance;
    cb.fogParams[2]     = 0.0f;
    cb.fogParams[3]     = 0.0f;
    m_frameUB->Update(&cb, sizeof(cb));
}

void Renderer3D::DrawMesh(RHIBuffer* vb, RHIBuffer* ib, uint32_t indexCount,
                          IndexFormat indexFormat,
                          const Mat4& model, RHITexture* texture,
                          const Color& tint) {
    if (!m_device || !vb || !ib || indexCount == 0) return;
    if (!m_inPass) {
        NARAGAME_WARN("Renderer3D::DrawMesh called outside Begin/End");
        return;
    }
    QueuedMesh q;
    q.vb          = vb;
    q.ib          = ib;
    q.indexCount  = indexCount;
    q.indexFormat = indexFormat;
    q.model       = model;
    q.texture     = texture ? texture : m_whiteTex.get();
    q.tint        = tint;
    m_queue.push_back(q);
}

void Renderer3D::End() {
    if (!m_device || !m_pipelineMain) {
        m_inPass = false;
        m_queue.clear();
        return;
    }
    if (m_queue.empty()) {
        m_inPass = false;
        return;
    }
    // The light-space matrix already lives in m_frameUB; just need to
    // run the two passes against the same FrameCB.
    RunShadowPass({});  // matrix arg unused; just kept for clarity if we
                        // later split frame data per pass
    RunMainPass();
    m_queue.clear();
    m_inPass = false;
}

void Renderer3D::RunShadowPass(const Mat4& /*lightViewProj*/) {
    // Bind the shadow map as a depth-only render target. D3D11Device's
    // SetRenderTarget detects the depth-only case (rtv == null, dsv !=
    // null) and clears the colour binding for us; ClearDepth wipes the
    // map to 1.0 (far).
    m_device->SetRenderTarget(m_shadowMap.get());
    m_device->ClearDepth(1.0f);

    m_device->BindPipeline(m_pipelineShadow.get());
    m_device->BindUniformBuffer(0, m_frameUB.get());

    DrawCB cb;
    for (const QueuedMesh& q : m_queue) {
        CopyMatrix(q.model, cb.model);
        cb.tint[0] = q.tint.r;
        cb.tint[1] = q.tint.g;
        cb.tint[2] = q.tint.b;
        cb.tint[3] = q.tint.a;
        m_drawUB->Update(&cb, sizeof(cb));
        m_device->BindUniformBuffer(1, m_drawUB.get());
        m_device->BindVertexBuffer(q.vb, sizeof(Vertex3D), 0);
        m_device->BindIndexBuffer(q.ib, q.indexFormat);
        m_device->DrawIndexed(q.indexCount, 0);
    }
}

void Renderer3D::RunMainPass() {
    // Back to the backbuffer + its matching depth view. The Clear /
    // ClearDepth for the main framebuffer is the caller's responsibility
    // (we don't want to wipe e.g. a 2D HUD that was drawn first).
    m_device->SetRenderTarget(nullptr);

    m_device->BindPipeline(m_pipelineMain.get());
    m_device->BindUniformBuffer(0, m_frameUB.get());
    // Shadow map sits at t1; the per-mesh albedo will land at t0 below.
    m_device->BindTexture(1, m_shadowMap.get());

    DrawCB cb;
    for (const QueuedMesh& q : m_queue) {
        CopyMatrix(q.model, cb.model);
        cb.tint[0] = q.tint.r;
        cb.tint[1] = q.tint.g;
        cb.tint[2] = q.tint.b;
        cb.tint[3] = q.tint.a;
        m_drawUB->Update(&cb, sizeof(cb));
        m_device->BindUniformBuffer(1, m_drawUB.get());
        m_device->BindTexture(0, q.texture);
        m_device->BindVertexBuffer(q.vb, sizeof(Vertex3D), 0);
        m_device->BindIndexBuffer(q.ib, q.indexFormat);
        m_device->DrawIndexed(q.indexCount, 0);
    }
}

} // namespace NaraGame
