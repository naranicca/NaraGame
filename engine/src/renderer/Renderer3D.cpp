#include "NaraGame/renderer/Renderer3D.h"

#include "NaraGame/core/Log.h"
#include "NaraGame/renderer/rhi/RHIDevice.h"
#include "NaraGame/renderer/rhi/RHIResources.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

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
    float cameraPosWorld[4];  // .xyz = camera world position
};
struct DrawCB {
    float model[16];
    float albedoFactor[4];   // .rgba multiplied against albedo sample
    float mrParams[4];       // .x = metallic, .y = roughness, .zw unused
    float emissiveFactor[4]; // .rgb multiplied against emissive sample
};
// kMaxJoints comes from Renderer3D.h; mirror it here so the cbuffer
// stays sized correctly. 64 joints * 64 bytes = 4 KiB, well under the
// 64 KiB D3D11 cbuffer cap.
struct SkinCB {
    float jointMatrices[kMaxJoints * 16];
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
    float4   cameraPosWorld;
};

cbuffer DrawCB : register(b1) {
    float4x4 model;
    float4   albedoFactor;
    float4   mrParams;        // .x metallic, .y roughness
    float4   emissiveFactor;  // .rgb emissive tint
};

struct VSIn {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float4 tangent  : TANGENT;  // xyz = tangent dir, w = bitangent sign
};

struct VSOut {
    float4 position      : SV_POSITION;
    float3 worldPos      : POSITION0;
    float3 worldNormal   : NORMAL;
    float3 viewPos       : TEXCOORD0;
    float2 uv            : TEXCOORD1;
    float4 lightSpacePos : TEXCOORD2;
    // Tangent travels into the pixel shader so we can rebuild the TBN.
    // xyz = world-space tangent direction, w = bitangent sign carried
    // straight from glTF.
    float4 worldTangent  : TEXCOORD3;
};

VSOut VSMain(VSIn input) {
    VSOut output;
    float4 wp = mul(model, float4(input.position, 1.0));
    output.position      = mul(viewProj, wp);
    output.worldPos      = wp.xyz;
    output.viewPos       = mul(view, wp).xyz;
    output.lightSpacePos = mul(lightViewProj, wp);
    // Upper 3x3 of model rotates / scales the normal. Assumes uniform
    // scale; a non-uniform scale would need the inverse-transpose.
    output.worldNormal   = mul((float3x3)model, input.normal);
    output.worldTangent  = float4(mul((float3x3)model, input.tangent.xyz),
                                  input.tangent.w);
    output.uv            = input.uv;
    return output;
}

Texture2D    albedoTex   : register(t0);
SamplerState sampLinear  : register(s0);
// Shadow map is a depth texture (R24_UNORM_X8 SRV in the D3D11 backend)
// so the .r channel holds the depth value directly. Sample with the
// default linear sampler -- PCF / comparison sampler is a later upgrade.
Texture2D    shadowMap   : register(t1);
// glTF pbrMetallicRoughness packs roughness in .g and metallic in .b
// (R is unused; A is occlusion in some pipelines but we ignore it).
// emissiveTex is treated as plain RGB; alpha is unused.
Texture2D    mrTex       : register(t2);
Texture2D    emissiveTex : register(t3);
// Tangent-space normal map (glTF normalTexture). RGB is the tangent
// normal encoded into [0, 1] (xyz * 0.5 + 0.5). A null bind on the C++
// side falls back to a 2x2 (128, 128, 255) texture so the decoded
// normal is (0, 0, 1) and the perturbation is a no-op.
Texture2D    normalTex   : register(t4);
// Equirectangular sky env map used as the diffuse-IBL source. Renderer
// always binds it (the bake lives inside Renderer3D); sample it with
// the (perturbed) world normal turned into (u, v) by atan2/acos.
Texture2D    skyEnvTex   : register(t5);

// Wrap a unit vector to equirectangular UVs. u = azimuth around +Y,
// v = elevation from +Y (0 = straight up, 1 = straight down).
float2 DirToEquirect(float3 dir) {
    float u = atan2(dir.x, dir.z) / (2.0 * 3.14159265) + 0.5;
    float v = acos(clamp(dir.y, -1.0, 1.0)) / 3.14159265;
    return float2(u, v);
}

