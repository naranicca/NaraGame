#include "NaraGame/renderer/Renderer3D.h"

#include "NaraGame/assets/Hdr.h"
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

// Cascaded shadow maps: three concentric cascades centred on the
// DirectionalLight's shadowCenter, each rendered to its own 1024x1024
// depth texture. Per-fragment cascade selection in the pixel shader
// picks the smallest cascade whose ortho fit still contains the
// fragment. Smaller cascades give crisper shadows on nearby geometry;
// the far cascade catches distant casters without blowing up resolution
// uniformly across the whole scene.
constexpr int      kCascadeCount       = 3;
constexpr uint32_t kCascadeShadowSize  = 1024;

// CB layout matches HLSL exactly. Default cbuffer matrix layout in HLSL
// is column_major which matches the engine's Mat4 storage
// (data[col*4 + row]), so we just memcpy and call mul(M, v) for M * v.
struct FrameCB {
    float view[16];
    float viewProj[16];
    float lightViewProj[kCascadeCount * 16];
    float lightDirWorld[4];   // .xyz = normalized direction, .w unused
    float skyAmbient[4];      // .rgb top of hemisphere (unused since IBL)
    float groundAmbient[4];   // .rgb bottom of hemisphere (unused since IBL)
    float fogColor[4];        // .rgb
    float fogParams[4];       // .x = start, .y = end, .zw unused
    float cameraPosWorld[4];  // .xyz = camera world position
    float shadowCenter[4];    // .xyz = world-space cascade centre
    float cascadeRadii[4];    // .x/.y/.z = ortho radii per cascade
};
struct DrawCB {
    float model[16];
    float albedoFactor[4];   // .rgba multiplied against albedo sample
    float mrParams[4];       // .x metallic .y roughness .z cascadeIdx (shadow pass) .w unused
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
    float4x4 lightViewProj[3];   // one per cascade
    float4   lightDirWorld;
    float4   skyAmbient;
    float4   groundAmbient;
    float4   fogColor;
    float4   fogParams;          // .x start, .y end
    float4   cameraPosWorld;
    float4   shadowCenter;       // .xyz = world-space cascade centre
    float4   cascadeRadii;       // .xyz = ortho radii per cascade
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
    // Tangent travels into the pixel shader so we can rebuild the TBN.
    // xyz = world-space tangent direction, w = bitangent sign carried
    // straight from glTF.
    float4 worldTangent  : TEXCOORD2;
};

VSOut VSMain(VSIn input) {
    VSOut output;
    float4 wp = mul(model, float4(input.position, 1.0));
    output.position      = mul(viewProj, wp);
    output.worldPos      = wp.xyz;
    output.viewPos       = mul(view, wp).xyz;
    // Upper 3x3 of model rotates / scales the normal. Assumes uniform
    // scale; a non-uniform scale would need the inverse-transpose.
    output.worldNormal   = mul((float3x3)model, input.normal);
    output.worldTangent  = float4(mul((float3x3)model, input.tangent.xyz),
                                  input.tangent.w);
    output.uv            = input.uv;
    return output;
}

Texture2D    albedoTex      : register(t0);
SamplerState sampLinear     : register(s0);
// Cascaded shadow maps -- one Texture2D per cascade (R24_UNORM_X8 SRV
// on the D3D11 backend) so the .r channel holds the depth value
// directly. The pixel shader picks a cascade by world distance from
// shadowCenter and only samples that cascade's map.
Texture2D    shadowMap0     : register(t1);
Texture2D    shadowMap1     : register(t8);
Texture2D    shadowMap2     : register(t9);
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
Texture2D    skyEnvTex          : register(t5);
// Prefiltered env atlas at t6: six 256x128 strips stacked vertically,
// each pre-convolved with a GGX lobe at roughness = strip/5. Sampled
// twice and lerped by frac(roughness * 5) for a smooth roughness
// response.
Texture2D    prefilteredEnvTex  : register(t6);
// BRDF LUT at t7: R = scale, G = bias. Indexed by (NdotV, roughness).
Texture2D    brdfLutTex         : register(t7);

// Wrap a unit vector to equirectangular UVs. u = azimuth around +Y,
// v = elevation from +Y (0 = straight up, 1 = straight down).
float2 DirToEquirect(float3 dir) {
    float u = atan2(dir.x, dir.z) / (2.0 * 3.14159265) + 0.5;
    float v = acos(clamp(dir.y, -1.0, 1.0)) / 3.14159265;
    return float2(u, v);
}

