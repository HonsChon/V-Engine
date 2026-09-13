#version 450

// FXAA 顶点着色器 - 全屏三角形（同 ssr.vert 模式，无需顶点缓冲）

layout(location = 0) out vec2 fragTexCoord;

void main() {
    // 生成覆盖整个屏幕的大三角形
    fragTexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(fragTexCoord * 2.0 - 1.0, 0.0, 1.0);
}
