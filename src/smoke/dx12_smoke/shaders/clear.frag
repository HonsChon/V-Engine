#version 450

// Internal constant-color-clear fragment shader. The tint comes through a push
// constant block in the pipeline's dedicated root-constant space.

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushData {
    vec4 tint;
} pushData;

void main() {
    outColor = pushData.tint;
}
