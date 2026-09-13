#version 450

/**
 * Nanite Cluster 调试可视化 - 顶点着色器 (GPU-driven 间接绘制版, Phase 5)
 *
 * 配合 build_visible_geometry.comp:几何已在 GPU 上展开为紧凑顶点流,
 * 每 corner 携带所属 clusterIndex 属性(location 4)。model 矩阵 / LOD
 * 改为按 clusterIndex 查 SSBO(GeomTable + TransformBuffer),不再走
 * per-cluster push constant —— 这是单 draw 间接绘制的前提。
 *
 * 注:法线用 mat3(model)(统一缩放假设);场景内 nanite mesh 的变换均
 * 为 identity/均匀缩放,非均匀缩放需改逆矩阵上传。
 */

layout(push_constant) uniform PushConstants {
    uint totalClusters;     // 总 Cluster 数量
    uint debugMode;         // 调试模式: 0=Cluster颜色, 1=法线, 2=LOD, 3=哈希色
    uint pad0;
    uint pad1;
} push;

// 顶点输入(location 4 = 所属 cluster 索引,由 GPU 展开写入)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in uint inClusterIndex;

// 顶点输出
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) flat out uint fragClusterIndex;
layout(location = 4) flat out uint fragTotalClusters;
layout(location = 5) flat out uint fragDebugMode;
layout(location = 6) flat out uint fragLodLevel;

// UBO - 全局共享数据 (set 0 / binding 0)
layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec4 viewPos;
    vec4 lightPos;
    vec4 lightColor;
} ubo;

// 静态几何表(与 build_visible_geometry.comp 同源):
//   a = (srcIndexOffset, indexCount, dstCornerOffset, meshIndex)
//   b = (lodLevel, flags, clusterIndex, 0)
layout(binding = 1) buffer GeomTableBuffer {
    uvec4 geomTable[];
};

// 每 mesh 世界矩阵(与 culling 共用 TransformBuffer)
layout(binding = 2) buffer TransformBuffer {
    mat4 transforms[];
};

void main() {
    uvec4 a = geomTable[inClusterIndex * 2u];
    uvec4 b = geomTable[inClusterIndex * 2u + 1u];

    mat4 model = transforms[a.w];

    // 计算世界空间位置
    vec4 worldPos = model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    // 计算世界空间法线(统一缩放假设: mat3(model))
    fragNormal = normalize(mat3(model) * inNormal);

    // 传递纹理坐标
    fragTexCoord = inTexCoord;

    // 传递 Cluster 信息
    fragClusterIndex = inClusterIndex;
    fragTotalClusters = push.totalClusters;
    fragDebugMode = push.debugMode;
    fragLodLevel = b.x;

    // 输出裁剪空间位置
    gl_Position = ubo.proj * ubo.view * worldPos;
}
