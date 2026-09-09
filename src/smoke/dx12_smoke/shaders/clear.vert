#version 450

// Internal constant-color-clear vertex shader (DX12RHICommandBuffer::clearColorImage).
// Identical triangle generation to blit.vert.

void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
