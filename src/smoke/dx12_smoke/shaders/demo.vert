#version 450

// DX12 smoke demo vertex shader.
// Conventions (match DX12RHIPipeline input layout + root signature):
//   TEXCOORD<location> semantics, UBO at set 0 / binding 0 (space 0 in DX12).

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec2 inUV;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 mvp;
} camera;

layout(location = 0) out vec2 outUV;

void main() {
    gl_Position = camera.mvp * vec4(inPos, 1.0);
    outUV = inUV;
}