// 5x5 box PCF: 25 depth taps around the projected UV, averaged. Taps
// are spaced two texels apart, giving a ~10-texel fringe in shadow-map
// space (so with kShadowMapSize = 2048 and a shadow radius of 6 units,
// roughly 6 cm of feathering in world space). Loops are static-bounded
// so HLSL unrolls; per-pixel cost is one Sample per tap and stays in
// the noise compared to the BRDF math.
float SampleShadow(float4 lightSpacePos) {
    float3 lsp = lightSpacePos.xyz / lightSpacePos.w;
    float2 uv = float2(lsp.x * 0.5 + 0.5, 1.0 - (lsp.y * 0.5 + 0.5));
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
    if (lsp.z < 0.0 || lsp.z > 1.0) return 1.0;

    const float bias       = 0.0015;
    // Must match kShadowMapSize on the C++ side (2048). Hardcoded so the
    // hot loop avoids a cbuffer read per tap. The 2x spacing widens the
    // softness without doubling the tap count.
    const float texelSize  = 1.0 / 2048.0;
    const float tapSpacing = texelSize * 2.0;

    float sum = 0.0;
    [unroll] for (int y = -2; y <= 2; ++y) {
        [unroll] for (int x = -2; x <= 2; ++x) {
            float2 off = float2(x, y) * tapSpacing;
            float  closest = shadowMap.Sample(sampLinear, uv + off).r;
            sum += (lsp.z - bias > closest) ? 0.0 : 1.0;
        }
    }
    return sum / 25.0;
}

// --- Cook-Torrance GGX -----------------------------------------------
//
// D - GGX normal distribution. `a` is the squared roughness so the
//     visual brightness curve runs roughly perceptual.
float D_GGX(float NdotH, float a) {
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * denom * denom + 1e-6);
}

// F - Schlick's Fresnel approximation. F0 is the reflectance at normal
//     incidence; for dielectrics ~0.04, for metals = albedo.
float3 F_Schlick(float VdotH, float3 F0) {
    float f = pow(1.0 - VdotH, 5.0);
    return F0 + (1.0 - F0) * f;
}

// G - Smith geometry term with Schlick-GGX. Same `a` as above; the
//     direct-lighting k = (a+1)^2 / 8 rolls roughness into a self-
//     shadowing factor.
float G_Smith(float NdotV, float NdotL, float a) {
    float k   = (a + 1.0) * (a + 1.0) * 0.125;
    float gV  = NdotV / (NdotV * (1.0 - k) + k);
    float gL  = NdotL / (NdotL * (1.0 - k) + k);
    return gV * gL;
}

// Direct lighting contribution for one light direction. Returns the
// (diffuse + specular) reflected radiance modulated by NdotL.
float3 BRDF(float3 N, float3 V, float3 L,
            float3 albedo, float metallic, float roughness) {
    float3 H = normalize(V + L);
    float  NdotL = saturate(dot(N, L));
    float  NdotV = saturate(dot(N, V));
    float  NdotH = saturate(dot(N, H));
    float  VdotH = saturate(dot(V, H));

    // F0 = 4% reflectance for non-metals, albedo for metals.
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float  a  = roughness * roughness;

    float  D = D_GGX(NdotH, a);
    float3 F = F_Schlick(VdotH, F0);
    float  G = G_Smith(NdotV, NdotL, a);

    float3 spec = (D * F * G) / max(4.0 * NdotV * NdotL, 1e-4);

    // Energy conservation: kd is what isn't reflected as specular, and
    // metals don't have a diffuse term at all (the F term absorbs it).
    float3 kd = (float3(1.0, 1.0, 1.0) - F) * (1.0 - metallic);
    float3 diff = kd * albedo / 3.14159265;

    return (diff + spec) * NdotL;
}