// Sample the prefiltered env atlas at the strip for `level` (0..5) and
// remap the equirectangular v into the strip's vertical slice.
float3 SamplePrefilteredStrip(float3 dir, int level) {
    float2 uv = DirToEquirect(dir);
    // Six 128-tall strips packed vertically -> each strip covers 1/6 of
    // the atlas in v, offset by level/6.
    uv.y = (float(level) + uv.y) * (1.0 / 6.0);
    return prefilteredEnvTex.SampleLevel(sampLinear, uv, 0).rgb;
}

// 5x5 box PCF in a cascade's shadow map. Templated over the texture by
// passing the Texture2D in directly (HLSL SM5+ supports texture function
// parameters), so the cascade branch only happens once outside the loop
// instead of 25 times. Tap spacing is 2 texels of the cascade's own
// 1024 size, giving each cascade a similarly-soft penumbra regardless
// of the world-space area it covers.
float PCF5x5(Texture2D tex, float2 uv, float refDepth) {
    const float bias       = 0.0015;
    const float texelSize  = 1.0 / 1024.0;
    const float tapSpacing = texelSize * 2.0;
    float sum = 0.0;
    [unroll] for (int y = -2; y <= 2; ++y) {
        [unroll] for (int x = -2; x <= 2; ++x) {
            float2 off = float2(x, y) * tapSpacing;
            float closest = tex.Sample(sampLinear, uv + off).r;
            sum += (refDepth - bias > closest) ? 0.0 : 1.0;
        }
    }
    return sum / 25.0;
}

// Project worldPos into the picked cascade and run PCF. Out-of-bounds
// UV or depth means "no shadow info here" -> return 1.0 (lit) so casters
// beyond the shadow region don't smear dark across the scene.
float SampleShadow(float3 worldPos) {
    // Pick the smallest cascade whose ortho fit still contains this
    // fragment. cascadeRadii is sorted (small -> large) on the C++ side.
    float3 fromCenter = worldPos - shadowCenter.xyz;
    float  dist = max(abs(fromCenter.x), max(abs(fromCenter.y), abs(fromCenter.z)));
    int cascade = 2;
    if (dist < cascadeRadii.x)      cascade = 0;
    else if (dist < cascadeRadii.y) cascade = 1;

    float4 lsp = mul(lightViewProj[cascade], float4(worldPos, 1.0));
    float3 lsp3 = lsp.xyz / lsp.w;
    float2 uv = float2(lsp3.x * 0.5 + 0.5, 1.0 - (lsp3.y * 0.5 + 0.5));
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return 1.0;
    if (lsp3.z < 0.0 || lsp3.z > 1.0) return 1.0;

    if (cascade == 0) return PCF5x5(shadowMap0, uv, lsp3.z);
    if (cascade == 1) return PCF5x5(shadowMap1, uv, lsp3.z);
    return PCF5x5(shadowMap2, uv, lsp3.z);
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

    float  shadow = SampleShadow(input.worldPos);
    float3 direct = BRDF(N, V, L, base.rgb, metallic, roughness) * shadow;

    // Image-based lighting -- Karis split-sum approximation.
    //
    // Diffuse: sample the baked sky in the (perturbed) normal direction.
    // A single tap, not a cosine-weighted irradiance integral, but the
    // smooth zenith/horizon/ground gradient still gives the right ambient
    // tint per surface point.
    //
    // Specular: bounce V off N to get the reflection direction, sample
    // the prefiltered env atlas at the strip selected by material
    // roughness (lerped between two adjacent strips for smoothness), and
    // weight by the BRDF LUT-integrated Fresnel + geometry term:
    //   specular = envSpec * (F * lut.r + lut.g)
    float3 envDiffuse = skyEnvTex.Sample(sampLinear, DirToEquirect(N)).rgb;
    float3 F0     = lerp(float3(0.04, 0.04, 0.04), base.rgb, metallic);
    float  NdV    = saturate(dot(N, V));
    float3 Fibl   = F_Schlick(NdV, F0);
    float3 kdIbl  = (float3(1.0, 1.0, 1.0) - Fibl) * (1.0 - metallic);
    float3 diffuseIbl = kdIbl * base.rgb * envDiffuse;

    float3 R       = reflect(-V, N);
    float  mipPos  = saturate(roughness) * 5.0;  // 6 strips: 0..5
    int    mipLo  = (int)floor(mipPos);
    int    mipHi  = min(mipLo + 1, 5);
    float  mipT   = mipPos - float(mipLo);
    float3 envLo  = SamplePrefilteredStrip(R, mipLo);
    float3 envHi  = SamplePrefilteredStrip(R, mipHi);
    float3 envSpec = lerp(envLo, envHi, mipT);
    float2 brdf   = brdfLutTex.Sample(sampLinear,
                                       float2(NdV, roughness)).rg;
    float3 specularIbl = envSpec * (Fibl * brdf.x + brdf.y);

    float3 ambient = diffuseIbl + specularIbl;

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
// The pixel shader is empty -- depth-only output, no color writes. The
// cascade index for this pass rides in DrawCB's mrParams.z; the renderer
// updates it per cascade before walking the queue.
float4 VSShadow(VSIn input) : SV_POSITION {
    float4 wp = mul(model, float4(input.position, 1.0));
    int cascade = (int)mrParams.z;
    return mul(lightViewProj[cascade], wp);
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
    int cascade = (int)mrParams.z;
    return mul(lightViewProj[cascade], wp);
}
)HLSL";

