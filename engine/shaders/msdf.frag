#version 450

// MSDF (Multi-channel Signed Distance Field) fragment stage. The atlas's RGB
// channels carry signed distance to the glyph edge in 3 different directions;
// the median(r,g,b) is a noise-resistant best estimate of the true distance,
// and converting that distance to screen pixels via fwidth(uv) lets us
// anti-alias at arbitrary scale -- the whole reason we're doing this.
//
// Atlas pxRange (the SDF spread) is baked in by msdf-atlas-gen at generation
// time. We hard-code the value used in scripts/build-msdf-atlas.ps1 here; if
// that changes the shader needs to too. Future work: pass it as a uniform.

layout(set = 0, binding = 1) uniform sampler2D uTex;

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUV;
layout(location = 0) out vec4 outColor;

float median(float a, float b, float c) {
    return max(min(a, b), min(max(a, b), c));
}

float screenPxRange() {
    const float atlasPxRange = 4.0;       // matches -pxrange in build-msdf-atlas.ps1
    vec2 unitRange     = vec2(atlasPxRange) / vec2(textureSize(uTex, 0));
    vec2 screenTexSize = vec2(1.0) / fwidth(vUV);
    return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

void main() {
    vec3 msd = texture(uTex, vUV).rgb;
    float sd = median(msd.r, msd.g, msd.b) - 0.5;
    float pxDist = screenPxRange() * sd;
    float alpha  = clamp(pxDist + 0.5, 0.0, 1.0);
    outColor = vec4(vColor.rgb, vColor.a * alpha);
}
