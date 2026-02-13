#version 450

// SSR 顶点着色器 - 全屏四边形
// 用于后处理 Pass

layout(location = 0) out vec2 fragTexCoord;

void main() {
    // 生成全屏三角形的顶点
    // 使用一个大三角形覆盖整个屏幕比两个三角形更高效
    fragTexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(fragTexCoord * 2.0 - 1.0, 0.0, 1.0);
    
    // 翻转 Y 坐标以匹配 Vulkan 坐标系
    fragTexCoord.y = 1.0 - fragTexCoord.y;
}