// Compile-time sanity: HLSL hard-codes 64 above; keep the C++ side in
// sync.
static_assert(kMaxJoints == 64, "Update HLSL jointMatrices[N] to match");

void CopyMatrix(const Mat4& src, float dst[16]) {
    for (int i = 0; i < 16; ++i) dst[i] = src.data[i];
}

// --- IBL bake helpers ----------------------------------------------------
//
// All run once during Renderer3D::Init from a CPU-side pixel buffer; the
// runtime never recomputes. Hammersley + GGX importance sampling matches
// the Karis "Real Shading in UE4" split-sum approach: the prefiltered env
// gets the GGX-weighted radiance integral, the BRDF LUT gets the geometry
// + Fresnel integral.

constexpr float kIblPi = 3.14159265358979323846f;

// Van der Corput radical inverse: reverse the bits of i, treat as fraction.
float RadicalInverseVdC(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;  // / 0x100000000
}

// 2D Hammersley sequence in [0, 1)^2 -- evenly distributed low-discrepancy
// samples. Drives the GGX importance sampling.
struct Vec2f { float x, y; };
Vec2f Hammersley(uint32_t i, uint32_t n) {
    return Vec2f{float(i) / float(n), RadicalInverseVdC(i)};
}

// GGX-weighted half-vector sample in the tangent frame whose +Z is `n`.
// Lower roughness narrows the lobe; alpha = roughness^2 per glTF / Disney.
Vec3 ImportanceSampleGGX(const Vec2f& xi, const Vec3& n, float roughness) {
    const float a = roughness * roughness;
    const float phi = 2.0f * kIblPi * xi.x;
    const float cosTheta = std::sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    // Tangent-space sample then rotate into the frame aligned with n.
    const Vec3 H_t = {sinTheta * std::cos(phi),
                     sinTheta * std::sin(phi),
                     cosTheta};
    const Vec3 up = (std::abs(n.z) < 0.999f) ? Vec3{0.0f, 0.0f, 1.0f}
                                            : Vec3{1.0f, 0.0f, 0.0f};
    const Vec3 T = {
        up.y * n.z - up.z * n.y,
        up.z * n.x - up.x * n.z,
        up.x * n.y - up.y * n.x,
    };
    const float tlen = std::sqrt(T.x * T.x + T.y * T.y + T.z * T.z);
    const Vec3 Tn = {T.x / tlen, T.y / tlen, T.z / tlen};
    const Vec3 B = {
        n.y * Tn.z - n.z * Tn.y,
        n.z * Tn.x - n.x * Tn.z,
        n.x * Tn.y - n.y * Tn.x,
    };
    return {Tn.x * H_t.x + B.x * H_t.y + n.x * H_t.z,
            Tn.y * H_t.x + B.y * H_t.y + n.y * H_t.z,
            Tn.z * H_t.x + B.z * H_t.y + n.z * H_t.z};
}