float4 PSMain(VSOut input) : SV_TARGET {
    // Build the TBN from the interpolated normal + tangent and use it
    // to bring the tangent-space normal sample into world space. The
    // flat-normal fallback texture (128, 128, 255) decodes to (0, 0, 1)
    // so meshes without a real normal map come out perturbed by N
    // alone -- exactly as if normal mapping were off.
    float3 Ng = normalize(input.worldNormal);
    float3 T  = normalize(input.worldTangent.xyz);
    float3 B  = cross(Ng, T) * input.worldTangent.w;
    float3 nSample = normalTex.Sample(sampLinear, input.uv).rgb * 2.0 - 1.0;
    float3 N  = normalize(nSample.x * T + nSample.y * B + nSample.z * Ng);

    float3 V = normalize(cameraPosWorld.xyz - input.worldPos);
    // lightDirWorld points FROM source TOWARD surface; flip to get the
    // vector from the surface back to the source.
    float3 L = normalize(-lightDirWorld.xyz);

    float4 base = albedoTex.Sample(sampLinear, input.uv) * albedoFactor;
    // Sample metallic-roughness texture per glTF spec (G=roughness,
    // B=metallic); the factors multiply against the sampled values so a
    // 1x1 white fallback texture leaves the factor alone.
    float4 mrSample = mrTex.Sample(sampLinear, input.uv);
    float  metallic  = saturate(mrParams.x * mrSample.b);
    float  roughness = clamp(mrParams.y * mrSample.g, 0.04, 1.0);

    float  shadow = SampleShadow(input.lightSpacePos);
    float3 direct = BRDF(N, V, L, base.rgb, metallic, roughness) * shadow;

    // Diffuse IBL: sample the baked sky in the direction of the (perturbed)
    // normal. Not a proper cosine-weighted irradiance integral -- a single
    // tap is the MVP -- but the smooth zenith / horizon / ground gradient
    // gives surfaces the right ambient tint based on where they're looking.
    // Energy split between diffuse and reflected light: kd = (1-F)*(1-metal),
    // F via Schlick at NdotV (no BRDF LUT yet; specular IBL is a follow-up).
    float3 envDiffuse = skyEnvTex.Sample(sampLinear, DirToEquirect(N)).rgb;
    float3 F0    = lerp(float3(0.04, 0.04, 0.04), base.rgb, metallic);
    float  NdV   = saturate(dot(N, V));
    float3 Fibl  = F_Schlick(NdV, F0);
    float3 kdIbl = (float3(1.0, 1.0, 1.0) - Fibl) * (1.0 - metallic);
    float3 ambient = kdIbl * base.rgb * envDiffuse;

    // Emissive is unshadowed self-emission added on top of the BRDF.
    float3 emissive = emissiveTex.Sample(sampLinear, input.uv).rgb
                    * emissiveFactor.rgb;

    float3 lit = ambient + direct + emissive;

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

// --- Skinned variants ------------------------------------------------
//
// The skin matrices come in cbuffer b2 as an array of float4x4. The
// vertex carries up to four joint influences as float indices (cast to
// int) + four weights that should sum to 1. The blend is the standard
// linear-blend skinning sum: SkinM = sum(W[i] * jointMatrices[Idx[i]]).
cbuffer SkinCB : register(b2) {
    row_major float4x4 jointMatrices[64];
};

struct VSInSkin {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    float4 tangent  : TANGENT;
    float4 jIdx     : JOINTINDEX;
    float4 jWeight  : JOINTWEIGHT;
};

float4x4 BlendSkin(VSInSkin input) {
    int4 idx = int4(input.jIdx);
    float4x4 skin =
        jointMatrices[idx.x] * input.jWeight.x +
        jointMatrices[idx.y] * input.jWeight.y +
        jointMatrices[idx.z] * input.jWeight.z +
        jointMatrices[idx.w] * input.jWeight.w;
    return skin;
}

VSOut VSMainSkin(VSInSkin input) {
    float4x4 skin = BlendSkin(input);
    float4 lp = mul(skin, float4(input.position, 1.0));
    float4 wp = mul(model, lp);
    VSOut output;
    output.position      = mul(viewProj, wp);
    output.worldPos      = wp.xyz;
    output.viewPos       = mul(view, wp).xyz;
    output.lightSpacePos = mul(lightViewProj, wp);
    // Skinning rotates the normal + tangent too; assume rigid joints (no
    // per-vertex shear), so the upper 3x3 of the blended skin matrix
    // rotates both cleanly. Bitangent sign (tangent.w) is invariant
    // under rotation, so just carry it through.
    float3 skinnedNormal  = mul((float3x3)skin, input.normal);
    float3 skinnedTangent = mul((float3x3)skin, input.tangent.xyz);
    output.worldNormal    = mul((float3x3)model, skinnedNormal);
    output.worldTangent   = float4(mul((float3x3)model, skinnedTangent),
                                   input.tangent.w);
    output.uv             = input.uv;
    return output;
}

float4 VSShadowSkin(VSInSkin input) : SV_POSITION {
    float4x4 skin = BlendSkin(input);
    float4 wp = mul(model, mul(skin, float4(input.position, 1.0)));
    return mul(lightViewProj, wp);
}
)HLSL";

