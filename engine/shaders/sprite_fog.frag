#version 450

// Sprite fragment stage with a fog/visibility multiplier. Samples the sprite
// atlas (binding 1) and a low-resolution visibility texture (binding 2) and
// multiplies them. The visibility texture's R channel is the only one read --
// G/B/A are reserved for future channels (light color, dynamic lights, etc).

layout(set = 0, binding = 1) uniform sampler2D uTex;
layout(set = 0, binding = 2) uniform sampler2D uFog;

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec2 vFogUV;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 tex = texture(uTex, vUV);
    float fog = texture(uFog, vFogUV).r;
    outColor = vec4(tex.rgb * vColor.rgb * fog, tex.a * vColor.a);
}