// IBL flavour of the Smith geometry term (k = a^2/2 vs (a+1)^2/8 in
// direct lighting). Pairs with the BRDF LUT bake below.
float GeometrySchlickGGX_IBL(float NdotV, float roughness) {
    const float a = roughness;
    const float k = (a * a) * 0.5f;
    return NdotV / (NdotV * (1.0f - k) + k);
}
float GeometrySmith_IBL(float NdotV, float NdotL, float roughness) {
    return GeometrySchlickGGX_IBL(NdotV, roughness) *
           GeometrySchlickGGX_IBL(NdotL, roughness);
}

// Convert a world-space direction to equirectangular (u, v) in [0,1].
// Mirrors the shader's DirToEquirect so the bake samples the same space
// the runtime does.
Vec2f DirToEquirectCpu(const Vec3& d) {
    const float u = std::atan2(d.x, d.z) / (2.0f * kIblPi) + 0.5f;
    const float dy = std::clamp(d.y, -1.0f, 1.0f);
    const float v = std::acos(dy) / kIblPi;
    return {u, v};
}

// Bilinear sample of an equirectangular float-RGB pixel buffer. Source
// is interleaved RGB (3 floats per pixel) of arbitrary size; the bake
// pipeline can feed either the procedural default or a loaded HDR file.
Vec3 SampleSkyBilinearFloat(const float* skyPixels, int w, int h,
                            const Vec3& dir) {
    const Vec2f uv = DirToEquirectCpu(dir);
    const float fx = uv.x * float(w);
    const float fy = uv.y * float(h - 1);
    int x0 = int(std::floor(fx)) % w;
    if (x0 < 0) x0 += w;
    int x1 = (x0 + 1) % w;
    int y0 = std::clamp(int(std::floor(fy)), 0, h - 1);
    int y1 = std::clamp(y0 + 1, 0, h - 1);
    const float tx = fx - std::floor(fx);
    const float ty = fy - std::floor(fy);
    auto get = [&](int x, int y) {
        const size_t i = (size_t(y) * w + size_t(x)) * 3;
        return Vec3{skyPixels[i + 0], skyPixels[i + 1], skyPixels[i + 2]};
    };
    const Vec3 c00 = get(x0, y0), c10 = get(x1, y0);
    const Vec3 c01 = get(x0, y1), c11 = get(x1, y1);
    const Vec3 a{c00.x + (c10.x - c00.x) * tx,
                c00.y + (c10.y - c00.y) * tx,
                c00.z + (c10.z - c00.z) * tx};
    const Vec3 b{c01.x + (c11.x - c01.x) * tx,
                c01.y + (c11.y - c01.y) * tx,
                c01.z + (c11.z - c01.z) * tx};
    return {a.x + (b.x - a.x) * ty,
            a.y + (b.y - a.y) * ty,
            a.z + (b.z - a.z) * ty};
}

// IEEE 754 binary32 -> binary16 conversion for RGBA16F texture upload.
// Subnormals + Inf collapse to nearest representable; mantissa truncates
// (faster than round-to-nearest and the IBL bake doesn't care about
// half-bit precision).
uint16_t FloatToHalf(float f) {
    union { float f; uint32_t u; } v;
    v.f = f;
    const uint32_t bits = v.u;
    const uint32_t sign = (bits >> 16) & 0x8000u;
    const int32_t  e    = int32_t((bits >> 23) & 0xFFu) - 127 + 15;
    const uint32_t m    = bits & 0x007FFFFFu;
    if (e >= 31) return uint16_t(sign | 0x7C00u | (m ? 0x0200u : 0u));
    if (e <= 0)  return uint16_t(sign);
    return uint16_t(sign | (uint32_t(e) << 10) | (m >> 13));
}

