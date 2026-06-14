#include "NaraGame/renderer/PostProcess.h"

#include "NaraGame/core/Log.h"
#include "NaraGame/renderer/rhi/RHIDevice.h"
#include "NaraGame/renderer/rhi/RHIResources.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace NaraGame {

namespace {

// Fullscreen quad vertices in NDC. The post-process shaders read .uv
// directly, so the UV layout matches the screen (top-left origin).
struct PostVertex {
    float x, y;
    float u, v;
};
const PostVertex kQuadVerts[4] = {
    {-1.0f, -1.0f, 0.0f, 1.0f},
    { 1.0f, -1.0f, 1.0f, 1.0f},
    { 1.0f,  1.0f, 1.0f, 0.0f},
    {-1.0f,  1.0f, 0.0f, 0.0f},
};
const uint16_t kQuadIndices[6] = {0, 1, 2, 0, 2, 3};

// Shared cbuffer for every post-process pass. Some passes only read a
// subset of the fields; HLSL doesn't mind unused fields and rolling
// them into one CB saves bind churn.
struct PostCB {
    float texelSize[4];    // .xy = 1/width, 1/height
    float bloomParams[4];  // .x threshold, .y strength
    float dofParams[4];    // .x focusDistance, .y focusRange
    float misc[4];         // .x exposure
};

constexpr const char* kVertSrc = R"HLSL(
struct VSIn  { float2 position : POSITION; float2 uv : TEXCOORD0; };
struct VSOut { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut VSMain(VSIn input) {
    VSOut o;
    o.position = float4(input.position, 0.0, 1.0);
    o.uv = input.uv;
    return o;
}
)HLSL";

// Bright-pass: extract HDR pixels above threshold for the bloom blur.
// Use the perceived brightness rather than a single channel so a
// strong red sample blooms the same amount a strong white one does.
constexpr const char* kBrightSrc = R"HLSL(
cbuffer PostCB : register(b0) {
    float4 texelSize;
    float4 bloomParams;
    float4 dofParams;
    float4 misc;
};
Texture2D    sceneHDR : register(t0);
SamplerState sampLin  : register(s0);

struct VSOut { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

float4 PSMain(VSOut input) : SV_TARGET {
    float3 c = sceneHDR.Sample(sampLin, input.uv).rgb;
    float  l = max(c.r, max(c.g, c.b));
    float  t = bloomParams.x;
    float  k = smoothstep(t, t + 0.5, l);
    return float4(c * k, 1.0);
}
)HLSL";

// Separable 9-tap Gaussian. PostCB.dofParams.zw piggy-backs the direction
// vector for the blur (set to (1,0) for horizontal, (0,1) for vertical
// by RunPost).
constexpr const char* kBlurSrc = R"HLSL(
cbuffer PostCB : register(b0) {
    float4 texelSize;
    float4 bloomParams;
    float4 dofParams;       // .zw = blur direction this pass
    float4 misc;
};
Texture2D    src     : register(t0);
SamplerState sampLin : register(s0);

struct VSOut { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

static const float kW[5] = { 0.227027, 0.194594, 0.121622, 0.054054, 0.016216 };

float4 PSMain(VSOut input) : SV_TARGET {
    float2 step = dofParams.zw * texelSize.xy;
    float3 sum = src.Sample(sampLin, input.uv).rgb * kW[0];
    [unroll]
    for (int i = 1; i < 5; ++i) {
        float2 off = step * float(i);
        sum += src.Sample(sampLin, input.uv + off).rgb * kW[i];
        sum += src.Sample(sampLin, input.uv - off).rgb * kW[i];
    }
    return float4(sum, 1.0);
}
)HLSL";

// Composite: read HDR scene + bloom + depth, compute DoF blend, apply
// ACES tone map, write LDR to the backbuffer.
constexpr const char* kCompositeSrc = R"HLSL(
cbuffer PostCB : register(b0) {
    float4 texelSize;
    float4 bloomParams;
    float4 dofParams;       // .x focusDistance, .y focusRange
    float4 misc;            // .x exposure
};
Texture2D    sceneHDR  : register(t0);
Texture2D    bloomTex  : register(t1);
Texture2D    depthTex  : register(t2);
SamplerState sampLin   : register(s0);

struct VSOut { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

// Knarkowicz's ACES filmic fit. Maps HDR -> [0, 1] with a soft shoulder.
float3 ACES(float3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// Reconstruct linear view-space distance from a D24 depth value. The
// engine's Mat4::Perspective is right-handed with depth in [0, 1], so
// the standard reverse is:
//   linear = (n * f) / (f - z * (f - n))
// where n / f are the near / far plane distances. Pulling them in as
// hard-coded constants matches the cube test app; a real renderer
// would push them through the cbuffer.
static const float kNear = 0.1;
static const float kFar  = 100.0;

float LinearDepth(float z) {
    return (kNear * kFar) / (kFar - z * (kFar - kNear));
}

float4 PSMain(VSOut input) : SV_TARGET {
    float3 hdr   = sceneHDR.Sample(sampLin, input.uv).rgb;
    float3 bloom = bloomTex.Sample(sampLin, input.uv).rgb;

    // Depth-of-field: blur amount ramps with |viewDist - focus| / range.
    // Use the already-blurred bloom buffer as the "out of focus" source
    // -- it's a cheap reuse that gives the foreground sharpness +
    // background haze look without authoring a separate gather pass.
    float z          = depthTex.Sample(sampLin, input.uv).r;
    float viewDepth  = LinearDepth(z);
    float coc        = saturate(abs(viewDepth - dofParams.x) / max(dofParams.y, 0.001));
    float3 blurred   = bloom;
    float3 dof       = lerp(hdr, blurred, coc * 0.6);

    float3 combined  = dof + bloom * bloomParams.y;
    combined        *= misc.x;        // exposure
    float3 ldr       = ACES(combined);
    return float4(ldr, 1.0);
}
)HLSL";

} // namespace

PostProcess::~PostProcess() { Shutdown(); }

void PostProcess::Init(RHIDevice& device, uint32_t width, uint32_t height) {
    m_device = &device;
    m_width  = width;
    m_height = height;
    CreateRTs();
    CreatePipelines();
    NARAGAME_INFO("PostProcess initialized (HDR %ux%u + bloom + DoF + ACES)",
                  width, height);
}

void PostProcess::Shutdown() {
    m_compositePipeline.reset();
    m_blurPipeline.reset();
    m_brightPipeline.reset();
    m_compositeShader.reset();
    m_blurShader.reset();
    m_brightShader.reset();
    m_postUB.reset();
    m_quadIB.reset();
    m_quadVB.reset();
    m_bloomBlurV.reset();
    m_bloomBlurH.reset();
    m_bloomBright.reset();
    m_sceneDepth.reset();
    m_sceneHDR.reset();
    m_device = nullptr;
}

void PostProcess::Resize(uint32_t width, uint32_t height) {
    if (width == m_width && height == m_height) return;
    m_width  = width;
    m_height = height;
    CreateRTs();
}

void PostProcess::CreateRTs() {
    if (!m_device) return;

    TextureDesc hdr;
    hdr.width        = m_width;
    hdr.height       = m_height;
    hdr.format       = TextureFormat::RGBA16F;
    hdr.filter       = TextureFilter::Linear;
    hdr.renderTarget = true;
    m_sceneHDR = m_device->CreateTexture(hdr);

    TextureDesc dep;
    dep.width        = m_width;
    dep.height       = m_height;
    dep.format       = TextureFormat::Depth24;
    dep.filter       = TextureFilter::Linear;
    dep.renderTarget = false;
    m_sceneDepth = m_device->CreateTexture(dep);

    // Bloom passes work at half-res; that's the standard cheap trick
    // and the blur reach matches the look of a real Gaussian without
    // taking 4x the samples.
    const uint32_t bw = std::max<uint32_t>(1, m_width  / 2);
    const uint32_t bh = std::max<uint32_t>(1, m_height / 2);

    TextureDesc bright;
    bright.width        = bw;
    bright.height       = bh;
    bright.format       = TextureFormat::RGBA16F;
    bright.filter       = TextureFilter::Linear;
    bright.renderTarget = true;
    m_bloomBright = m_device->CreateTexture(bright);
    m_bloomBlurH  = m_device->CreateTexture(bright);
    m_bloomBlurV  = m_device->CreateTexture(bright);
}

void PostProcess::CreatePipelines() {
    if (!m_device) return;

    BufferDesc vb;
    vb.type        = BufferType::Vertex;
    vb.usage       = BufferUsage::Static;
    vb.size        = sizeof(kQuadVerts);
    vb.initialData = kQuadVerts;
    m_quadVB = m_device->CreateBuffer(vb);

    BufferDesc ib;
    ib.type        = BufferType::Index;
    ib.usage       = BufferUsage::Static;
    ib.size        = sizeof(kQuadIndices);
    ib.initialData = kQuadIndices;
    m_quadIB = m_device->CreateBuffer(ib);

    BufferDesc ub;
    ub.type  = BufferType::Uniform;
    ub.usage = BufferUsage::Dynamic;
    ub.size  = sizeof(PostCB);
    m_postUB = m_device->CreateBuffer(ub);

    // All three pipelines share the same vertex shader source + layout
    // (the fullscreen quad); only the pixel shader changes.
    auto makeShader = [&](const char* ps, const char* name) {
        ShaderDesc sd;
        sd.vertexSource = kVertSrc;
        sd.pixelSource  = ps;
        sd.vertexEntry  = "VSMain";
        sd.pixelEntry   = "PSMain";
        sd.debugName    = name;
        return m_device->CreateShader(sd);
    };
    m_brightShader    = makeShader(kBrightSrc,    "PostProcess.Bright");
    m_blurShader      = makeShader(kBlurSrc,      "PostProcess.Blur");
    m_compositeShader = makeShader(kCompositeSrc, "PostProcess.Composite");

    VertexLayout layout;
    layout.stride = sizeof(PostVertex);
    layout.attributes.push_back({"POSITION", 0, VertexFormat::Float2, 0});
    layout.attributes.push_back({"TEXCOORD", 0, VertexFormat::Float2,
                                 uint32_t(offsetof(PostVertex, u))});

    auto makePipeline = [&](RHIShader* shader) {
        PipelineDesc pd;
        pd.shader        = shader;
        pd.vertexLayout  = layout;
        pd.blend         = BlendMode::None;
        pd.depthTest     = false;
        pd.depthWrite    = false;
        pd.cullMode      = CullMode::None;
        return m_device->CreatePipeline(pd);
    };
    m_brightPipeline    = makePipeline(m_brightShader.get());
    m_blurPipeline      = makePipeline(m_blurShader.get());
    m_compositePipeline = makePipeline(m_compositeShader.get());
}

void PostProcess::BindSceneTarget() {
    if (!m_device || !m_sceneHDR || !m_sceneDepth) return;
    m_device->SetRenderTargets(m_sceneHDR.get(), m_sceneDepth.get());
}

void PostProcess::DrawFullscreen() {
    m_device->BindVertexBuffer(m_quadVB.get(), sizeof(PostVertex), 0);
    m_device->BindIndexBuffer(m_quadIB.get(), IndexFormat::Uint16);
    m_device->DrawIndexed(6, 0);
}

void PostProcess::RunPost(const PostProcessParams& params) {
    if (!m_device || !m_sceneHDR) return;

    PostCB cb{};

    // --- Bright pass: scene HDR -> bloomBright (half-res) ---
    const float halfW = float(std::max<uint32_t>(1, m_width  / 2));
    const float halfH = float(std::max<uint32_t>(1, m_height / 2));
    cb.texelSize[0]   = 1.0f / halfW;
    cb.texelSize[1]   = 1.0f / halfH;
    cb.bloomParams[0] = params.bloomThreshold;
    cb.bloomParams[1] = params.bloomStrength;
    m_postUB->Update(&cb, sizeof(cb));

    m_device->SetRenderTarget(m_bloomBright.get());
    m_device->BindPipeline(m_brightPipeline.get());
    m_device->BindUniformBuffer(0, m_postUB.get());
    m_device->BindTexture(0, m_sceneHDR.get());
    DrawFullscreen();

    // --- Horizontal blur: bloomBright -> bloomBlurH ---
    cb.dofParams[2] = 1.0f;   // direction.x
    cb.dofParams[3] = 0.0f;   // direction.y
    m_postUB->Update(&cb, sizeof(cb));
    m_device->SetRenderTarget(m_bloomBlurH.get());
    m_device->BindPipeline(m_blurPipeline.get());
    m_device->BindUniformBuffer(0, m_postUB.get());
    m_device->BindTexture(0, m_bloomBright.get());
    DrawFullscreen();

    // --- Vertical blur: bloomBlurH -> bloomBlurV ---
    cb.dofParams[2] = 0.0f;
    cb.dofParams[3] = 1.0f;
    m_postUB->Update(&cb, sizeof(cb));
    m_device->SetRenderTarget(m_bloomBlurV.get());
    m_device->BindTexture(0, m_bloomBlurH.get());
    DrawFullscreen();

    // --- Composite to backbuffer with DoF + ACES ---
    cb.texelSize[0]   = 1.0f / float(m_width);
    cb.texelSize[1]   = 1.0f / float(m_height);
    cb.bloomParams[0] = params.bloomThreshold;
    cb.bloomParams[1] = params.bloomStrength;
    cb.dofParams[0]   = params.focusDistance;
    cb.dofParams[1]   = params.focusRange;
    cb.dofParams[2]   = 0.0f;
    cb.dofParams[3]   = 0.0f;
    cb.misc[0]        = params.exposure;
    m_postUB->Update(&cb, sizeof(cb));

    m_device->SetRenderTarget(nullptr);   // backbuffer + its depth view
    m_device->BindPipeline(m_compositePipeline.get());
    m_device->BindUniformBuffer(0, m_postUB.get());
    m_device->BindTexture(0, m_sceneHDR.get());
    m_device->BindTexture(1, m_bloomBlurV.get());
    m_device->BindTexture(2, m_sceneDepth.get());
    DrawFullscreen();
}

} // namespace NaraGame
