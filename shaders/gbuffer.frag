#version 450

// G-Buffer 片段着色器
// 输出到多个渲染目标 (MRT):
// - Location 0: Position (RGB16F) - 世界空间位置
// - Location 1: Normal (RGB16F) - 世界空间法线
// - Location 2: Albedo (RGBA8) - 反照率(RGB) + 金属度(A)

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in mat3 fragTBN;

// 多渲染目标输出
layout(location = 0) out vec4 outPosition;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outAlbedo;

// 纹理采样器
layout(binding = 1) uniform sampler2D albedoMap;
layout(binding = 2) uniform sampler2D normalMap;
layout(binding = 3) uniform sampler2D specularMap;  // R: 金属度, G: 粗糙度

void main() {
    // ========================================
    // 输出 0: 世界空间位置
    // ========================================
    outPosition = vec4(fragWorldPos, 1.0);
    
    // ========================================
    // 输出 1: 世界空间法线
    // ========================================
    // 从法线贴图获取切线空间法线
    vec3 normalMapValue = texture(normalMap, fragTexCoord).rgb;
    
    vec3 normal;
    if (length(normalMapValue) > 0.01) {
        // 将法线从 [0, 1] 转换到 [-1, 1]
        vec3 tangentNormal = normalMapValue * 2.0 - 1.0;
        // 转换到世界空间
        normal = normalize(fragTBN * tangentNormal);
    } else {
        // 如果没有法线贴图，使用顶点法线
        normal = normalize(fragNormal);
    }
    
    // 编码法线到 [0, 1] 范围存储（可选，这里直接存储）
    outNormal = vec4(normal * 0.5 + 0.5, 1.0);
    
    // ========================================
    // 输出 2: Albedo + 金属度
    // ========================================
    vec3 albedo = texture(albedoMap, fragTexCoord).rgb;
    
    // 从 specular 贴图获取金属度（假设存储在 R 通道）
    float metallic = texture(specularMap, fragTexCoord).r;
    
    outAlbedo = vec4(albedo, metallic);
}