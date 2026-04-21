#version 450

/**
 * Nanite Cluster 调试可视化 - 顶点着色器
 * 
 * 为每个 Cluster 分配唯一颜色，便于可视化 Cluster 分割结果
 */

// Push Constants - 每个 Cluster 的数据
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 normalMatrix;
    uint clusterIndex;      // Cluster 索引（用于生成颜色）
    uint totalClusters;     // 总 Cluster 数量
    uint debugMode;         // 调试模式: 0=Cluster颜色, 1=法线, 2=LOD
    float padding;
} push;

// 顶点输入
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;

// 顶点输出
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) flat out uint fragClusterIndex;
layout(location = 4) flat out uint fragTotalClusters;
layout(location = 5) flat out uint fragDebugMode;

// UBO - 全局共享数据
layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec4 viewPos;
    vec4 lightPos;
    vec4 lightColor;
} ubo;

void main() {
    // 计算世界空间位置
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    
    // 计算世界空间法线
    mat3 normalMat = mat3(push.normalMatrix);
    fragNormal = normalize(normalMat * inNormal);
    
    // 传递纹理坐标
    fragTexCoord = inTexCoord;
    
    // 传递 Cluster 信息
    fragClusterIndex = push.clusterIndex;
    fragTotalClusters = push.totalClusters;
    fragDebugMode = push.debugMode;
    
    // 输出裁剪空间位置
    gl_Position = ubo.proj * ubo.view * worldPos;
}
