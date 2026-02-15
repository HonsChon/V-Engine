#version 450

// G-Buffer 顶点着色器
// 输出世界空间的位置、法线等信息供片段着色器使用

// Push Constants - 每个物体独立的变换数据
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 normalMatrix;
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out mat3 fragTBN;

// UBO - 全局共享的数据（相机、光照）
layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec4 viewPos;
    vec4 lightPos;
    vec4 lightColor;
} ubo;

void main() {
    // 计算世界空间位置（使用 Push Constants 的 model）
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    
    // 计算世界空间法线（使用 Push Constants 的 normalMatrix）
    mat3 normalMat = mat3(push.normalMatrix);
    fragNormal = normalize(normalMat * inNormal);
    
    // 传递纹理坐标
    fragTexCoord = inTexCoord;
    
    // 计算 TBN 矩阵（用于法线贴图）
    vec3 T = normalize(normalMat * inTangent);
    vec3 N = fragNormal;
    // 使用 Gram-Schmidt 正交化
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T);
    fragTBN = mat3(T, B, N);
    
    // 输出裁剪空间位置
    gl_Position = ubo.proj * ubo.view * worldPos;
}
