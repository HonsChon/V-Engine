#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragTangent;
layout(location = 4) in vec3 fragBitangent;
layout(location = 5) in vec3 fragViewPos;
layout(location = 6) in vec3 fragLightPos;

layout(location = 0) out vec4 outColor;

// 使用 push constant 或默认材质参数（暂时不使用纹理）
// layout(binding = 1) uniform sampler2D albedoMap;
// layout(binding = 2) uniform sampler2D normalMap;
// layout(binding = 3) uniform sampler2D metallicMap;
// layout(binding = 4) uniform sampler2D roughnessMap;
// layout(binding = 5) uniform sampler2D aoMap;

const float PI = 3.14159265359;

// 默认材质参数
const vec3 DEFAULT_ALBEDO = vec3(0.8, 0.2, 0.2);  // 红色
const float DEFAULT_METALLIC = 0.0;   // 非金属
const float DEFAULT_ROUGHNESS = 0.5;  // 中等粗糙度
const float DEFAULT_AO = 1.0;         // 无遮蔽

// 使用顶点法线（不使用法线贴图）
vec3 getNormal() {
    return normalize(fragNormal);
}

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;
    
    return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    // 使用默认材质参数（不使用纹理）
    vec3 albedo = DEFAULT_ALBEDO;
    float metallic = DEFAULT_METALLIC;
    float roughness = DEFAULT_ROUGHNESS;
    float ao = DEFAULT_AO;
    
    vec3 N = getNormal();
    vec3 V = normalize(fragViewPos - fragWorldPos);
    
    // Calculate reflectance at normal incidence
    // 对于非金属材质，F0 约为 0.04
    // 对于金属材质，F0 为 albedo 颜色
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);
    
    // Reflectance equation
    vec3 Lo = vec3(0.0);
    
    // Light calculations
    vec3 L = normalize(fragLightPos - fragWorldPos);
    vec3 H = normalize(V + L);
    float distance = length(fragLightPos - fragWorldPos);
    float attenuation = 1.0 / (distance * distance);
    vec3 lightColor = vec3(300.0, 300.0, 300.0);  // 强光源
    vec3 radiance = lightColor * attenuation;
    
    // Cook-Torrance BRDF
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;  // 金属没有漫反射
    
    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;
    
    float NdotL = max(dot(N, L), 0.0);
    Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    
    // Ambient lighting (简化的环境光)
    vec3 ambient = vec3(0.03) * albedo * ao;
    
    vec3 color = ambient + Lo;
    
    // HDR tonemapping (Reinhard)
    color = color / (color + vec3(1.0));
    
    // Gamma correction
    color = pow(color, vec3(1.0/2.2));
    
    outColor = vec4(color, 1.0);
}