// Fill a 256x128 float-RGB buffer with the engine's default sky gradient
// -- cool zenith at the top through neutral horizon to dark warm earth.
// Lifted out of Init so LoadEnvironment can re-bake from the procedural
// source via the same code path that handles a loaded HDR file.
constexpr int kProcSkyW = 256;
constexpr int kProcSkyH = 128;
std::vector<float> MakeProceduralSkyPixels() {
    const Vec3 cZenith   = {0.30f, 0.42f, 0.55f};
    const Vec3 cHorizUp  = {0.55f, 0.55f, 0.55f};
    const Vec3 cHorizDn  = {0.50f, 0.42f, 0.35f};
    const Vec3 cGround   = {0.16f, 0.13f, 0.10f};
    auto lerp3 = [](const Vec3& a, const Vec3& b, float t) {
        return Vec3{a.x + (b.x - a.x) * t,
                    a.y + (b.y - a.y) * t,
                    a.z + (b.z - a.z) * t};
    };
    auto smooth = [](float t) { return t * t * (3.0f - 2.0f * t); };
    std::vector<float> px(size_t(kProcSkyW) * size_t(kProcSkyH) * 3);
    for (int y = 0; y < kProcSkyH; ++y) {
        const float v = float(y) / float(kProcSkyH - 1);
        const Vec3 col = (v < 0.5f)
            ? lerp3(cZenith,  cHorizUp, smooth(v * 2.0f))
            : lerp3(cHorizDn, cGround,  smooth((v - 0.5f) * 2.0f));
        for (int x = 0; x < kProcSkyW; ++x) {
            const size_t i = (size_t(y) * kProcSkyW + size_t(x)) * 3;
            px[i + 0] = col.x;
            px[i + 1] = col.y;
            px[i + 2] = col.z;
        }
    }
    return px;
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

    // Bake the IBL sky env + prefiltered atlas from the procedural sky
    // gradient. LoadEnvironment can later swap the source for a loaded
    // HDR file and re-bake both textures via the same code path.
    const std::vector<float> procSky = MakeProceduralSkyPixels();
    BakeIblFromSource(procSky.data(), kProcSkyW, kProcSkyH);

    // --- Specular IBL: BRDF LUT ----------------------------------------
    //
    // Karis split-sum geometry+Fresnel integral. 64x64 is plenty for the
    // smooth surface this function has (most graphics pipelines ship a
    // 256x256 baked offline). R = scale, G = bias; the runtime combines
    // them as F * scale + bias for the specular reflectance.
    constexpr int kLutW = 64;
    constexpr int kLutH = 64;
    constexpr int kLutSamples = 256;
    std::vector<uint8_t> lutPixels(size_t(kLutW) * size_t(kLutH) * 4);
    for (int y = 0; y < kLutH; ++y) {
        // Map y to roughness in (1/N, 1]; clamp away from zero so the
        // GGX denominator stays finite.
        const float roughness = std::max(0.04f,
            (float(y) + 0.5f) / float(kLutH));
        for (int x = 0; x < kLutW; ++x) {
            const float NdotV = std::max(0.001f,
                (float(x) + 0.5f) / float(kLutW));
            const Vec3 V = {std::sqrt(1.0f - NdotV * NdotV), 0.0f, NdotV};
            float scale = 0.0f;
            float bias  = 0.0f;
            const Vec3 N = {0.0f, 0.0f, 1.0f};
            for (int s = 0; s < kLutSamples; ++s) {
                const Vec2f xi = Hammersley(uint32_t(s),
                                            uint32_t(kLutSamples));
                const Vec3 H = ImportanceSampleGGX(xi, N, roughness);
                const float VdotH = std::max(0.0f,
                    V.x * H.x + V.y * H.y + V.z * H.z);
                const Vec3 L = {2.0f * VdotH * H.x - V.x,
                                2.0f * VdotH * H.y - V.y,
                                2.0f * VdotH * H.z - V.z};
                const float NdotL = std::max(0.0f, L.z);
                const float NdotH = std::max(0.0f, H.z);
                if (NdotL > 0.0f) {
                    const float G = GeometrySmith_IBL(NdotV, NdotL, roughness);
                    const float G_vis = (G * VdotH) / std::max(1e-4f,
                                                                NdotH * NdotV);
                    const float Fc = std::pow(1.0f - VdotH, 5.0f);
                    scale += (1.0f - Fc) * G_vis;
                    bias  += Fc * G_vis;
                }
            }
            scale /= float(kLutSamples);
            bias  /= float(kLutSamples);
            const size_t i = (size_t(y) * kLutW + size_t(x)) * 4;
            lutPixels[i + 0] = uint8_t(std::clamp(scale, 0.0f, 1.0f) * 255.0f);
            lutPixels[i + 1] = uint8_t(std::clamp(bias,  0.0f, 1.0f) * 255.0f);
            lutPixels[i + 2] = 0;
            lutPixels[i + 3] = 0xFF;
        }
    }
    TextureDesc lut;
    lut.width  = kLutW;
    lut.height = kLutH;
    lut.format = TextureFormat::RGBA8;
    lut.filter = TextureFilter::Linear;
    lut.pixels = lutPixels.data();
    m_brdfLutTex = device.CreateTexture(lut);

    // Three cascade shadow maps. Each backend wires up both a DSV (so
    // SetRenderTarget recognises it as depth-only) and an SRV (so the
    // main pass can sample it as a regular texture).
    for (int c = 0; c < kCascadeCount; ++c) {
        TextureDesc sm;
        sm.width        = kCascadeShadowSize;
        sm.height       = kCascadeShadowSize;
        sm.format       = TextureFormat::Depth24;
        sm.filter       = TextureFilter::Linear;
        sm.renderTarget = false;
        m_cascadeShadowMaps[c] = device.CreateTexture(sm);
    }

    NARAGAME_INFO("Renderer3D initialized (%d cascades @ %ux%u + IBL)",
                  kCascadeCount, kCascadeShadowSize, kCascadeShadowSize);
}