// Compile-time sanity: HLSL hard-codes 64 above; keep the C++ side in
// sync.
static_assert(kMaxJoints == 64, "Update HLSL jointMatrices[N] to match");

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
    layout.attributes.push_back({"TANGENT",  0, VertexFormat::Float4,
                                 uint32_t(offsetof(Vertex3D, tangent))});

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

    // Skinned shaders share the source -- only the entry point differs.
    ShaderDesc sdMainSkin;
    sdMainSkin.vertexSource = kShaderSrc;
    sdMainSkin.pixelSource  = kShaderSrc;
    sdMainSkin.vertexEntry  = "VSMainSkin";
    sdMainSkin.pixelEntry   = "PSMain";
    sdMainSkin.debugName    = "Renderer3D.MainSkin";
    m_shaderMainSkin = device.CreateShader(sdMainSkin);

    ShaderDesc sdShadowSkin;
    sdShadowSkin.vertexSource = kShaderSrc;
    sdShadowSkin.pixelSource  = kShaderSrc;
    sdShadowSkin.vertexEntry  = "VSShadowSkin";
    sdShadowSkin.pixelEntry   = "PSShadow";
    sdShadowSkin.debugName    = "Renderer3D.ShadowSkin";
    m_shaderShadowSkin = device.CreateShader(sdShadowSkin);

    VertexLayout skinLayout;
    skinLayout.stride = sizeof(Vertex3DSkin);
    skinLayout.attributes.push_back({"POSITION", 0, VertexFormat::Float3, 0});
    skinLayout.attributes.push_back({"NORMAL",   0, VertexFormat::Float3,
                                     uint32_t(offsetof(Vertex3DSkin, normal))});
    skinLayout.attributes.push_back({"TEXCOORD", 0, VertexFormat::Float2,
                                     uint32_t(offsetof(Vertex3DSkin, uv))});
    skinLayout.attributes.push_back({"TANGENT",  0, VertexFormat::Float4,
                                     uint32_t(offsetof(Vertex3DSkin, tangent))});
    skinLayout.attributes.push_back({"JOINTINDEX",  0, VertexFormat::Float4,
                                     uint32_t(offsetof(Vertex3DSkin, jointIndices))});
    skinLayout.attributes.push_back({"JOINTWEIGHT", 0, VertexFormat::Float4,
                                     uint32_t(offsetof(Vertex3DSkin, jointWeights))});

    PipelineDesc pdMainSkin = pdMain;
    pdMainSkin.shader       = m_shaderMainSkin.get();
    pdMainSkin.vertexLayout = skinLayout;
    m_pipelineMainSkin = device.CreatePipeline(pdMainSkin);

    PipelineDesc pdShadowSkin = pdShadow;
    pdShadowSkin.shader       = m_shaderShadowSkin.get();
    pdShadowSkin.vertexLayout = skinLayout;
    m_pipelineShadowSkin = device.CreatePipeline(pdShadowSkin);

    BufferDesc skb;
    skb.type  = BufferType::Uniform;
    skb.usage = BufferUsage::Dynamic;
    skb.size  = sizeof(SkinCB);
    m_skinUB = device.CreateBuffer(skb);

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

    // 2x2 flat normal map. RGB (128, 128, 255) decodes in the shader to
    // tangent normal (0, 0, 1) -- straight out of the surface -- so a
    // mesh without a real normal map renders as if normal mapping were
    // off, with no shader branch.
    const uint8_t flatNormalPixels[16] = {
        0x80, 0x80, 0xFF, 0xFF,  0x80, 0x80, 0xFF, 0xFF,
        0x80, 0x80, 0xFF, 0xFF,  0x80, 0x80, 0xFF, 0xFF,
    };
    TextureDesc nd;
    nd.width  = 2;
    nd.height = 2;
    nd.format = TextureFormat::RGBA8;
    nd.filter = TextureFilter::Point;
    nd.pixels = flatNormalPixels;
    m_flatNormalTex = device.CreateTexture(nd);

    // Bake the diffuse-IBL sky env: a 256x128 equirectangular gradient
    // running from a cool zenith at v=0 through a neutral horizon at
    // v=0.5 to a dark warm earth at v=1. Sampled in the pixel shader by
    // the (perturbed) world normal to give each surface point an ambient
    // colour that follows where it's looking instead of the old two-
    // colour hemi lerp. Colours match the look the cube test scene was
    // already using so this drops in without retuning anything else.
    constexpr int kSkyW = 256;
    constexpr int kSkyH = 128;
    const Vec3 cZenith   = {0.30f, 0.42f, 0.55f};  // cool blue overhead
    const Vec3 cHorizUp  = {0.55f, 0.55f, 0.55f};  // neutral at horizon (above)
    const Vec3 cHorizDn  = {0.50f, 0.42f, 0.35f};  // warm horizon (below)
    const Vec3 cGround   = {0.16f, 0.13f, 0.10f};  // dark earth straight down
    auto lerp3 = [](const Vec3& a, const Vec3& b, float t) {
        return Vec3{a.x + (b.x - a.x) * t,
                    a.y + (b.y - a.y) * t,
                    a.z + (b.z - a.z) * t};
    };
    auto smooth = [](float t) { return t * t * (3.0f - 2.0f * t); };
    std::vector<uint8_t> skyPixels(size_t(kSkyW) * size_t(kSkyH) * 4);
    for (int y = 0; y < kSkyH; ++y) {
        const float v = float(y) / float(kSkyH - 1);  // 0 = top, 1 = bottom
        Vec3 col;
        if (v < 0.5f) {
            col = lerp3(cZenith, cHorizUp, smooth(v * 2.0f));
        } else {
            col = lerp3(cHorizDn, cGround, smooth((v - 0.5f) * 2.0f));
        }
        const uint8_t r = uint8_t(std::clamp(col.x, 0.0f, 1.0f) * 255.0f);
        const uint8_t g = uint8_t(std::clamp(col.y, 0.0f, 1.0f) * 255.0f);
        const uint8_t b = uint8_t(std::clamp(col.z, 0.0f, 1.0f) * 255.0f);
        for (int x = 0; x < kSkyW; ++x) {
            const size_t i = (size_t(y) * kSkyW + size_t(x)) * 4;
            skyPixels[i + 0] = r;
            skyPixels[i + 1] = g;
            skyPixels[i + 2] = b;
            skyPixels[i + 3] = 0xFF;
        }
    }
    TextureDesc sky;
    sky.width  = kSkyW;
    sky.height = kSkyH;
    sky.format = TextureFormat::RGBA8;
    sky.filter = TextureFilter::Linear;
    sky.pixels = skyPixels.data();
    m_skyEnvTex = device.CreateTexture(sky);

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
    m_skinQueue.clear();
    m_skinPool.clear();
    m_shadowMap.reset();
    m_skyEnvTex.reset();
    m_flatNormalTex.reset();
    m_whiteTex.reset();
    m_skinUB.reset();
    m_drawUB.reset();
    m_frameUB.reset();
    m_pipelineShadowSkin.reset();
    m_pipelineMainSkin.reset();
    m_pipelineShadow.reset();
    m_pipelineMain.reset();
    m_shaderShadowSkin.reset();
    m_shaderMainSkin.reset();
    m_shaderShadow.reset();
    m_shaderMain.reset();
    m_device = nullptr;
}

