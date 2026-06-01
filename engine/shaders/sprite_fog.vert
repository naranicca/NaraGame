#version 450

// Sprite vertex stage with fog UV passthrough. Identical to sprite.vert except
// it computes a fog texture UV from world-space position so the fragment stage
// can sample a visibility texture independently of the sprite's own UVs.

layout(set = 0, binding = 0) uniform Camera { mat4 uViewProj; };
layout(set = 0, binding = 3) uniform FogXform {
    // Two affine rows: fogU = uFogU.xyz . vec3(pos.xy, 1) and same for V.
    // Lets the caller send a full 2x3 transform (needed for iso games where
    // the fog texture is indexed by world tile coordinates but the vertex
    // positions are screen coordinates).
    vec4 uFogU;
    vec4 uFogV;
};

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec2 vFogUV;

void main() {
    gl_Position = uViewProj * vec4(inPos, 1.0);
    vColor = inColor;
    vUV = inUV;
    vFogUV = vec2(
        uFogU.x * inPos.x + uFogU.y * inPos.y + uFogU.z,
        uFogV.x * inPos.x + uFogV.y * inPos.y + uFogV.z
    );
}