void Renderer3D::BakeIblFromSource(const float* srcPixels, int srcW, int srcH) {
    if (!m_device || !srcPixels || srcW <= 0 || srcH <= 0) return;

    // Output sky env tex: 256x128 RGBA16F. We keep the bake output
    // resolution fixed regardless of source HDR size so the shader's
    // SamplePrefilteredStrip math (strip = level/6) stays unchanged.
    constexpr int kSkyW = 256;
    constexpr int kSkyH = 128;
    std::vector<uint16_t> skyHalf(size_t(kSkyW) * size_t(kSkyH) * 4);
    for (int y = 0; y < kSkyH; ++y) {
        const float v = float(y) / float(kSkyH - 1);
        const float elev = v * kIblPi;
        const float sinE = std::sin(elev);
        const float cosE = std::cos(elev);
        for (int x = 0; x < kSkyW; ++x) {
            const float u = float(x) / float(kSkyW);
            const float az = (u - 0.5f) * 2.0f * kIblPi;
            const Vec3 dir = {sinE * std::sin(az), cosE, sinE * std::cos(az)};
            const Vec3 col = SampleSkyBilinearFloat(srcPixels, srcW, srcH, dir);
            const size_t i = (size_t(y) * kSkyW + size_t(x)) * 4;
            skyHalf[i + 0] = FloatToHalf(col.x);
            skyHalf[i + 1] = FloatToHalf(col.y);
            skyHalf[i + 2] = FloatToHalf(col.z);
            skyHalf[i + 3] = FloatToHalf(1.0f);
        }
    }
    TextureDesc sky;
    sky.width  = kSkyW;
    sky.height = kSkyH;
    sky.format = TextureFormat::RGBA16F;
    sky.filter = TextureFilter::Linear;
    sky.pixels = skyHalf.data();
    m_skyEnvTex = m_device->CreateTexture(sky);

    // Prefiltered atlas: six 256x128 strips, GGX-convolved at the
    // corresponding roughness level. 32 importance samples / pixel.
    constexpr int kPrefLevels  = 6;
    constexpr int kPrefSamples = 32;
    const int prefTotalH = kSkyH * kPrefLevels;
    std::vector<uint16_t> prefHalf(size_t(kSkyW) * size_t(prefTotalH) * 4);
    for (int level = 0; level < kPrefLevels; ++level) {
        const float roughness = float(level) / float(kPrefLevels - 1);
        const int rowStart = level * kSkyH;
        for (int y = 0; y < kSkyH; ++y) {
            const float v = float(y) / float(kSkyH - 1);
            const float elev = v * kIblPi;
            const float sinE = std::sin(elev);
            const float cosE = std::cos(elev);
            for (int x = 0; x < kSkyW; ++x) {
                const float u = float(x) / float(kSkyW);
                const float az = (u - 0.5f) * 2.0f * kIblPi;
                const Vec3 N = {sinE * std::sin(az), cosE, sinE * std::cos(az)};
                Vec3 sum{0.0f, 0.0f, 0.0f};
                float weight = 0.0f;
                for (int s = 0; s < kPrefSamples; ++s) {
                    const Vec2f xi = Hammersley(uint32_t(s),
                                                uint32_t(kPrefSamples));
                    const Vec3 H = ImportanceSampleGGX(xi, N, roughness);
                    const float NdotH = N.x * H.x + N.y * H.y + N.z * H.z;
                    const Vec3 L = {2.0f * NdotH * H.x - N.x,
                                    2.0f * NdotH * H.y - N.y,
                                    2.0f * NdotH * H.z - N.z};
                    const float NdotL = std::max(0.0f,
                                                 N.x * L.x + N.y * L.y + N.z * L.z);
                    if (NdotL > 0.0f) {
                        const Vec3 c = SampleSkyBilinearFloat(srcPixels,
                                                              srcW, srcH, L);
                        sum.x += c.x * NdotL;
                        sum.y += c.y * NdotL;
                        sum.z += c.z * NdotL;
                        weight += NdotL;
                    }
                }
                if (weight > 0.0f) {
                    sum.x /= weight; sum.y /= weight; sum.z /= weight;
                }
                const size_t i = (size_t(rowStart + y) * kSkyW + size_t(x)) * 4;
                prefHalf[i + 0] = FloatToHalf(sum.x);
                prefHalf[i + 1] = FloatToHalf(sum.y);
                prefHalf[i + 2] = FloatToHalf(sum.z);
                prefHalf[i + 3] = FloatToHalf(1.0f);
            }
        }
    }
    TextureDesc pref;
    pref.width  = kSkyW;
    pref.height = prefTotalH;
    pref.format = TextureFormat::RGBA16F;
    pref.filter = TextureFilter::Linear;
    pref.pixels = prefHalf.data();
    m_prefilteredEnvTex = m_device->CreateTexture(pref);
}

