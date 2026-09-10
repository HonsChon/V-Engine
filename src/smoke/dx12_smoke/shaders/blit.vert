#version 450

// Internal scaled-blit vertex shader: fullscreen triangle from SV_VertexID,
// no vertex buffer, no descriptors. Used by DX12RHICommandBuffer::blitImage.

layout(location = 0) out vec2 outUV;

void main() {
    // (0,0) (2,0) (0,2) triangle covers the full viewport; UV in [0,2] so the
    // bilinear sample clamps at the far edge exactly like a 1:1 blit.
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    outUV = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
