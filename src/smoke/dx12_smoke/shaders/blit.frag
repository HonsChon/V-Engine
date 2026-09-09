#version 450

// Internal scaled-blit fragment shader: linear-sampled source copy.

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D srcTex;

void main() {
    outColor = texture(srcTex, inUV);
}
