#version 450

// MSDF vertex stage. Identical to the sprite vertex shader -- the only
// difference between the bitmap path and the MSDF path is in the fragment
// shader. Camera UBO at set 0 binding 0 matches the existing RHI plumbing.

layout(set = 0, binding = 0) uniform Camera {
    mat4 uViewProj;
} cam;

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aUV;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUV;

void main() {
    gl_Position = cam.uViewProj * vec4(aPos, 1.0);
    vColor = aColor;
    vUV = aUV;
}