void Renderer3D::Begin(const Mat4& view, const Mat4& projection,
                       const DirectionalLight& light,
                       const FogParams& fog,
                       RHITexture* sceneColor,
                       RHITexture* sceneDepth) {
    if (!m_device || !m_pipelineMain || !m_frameUB) return;
    if (m_inPass) {
        NARAGAME_WARN("Renderer3D::Begin called without matching End");
    }
    m_inPass = true;
    m_queue.clear();
    m_skinQueue.clear();
    m_skinPool.clear();
    m_sceneColor = sceneColor;
    m_sceneDepth = sceneDepth;

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

    // Recover the camera world position by inverting the view matrix's
    // translation column. View = R | t with R the rotated basis and t
    // the translated camera; camera world pos = -R^T * t. The engine's
    // LookAt builds this exactly so we can read the row-major data
    // directly: cameraPos = -(view.At(0..2, 3) * basis^T).
    // For an RH view: camera = -view^T * view.At(0..2, 3) won't compile
    // here; reconstruct by hand from the LookAt formula.
    // (The view's [0..2][3] hold -dot(basis, eye), so applying basis^T
    // to that vector negated gives back eye.)
    const Vec3 camPos{
        -(view.At(0, 0) * view.At(0, 3) + view.At(1, 0) * view.At(1, 3) + view.At(2, 0) * view.At(2, 3)),
        -(view.At(0, 1) * view.At(0, 3) + view.At(1, 1) * view.At(1, 3) + view.At(2, 1) * view.At(2, 3)),
        -(view.At(0, 2) * view.At(0, 3) + view.At(1, 2) * view.At(1, 3) + view.At(2, 2) * view.At(2, 3)),
    };

    FrameCB cb;
    CopyMatrix(view,        cb.view);
    CopyMatrix(projection * view, cb.viewProj);
    CopyMatrix(lightVP,     cb.lightViewProj);
    cb.lightDirWorld[0]    = dir.x;
    cb.lightDirWorld[1]    = dir.y;
    cb.lightDirWorld[2]    = dir.z;
    cb.lightDirWorld[3]    = 0.0f;
    cb.skyAmbient[0]       = light.skyAmbient.r;
    cb.skyAmbient[1]       = light.skyAmbient.g;
    cb.skyAmbient[2]       = light.skyAmbient.b;
    cb.skyAmbient[3]       = 1.0f;
    cb.groundAmbient[0]    = light.groundAmbient.r;
    cb.groundAmbient[1]    = light.groundAmbient.g;
    cb.groundAmbient[2]    = light.groundAmbient.b;
    cb.groundAmbient[3]    = 1.0f;
    cb.fogColor[0]         = fog.color.r;
    cb.fogColor[1]         = fog.color.g;
    cb.fogColor[2]         = fog.color.b;
    cb.fogColor[3]         = 1.0f;
    cb.fogParams[0]        = fog.startDistance;
    cb.fogParams[1]        = fog.endDistance;
    cb.fogParams[2]        = 0.0f;
    cb.fogParams[3]        = 0.0f;
    cb.cameraPosWorld[0]   = camPos.x;
    cb.cameraPosWorld[1]   = camPos.y;
    cb.cameraPosWorld[2]   = camPos.z;
    cb.cameraPosWorld[3]   = 0.0f;
    m_frameUB->Update(&cb, sizeof(cb));
}