bool Renderer3D::LoadEnvironment(const std::string& hdrPath) {
    if (!m_device) return false;
    if (hdrPath.empty()) {
        // Revert to the procedural default.
        const std::vector<float> procSky = MakeProceduralSkyPixels();
        BakeIblFromSource(procSky.data(), kProcSkyW, kProcSkyH);
        NARAGAME_INFO("Renderer3D::LoadEnvironment: reverted to procedural sky");
        return true;
    }
    const HdrImage hdr = LoadHdrFile(hdrPath);
    if (!hdr.Valid()) {
        NARAGAME_WARN("Renderer3D::LoadEnvironment: '%s' invalid; "
                      "keeping current sky",
                      hdrPath.c_str());
        return false;
    }
    BakeIblFromSource(hdr.pixels.data(), int(hdr.width), int(hdr.height));
    NARAGAME_INFO("Renderer3D::LoadEnvironment: '%s' baked (%ux%u source)",
                  hdrPath.c_str(), hdr.width, hdr.height);
    return true;
}

void Renderer3D::Shutdown() {
    m_queue.clear();
    m_skinQueue.clear();
    m_skinPool.clear();
    for (auto& tex : m_cascadeShadowMaps) tex.reset();
    m_brdfLutTex.reset();
    m_prefilteredEnvTex.reset();
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

    // Fit three orthographic light cameras around concentric shadow
    // volumes, all centred on light.shadowCenter. Radii nest 0.3 / 0.6 /
    // 1.0 of the full shadow radius so close geometry samples the
    // tightest cascade and the far cascade still catches distant
    // casters. The pixel shader picks the smallest cascade that
    // contains the fragment.
    const float R = light.shadowRadius;
    const Vec3& c = light.shadowCenter;
    Vec3 dir = light.direction;
    float dl = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (dl > 1e-6f) { dir.x /= dl; dir.y /= dl; dir.z /= dl; }
    else            { dir = {0.0f, -1.0f, 0.0f}; }
    Vec3 up{0.0f, 1.0f, 0.0f};
    if (std::abs(dir.y) > 0.999f) up = {0.0f, 0.0f, 1.0f};

    const float cascadeRadii[kCascadeCount] = {R * 0.3f, R * 0.6f, R};
    Mat4 cascadeVP[kCascadeCount];
    for (int k = 0; k < kCascadeCount; ++k) {
        const float r = cascadeRadii[k];
        const Vec3 eye{c.x - dir.x * r * 2.0f,
                       c.y - dir.y * r * 2.0f,
                       c.z - dir.z * r * 2.0f};
        const Mat4 lightView = mat4::LookAt(eye, c, up);
        const Mat4 lightProj = mat4::Ortho(-r, r, -r, r, 0.1f, r * 4.0f);
        cascadeVP[k] = lightProj * lightView;
    }

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
    for (int k = 0; k < kCascadeCount; ++k) {
        CopyMatrix(cascadeVP[k], cb.lightViewProj + k * 16);
    }
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
    cb.shadowCenter[0]     = c.x;
    cb.shadowCenter[1]     = c.y;
    cb.shadowCenter[2]     = c.z;
    cb.shadowCenter[3]     = 0.0f;
    cb.cascadeRadii[0]     = cascadeRadii[0];
    cb.cascadeRadii[1]     = cascadeRadii[1];
    cb.cascadeRadii[2]     = cascadeRadii[2];
    cb.cascadeRadii[3]     = 0.0f;
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
    // Render the queue once per cascade. The cascade-specific light VP
    // already lives in FrameCB.lightViewProj[cascade]; the vertex shader
    // reads which one to use from mrParams.z, which we update per draw
    // per cascade below.
    auto packDraw = [](const QueuedMesh& q, int cascade, DrawCB& out) {
        CopyMatrix(q.model, out.model);
        out.albedoFactor[0]   = q.material.albedoFactor.r;
        out.albedoFactor[1]   = q.material.albedoFactor.g;
        out.albedoFactor[2]   = q.material.albedoFactor.b;
        out.albedoFactor[3]   = q.material.albedoFactor.a;
        out.mrParams[0]       = q.material.metallic;
        out.mrParams[1]       = q.material.roughness;
        out.mrParams[2]       = float(cascade);  // VSShadow reads this
        out.mrParams[3]       = 0.0f;
        out.emissiveFactor[0] = q.material.emissiveFactor.r;
        out.emissiveFactor[1] = q.material.emissiveFactor.g;
        out.emissiveFactor[2] = q.material.emissiveFactor.b;
        out.emissiveFactor[3] = 0.0f;
    };

    for (int cascade = 0; cascade < kCascadeCount; ++cascade) {
        m_device->SetRenderTarget(m_cascadeShadowMaps[cascade].get());
        m_device->ClearDepth(1.0f);

        m_device->BindPipeline(m_pipelineShadow.get());
        m_device->BindUniformBuffer(0, m_frameUB.get());

        DrawCB cb;
        for (const QueuedMesh& q : m_queue) {
            packDraw(q, cascade, cb);
            m_drawUB->Update(&cb, sizeof(cb));
            m_device->BindUniformBuffer(1, m_drawUB.get());
            m_device->BindVertexBuffer(q.vb, sizeof(Vertex3D), 0);
            m_device->BindIndexBuffer(q.ib, q.indexFormat);
            m_device->DrawIndexed(q.indexCount, 0);
        }

        // Skinned shadow pass for this cascade: swap PSO + upload SkinCB
        // per mesh. SkinCB is sized for kMaxJoints; copy in what the
        // caller supplied and leave the rest as zero-filled.
        if (!m_skinQueue.empty()) {
            m_device->BindPipeline(m_pipelineShadowSkin.get());
            m_device->BindUniformBuffer(0, m_frameUB.get());
            SkinCB skinCB{};
            for (const QueuedSkinned& q : m_skinQueue) {
                DrawCB dcb;
                QueuedMesh stub;
                stub.model    = q.model;
                stub.material = q.material;
                packDraw(stub, cascade, dcb);
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
                m_device->BindVertexBuffer(q.vb, sizeof(Vertex3DSkin), 0);
                m_device->BindIndexBuffer(q.ib, q.indexFormat);
                m_device->DrawIndexed(q.indexCount, 0);
            }
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
    // Cascade shadow maps sit at t1 / t8 / t9, IBL chain at t5..t7. All
    // renderer-owned so they stay bound for every draw; per-mesh
    // material textures land at t0 / t2 / t3 / t4 below.
    m_device->BindTexture(1, m_cascadeShadowMaps[0].get());
    m_device->BindTexture(8, m_cascadeShadowMaps[1].get());
    m_device->BindTexture(9, m_cascadeShadowMaps[2].get());
    m_device->BindTexture(5, m_skyEnvTex.get());
    m_device->BindTexture(6, m_prefilteredEnvTex.get());
    m_device->BindTexture(7, m_brdfLutTex.get());

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
        m_device->BindTexture(1, m_cascadeShadowMaps[0].get());
        m_device->BindTexture(8, m_cascadeShadowMaps[1].get());
        m_device->BindTexture(9, m_cascadeShadowMaps[2].get());
        m_device->BindTexture(5, m_skyEnvTex.get());
        m_device->BindTexture(6, m_prefilteredEnvTex.get());
        m_device->BindTexture(7, m_brdfLutTex.get());
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
