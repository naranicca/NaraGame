#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform Camera { mat4 uViewProj; };

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUV;

void main() {
    gl_Position = uViewProj * vec4(inPos, 1.0);
    vColor = inColor;
    vUV = inUV;
}