void Renderer3D::DrawSkinnedMesh(RHIBuffer* vb, RHIBuffer* ib,
                                 uint32_t indexCount,
                                 IndexFormat indexFormat,
                                 const Mat4& model,
                                 const Mat4* jointMatrices, int jointCount,
                                 const Material& material) {
    if (!m_device || !vb || !ib || indexCount == 0) return;
    if (!m_inPass) {
        NARAGAME_WARN("Renderer3D::DrawSkinnedMesh called outside Begin/End");
        return;
    }
    if (jointCount <= 0 || jointMatrices == nullptr) {
        NARAGAME_WARN("Renderer3D::DrawSkinnedMesh requires at least one joint");
        return;
    }
    if (jointCount > kMaxJoints) {
        NARAGAME_WARN("Renderer3D::DrawSkinnedMesh: %d joints > max %d, clamping",
                      jointCount, kMaxJoints);
        jointCount = kMaxJoints;
    }
    QueuedSkinned q;
    q.vb          = vb;
    q.ib          = ib;
    q.indexCount  = indexCount;
    q.indexFormat = indexFormat;
    q.model       = model;
    q.material    = material;
    q.jointStart  = int(m_skinPool.size());
    q.jointCount  = jointCount;
    m_skinPool.insert(m_skinPool.end(), jointMatrices,
                      jointMatrices + jointCount);
    m_skinQueue.push_back(q);
}

void Renderer3D::DrawMesh(RHIBuffer* vb, RHIBuffer* ib, uint32_t indexCount,
                          IndexFormat indexFormat,
                          const Mat4& model,
                          const Material& material) {
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
    q.material    = material;
    m_queue.push_back(q);
}

