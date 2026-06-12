#include "NaraGame/renderer/Renderer3D.h"

#include "NaraGame/core/Log.h"
#include "NaraGame/renderer/rhi/RHIDevice.h"
#include "NaraGame/renderer/rhi/RHIResources.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace NaraGame {

namespace {

// CB layout matches HLSL exactly (column-major matrices, 16-byte
// alignment). Vec3 fields ride along inside a Vec4 so HLSL doesn't
// silently pad them and slide subsequent fields off.
struct FrameCB {
    float viewProj[16];
    float lightDirWorld[4];   // xyz = light direction, w unused
    float ambientColor[4];    // rgb = ambient, a unused
};
struct DrawCB {
    float model[16];
    float tint[4];
};

constexpr const char* kShaderSrc = R"HLSL(
// Default cbuffer matrix layout in HLSL is column_major, which matches
// the engine's Mat4 column-major storage (data[col*4 + row]) so we can
// just memcpy data into the buffer and call mul(M, v) for M * v.
cbuffer FrameCB : register(b0) {
    float4x4 viewProj;
    float4 lightDirWorld;   // .xyz = light direction (normalized), .w unused
    float4 ambient;         // .rgb = ambient color
};

cbuffer DrawCB : register(b1) {
    float4x4 model;
    float4 tint;
};

struct VSIn {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
};

struct VSOut {
    float4 position    : SV_POSITION;
    float3 worldNormal : NORMAL;
    float2 uv          : TEXCOORD0;
};

VSOut VSMain(VSIn input) {
    VSOut output;
    float4 wp = mul(model, float4(input.position, 1.0));
    output.position    = mul(viewProj, wp);
    // Upper 3x3 of model rotates / scales the normal. Assumes uniform
    // scale; a non-uniform scale would need the inverse-transpose.
    output.worldNormal = mul((float3x3)model, input.normal);
    output.uv          = input.uv;
    return output;
}

Texture2D albedo : register(t0);
SamplerState samp : register(s0);

float4 PSMain(VSOut input) : SV_TARGET {
    float3 N = normalize(input.worldNormal);
    // lightDirWorld points FROM the source toward the surface. Lambert
    // wants the vector toward the source, so negate.
    float3 L = normalize(-lightDirWorld.xyz);
    float  NdotL = saturate(dot(N, L));
    float4 base  = albedo.Sample(samp, input.uv) * tint;
    float3 lit   = base.rgb * (ambient.rgb + NdotL);
    return float4(lit, base.a);
}
)HLSL";

// HLSL cbuffers default to column_major matrices, which matches the
// engine's Mat4 storage exactly. Just copy the floats verbatim.
void CopyMatrix(const Mat4& src, float dst[16]) {
    for (int i = 0; i < 16; ++i) dst[i] = src.data[i];
}

} // namespace

Renderer3D::~Renderer3D() { Shutdown(); }

void Renderer3D::Init(RHIDevice& device) {
    m_device = &device;

    ShaderDesc sd;
    sd.vertexSource = kShaderSrc;
    sd.pixelSource  = kShaderSrc;
    sd.debugName    = "Renderer3D";
    m_shader = device.CreateShader(sd);
    if (!m_shader || !m_shader->Valid()) {
        NARAGAME_ERROR("Renderer3D: shader compile failed");
        return;
    }

    PipelineDesc pd;
    pd.shader = m_shader.get();
    pd.vertexLayout.stride = sizeof(Vertex3D);
    pd.vertexLayout.attributes.push_back({"POSITION", 0, VertexFormat::Float3, 0});
    pd.vertexLayout.attributes.push_back({"NORMAL",   0, VertexFormat::Float3,
                                          uint32_t(offsetof(Vertex3D, normal))});
    pd.vertexLayout.attributes.push_back({"TEXCOORD", 0, VertexFormat::Float2,
                                          uint32_t(offsetof(Vertex3D, uv))});
    // 3D defaults: blend off so opaque geometry composites without
    // touching the destination alpha; depth test + write on; cull back
    // faces (front face = CCW per glTF convention).
    pd.blend       = BlendMode::None;
    pd.depthTest   = true;
    pd.depthWrite  = true;
    pd.depthCompare = CompareFunc::Less;
    pd.cullMode    = CullMode::Back;
    m_pipeline = device.CreatePipeline(pd);

    // Per-frame and per-draw constant buffers. Dynamic so we can update
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

    // Default 2x2 white texture. 1x1 would work fine here -- this is
    // the 3D path, not the 2D Renderer2D quirk that needs >= 2x2 to
    // avoid the documented alpha bug -- but 2x2 keeps us symmetric
    // with the rest of the engine and costs nothing.
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

    NARAGAME_INFO("Renderer3D initialized (Lambert + 1 directional light)");
}

void Renderer3D::Shutdown() {
    m_pipeline.reset();
    m_shader.reset();
    m_frameUB.reset();
    m_drawUB.reset();
    m_whiteTex.reset();
    m_device = nullptr;
}

void Renderer3D::Begin(const Mat4& view, const Mat4& projection,
                      const Vec3& lightDir, const Color& ambient) {
    if (!m_device || !m_pipeline || !m_frameUB) return;
    if (m_inPass) {
        NARAGAME_WARN("Renderer3D::Begin called without matching End");
    }
    m_inPass = true;

    const Mat4 vp = projection * view;
    FrameCB cb;
    CopyMatrix(vp, cb.viewProj);
    // Normalize light direction so the shader doesn't need to.
    const float lenSq = lightDir.x * lightDir.x + lightDir.y * lightDir.y
                       + lightDir.z * lightDir.z;
    const float inv = lenSq > 1e-8f ? 1.0f / std::sqrt(lenSq) : 0.0f;
    cb.lightDirWorld[0] = lightDir.x * inv;
    cb.lightDirWorld[1] = lightDir.y * inv;
    cb.lightDirWorld[2] = lightDir.z * inv;
    cb.lightDirWorld[3] = 0.0f;
    cb.ambientColor[0] = ambient.r;
    cb.ambientColor[1] = ambient.g;
    cb.ambientColor[2] = ambient.b;
    cb.ambientColor[3] = ambient.a;
    m_frameUB->Update(&cb, sizeof(cb));

    m_device->BindPipeline(m_pipeline.get());
    m_device->BindUniformBuffer(0, m_frameUB.get());
}

void Renderer3D::End() {
    m_inPass = false;
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

    DrawCB cb;
    CopyMatrix(model, cb.model);
    cb.tint[0] = tint.r;
    cb.tint[1] = tint.g;
    cb.tint[2] = tint.b;
    cb.tint[3] = tint.a;
    m_drawUB->Update(&cb, sizeof(cb));

    m_device->BindUniformBuffer(1, m_drawUB.get());
    m_device->BindTexture(0, texture ? texture : m_whiteTex.get());
    m_device->BindVertexBuffer(vb, sizeof(Vertex3D), 0);
    m_device->BindIndexBuffer(ib, indexFormat);
    m_device->DrawIndexed(indexCount, 0);
}

} // namespace NaraGame
