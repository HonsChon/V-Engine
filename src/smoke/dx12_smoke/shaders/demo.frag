#version 450

// DX12 smoke demo fragment shader.
// Texture at set 1 / binding 0 -> space 1; push constants in space 2 (b0).

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D albedoTex;

layout(push_constant) uniform PushData {
    vec4 tint;
} pushData;

void main() {
    outColor = texture(albedoTex, inUV) * pushData.tint;
}