void Renderer3D::End() {
    if (!m_device || !m_pipelineMain) {
        m_inPass = false;
        m_queue.clear();
        m_skinQueue.clear();
        m_skinPool.clear();
        return;
    }
    if (m_queue.empty() && m_skinQueue.empty()) {
        m_inPass = false;
        return;
    }
    // The light-space matrix already lives in m_frameUB; just need to
    // run the two passes against the same FrameCB.
    RunShadowPass({});  // matrix arg unused; just kept for clarity if we
                        // later split frame data per pass
    RunMainPass();
    m_queue.clear();
    m_skinQueue.clear();
    m_skinPool.clear();
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

    auto packDraw = [](const QueuedMesh& q, DrawCB& out) {
        CopyMatrix(q.model, out.model);
        out.albedoFactor[0]   = q.material.albedoFactor.r;
        out.albedoFactor[1]   = q.material.albedoFactor.g;
        out.albedoFactor[2]   = q.material.albedoFactor.b;
        out.albedoFactor[3]   = q.material.albedoFactor.a;
        out.mrParams[0]       = q.material.metallic;
        out.mrParams[1]       = q.material.roughness;
        out.mrParams[2]       = 0.0f;
        out.mrParams[3]       = 0.0f;
        out.emissiveFactor[0] = q.material.emissiveFactor.r;
        out.emissiveFactor[1] = q.material.emissiveFactor.g;
        out.emissiveFactor[2] = q.material.emissiveFactor.b;
        out.emissiveFactor[3] = 0.0f;
    };

    DrawCB cb;
    for (const QueuedMesh& q : m_queue) {
        packDraw(q, cb);
        m_drawUB->Update(&cb, sizeof(cb));
        m_device->BindUniformBuffer(1, m_drawUB.get());
        m_device->BindVertexBuffer(q.vb, sizeof(Vertex3D), 0);
        m_device->BindIndexBuffer(q.ib, q.indexFormat);
        m_device->DrawIndexed(q.indexCount, 0);
    }

    // Skinned shadow pass: swap PSO + upload SkinCB per mesh. SkinCB is
    // sized for kMaxJoints; copy in what the caller supplied and leave
    // the rest as zero-filled (the shader only indexes the joints it
    // needs).
    if (!m_skinQueue.empty()) {
        m_device->BindPipeline(m_pipelineShadowSkin.get());
        m_device->BindUniformBuffer(0, m_frameUB.get());
        SkinCB skinCB{};
        for (const QueuedSkinned& q : m_skinQueue) {
            DrawCB dcb;
            QueuedMesh stub;
            stub.model    = q.model;
            stub.material = q.material;
            packDraw(stub, dcb);
            m_drawUB->Update(&dcb, sizeof(dcb));
            for (int j = 0; j < q.jointCount; ++j) {
                CopyMatrix(m_skinPool[size_t(q.jointStart + j)],
                           &skinCB.jointMatrices[j * 16]);
            }
            // Fill remaining joints with identity so a stray index out
            // of [0, jointCount) still produces a finite vertex.
            for (int j = q.jointCount; j < kMaxJoints; ++j) {
                Mat4 id;
                CopyMatrix(id, &skinCB.jointMatrices[j * 16]);
            }
            m_skinUB->Update(&skinCB, sizeof(skinCB));
            m_device->BindUniformBuffer(1, m_drawUB.get());
            m_device->BindUniformBuffer(2, m_skinUB.get());
            m_device->BindVertexBuffer(q.vb, sizeof(Vertex3DSkin), 0);
            m_device->BindIndexBuffer(q.ib, q.indexFormat);
            m_device->DrawIndexed(q.indexCount, 0);
        }
    }
}

