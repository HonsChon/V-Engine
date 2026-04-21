#version 450

/**
 * Nanite Cluster 调试可视化 - 片段着色器
 * 
 * 支持多种可视化模式：
 * - Mode 0: Cluster 颜色 - 每个 Cluster 一种独特颜色
 * - Mode 1: 法线可视化 - 显示世界空间法线
 * - Mode 2: LOD 可视化 - 根据 LOD 级别着色
 * - Mode 3: 线框叠加 - 显示 Cluster 边界
 */

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) flat in uint fragClusterIndex;
layout(location = 4) flat in uint fragTotalClusters;
layout(location = 5) flat in uint fragDebugMode;

layout(location = 0) out vec4 outColor;

// UBO
layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec4 viewPos;
    vec4 lightPos;
    vec4 lightColor;
} ubo;

// ============================================
// 颜色生成函数
// ============================================

/**
 * 基于黄金比例的 HSV 颜色生成
 * 确保相邻 Cluster 颜色差异明显
 */
vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

vec3 getClusterColor(uint index, uint total) {
    // 黄金比例，确保颜色分布均匀
    const float goldenRatioConjugate = 0.618033988749895;
    
    // 使用黄金比例生成 Hue，避免相邻 Cluster 颜色相似
    float hue = fract(float(index) * goldenRatioConjugate);
    
    // 饱和度和亮度略微变化，增加视觉区分度
    float saturation = 0.7 + 0.3 * fract(float(index) * 0.371);
    float value = 0.8 + 0.2 * fract(float(index) * 0.529);
    
    return hsv2rgb(vec3(hue, saturation, value));
}

/**
 * 基于哈希的随机颜色（备选方案）
 */
uint hash(uint x) {
    x += (x << 10u);
    x ^= (x >> 6u);
    x += (x << 3u);
    x ^= (x >> 11u);
    x += (x << 15u);
    return x;
}

vec3 hashColor(uint index) {
    uint h = hash(index);
    return vec3(
        float((h >> 0) & 0xFFu) / 255.0,
        float((h >> 8) & 0xFFu) / 255.0,
        float((h >> 16) & 0xFFu) / 255.0
    ) * 0.6 + 0.4;  // 确保颜色不会太暗
}

// ============================================
// 光照计算（简化版）
// ============================================
vec3 calculateSimpleLighting(vec3 baseColor, vec3 normal, vec3 worldPos) {
    vec3 lightDir = normalize(ubo.lightPos.xyz - worldPos);
    vec3 viewDir = normalize(ubo.viewPos.xyz - worldPos);
    
    // 环境光
    float ambient = 0.2;
    
    // 漫反射
    float diff = max(dot(normal, lightDir), 0.0);
    
    // 简单的 Blinn-Phong 高光
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), 32.0) * 0.3;
    
    return baseColor * (ambient + diff * 0.8) + vec3(spec);
}

// ============================================
// 主函数
// ============================================
void main() {
    vec3 normal = normalize(fragNormal);
    
    // 双面光照：如果法线背向相机，翻转它
    vec3 viewDir = normalize(ubo.viewPos.xyz - fragWorldPos);
    if (dot(normal, viewDir) < 0.0) {
        normal = -normal;
    }
    
    vec3 color;
    
    switch (fragDebugMode) {
        case 0:  // Cluster 颜色模式
        {
            vec3 clusterColor = getClusterColor(fragClusterIndex, fragTotalClusters);
            color = calculateSimpleLighting(clusterColor, normal, fragWorldPos);
            break;
        }
        
        case 1:  // 法线可视化模式
        {
            // 将法线从 [-1, 1] 映射到 [0, 1]
            color = normal * 0.5 + 0.5;
            break;
        }
        
        case 2:  // LOD 可视化模式 (TODO: 当实现 LOD 后)
        {
            // 目前只有 LOD 0，显示绿色
            // 未来：LOD 0 = 绿色, LOD 1 = 黄色, LOD 2 = 橙色, LOD 3+ = 红色
            color = vec3(0.2, 0.8, 0.3);
            break;
        }
        
        case 3:  // 哈希随机颜色（更高对比度）
        {
            vec3 hashClusterColor = hashColor(fragClusterIndex);
            color = calculateSimpleLighting(hashClusterColor, normal, fragWorldPos);
            break;
        }
        
        default:
            color = vec3(1.0, 0.0, 1.0);  // 错误：品红色
            break;
    }
    
    outColor = vec4(color, 1.0);
}
