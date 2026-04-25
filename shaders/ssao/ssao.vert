#version 450

// SSAO 全屏三角形顶点着色器
// 使用无顶点缓冲的全屏三角形技巧

layout(location = 0) out vec2 fragTexCoord;

void main() {
    // 生成覆盖屏幕的大三角形 (vertexIndex: 0, 1, 2)
    fragTexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(fragTexCoord * 2.0 - 1.0, 0.0, 1.0);
}