void Renderer3D::RunMainPass() {
    // Route the main pass back to whatever the caller staged in Begin.
    // The shadow pass swapped to the shadow map; without restoring
    // here the main draws would silently land on the shadow texture.
    // Clear / ClearDepth on the target is the caller's responsibility
    // (we don't want to wipe e.g. a 2D HUD that was drawn first).
    if (m_sceneColor || m_sceneDepth) {
        m_device->SetRenderTargets(m_sceneColor, m_sceneDepth);
    } else {
        m_device->SetRenderTarget(nullptr);
    }

    m_device->BindPipeline(m_pipelineMain.get());
    m_device->BindUniformBuffer(0, m_frameUB.get());
    // Shadow map sits at t1 for the whole main pass; per-mesh material
    // textures land at t0 (baseColor), t2 (metallic-roughness), and t3
    // (emissive) below. Null pointers fall back to the 2x2 white texture
    // so the shader's sample-then-multiply path is always valid. The IBL
    // sky env at t5 is renderer-owned and stays bound for every draw.
    m_device->BindTexture(1, m_shadowMap.get());
    m_device->BindTexture(5, m_skyEnvTex.get());

    auto packDraw = [](const QueuedMesh& q, DrawCB& out) {
        CopyMatrix(q.model, out.model);
        out.albedoFactor[0]   = q.material.albedoFactor.r;
        out.albedoFactor[1]   = q.material.albedoFactor.g;
        out.albedoFactor[2]   = q.material.albedoFactor.b;
        out.albedoFactor[3]   = q.material.albedoFactor.a;
        out.mrParams[0]       = q.material.metallic;
        out.mrParams[1]       = q.material.roughness;
        out.mrParams[2]       = 0.0f;
        out.mrParams[3]       = 0.0f;
        out.emissiveFactor[0] = q.material.emissiveFactor.r;
        out.emissiveFactor[1] = q.material.emissiveFactor.g;
        out.emissiveFactor[2] = q.material.emissiveFactor.b;
        out.emissiveFactor[3] = 0.0f;
    };

    RHITexture* whiteFallback  = m_whiteTex.get();
    RHITexture* normalFallback = m_flatNormalTex.get();
    auto bindTex = [this, whiteFallback](int slot, RHITexture* t) {
        m_device->BindTexture(slot, t ? t : whiteFallback);
    };
    auto bindNormal = [this, normalFallback](RHITexture* t) {
        m_device->BindTexture(4, t ? t : normalFallback);
    };

    DrawCB cb;
    for (const QueuedMesh& q : m_queue) {
        packDraw(q, cb);
        m_drawUB->Update(&cb, sizeof(cb));
        m_device->BindUniformBuffer(1, m_drawUB.get());
        bindTex(0, q.material.baseColorTex);
        bindTex(2, q.material.metallicRoughnessTex);
        bindTex(3, q.material.emissiveTex);
        bindNormal(q.material.normalTex);
        m_device->BindVertexBuffer(q.vb, sizeof(Vertex3D), 0);
        m_device->BindIndexBuffer(q.ib, q.indexFormat);
        m_device->DrawIndexed(q.indexCount, 0);
    }

    // Skinned main pass: swap to the skin PSO + upload SkinCB. Shadow
    // map stays bound at t1 from the static draws above.
    if (!m_skinQueue.empty()) {
        m_device->BindPipeline(m_pipelineMainSkin.get());
        m_device->BindUniformBuffer(0, m_frameUB.get());
        m_device->BindTexture(1, m_shadowMap.get());
        m_device->BindTexture(5, m_skyEnvTex.get());
        SkinCB skinCB{};
        for (const QueuedSkinned& q : m_skinQueue) {
            DrawCB dcb;
            QueuedMesh stub;
            stub.model    = q.model;
            stub.material = q.material;
            packDraw(stub, dcb);
            m_drawUB->Update(&dcb, sizeof(dcb));
            for (int j = 0; j < q.jointCount; ++j) {
                CopyMatrix(m_skinPool[size_t(q.jointStart + j)],
                           &skinCB.jointMatrices[j * 16]);
            }
            for (int j = q.jointCount; j < kMaxJoints; ++j) {
                Mat4 id;
                CopyMatrix(id, &skinCB.jointMatrices[j * 16]);
            }
            m_skinUB->Update(&skinCB, sizeof(skinCB));
            m_device->BindUniformBuffer(1, m_drawUB.get());
            m_device->BindUniformBuffer(2, m_skinUB.get());
            bindTex(0, q.material.baseColorTex);
            bindTex(2, q.material.metallicRoughnessTex);
            bindTex(3, q.material.emissiveTex);
            bindNormal(q.material.normalTex);
            m_device->BindVertexBuffer(q.vb, sizeof(Vertex3DSkin), 0);
            m_device->BindIndexBuffer(q.ib, q.indexFormat);
            m_device->DrawIndexed(q.indexCount, 0);
        }
    }
}

} // namespace NaraGame
