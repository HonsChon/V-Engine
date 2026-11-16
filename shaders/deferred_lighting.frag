#version 450

// Deferred Lighting - 片段着色器
// 使用 G-Buffer 数据进行 PBR 光照计算

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

// Uniform Buffer（场景多灯；布局与 C++ LightingPass::LightingUBO 一致）
struct LightData {
    vec4 colorType;       // rgb 颜色, a 类型(0 Dir / 1 Point / 2 Spot)
    vec4 positionInt;     // xyz 世界位置, a 强度
    vec4 directionRange;  // xyz 方向, a range
    vec4 atten;           // x const, y linear, z quadratic, w 内锥角(rad)
    vec4 outerPad;        // x 外锥角(rad)
};

layout(binding = 0) uniform LightingUBO {
    vec4 viewPos;        // xyz: 相机位置
    vec4 ambientColor;   // rgb: 环境光颜色, a: 强度
    vec4 screenSize;     // xy: 屏幕尺寸
    vec4 lightCountPad;  // x: lightCount
    LightData lights[16];
} ubo;

// G-Buffer 纹理
layout(binding = 1) uniform sampler2D gPosition;  // 世界空间位置
layout(binding = 2) uniform sampler2D gNormal;    // 世界空间法线
layout(binding = 3) uniform sampler2D gAlbedo;    // Albedo (RGB) + Metallic (A)

// SSAO 纹理
layout(binding = 4) uniform sampler2D gSSAO;      // 环境光遮蔽

const float PI = 3.14159265359;

// Fresnel-Schlick 近似
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// GGX 法线分布函数
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return num / denom;
}

// Schlick-GGX 几何函数
float geometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / denom;
}

// Smith 几何遮蔽函数
float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = geometrySchlickGGX(NdotV, roughness);
    float ggx1 = geometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

void main() {
    // 从 G-Buffer 采样
    vec3 fragPos = texture(gPosition, fragTexCoord).rgb;
    vec3 normal = texture(gNormal, fragTexCoord).rgb;
    vec4 albedoMetallic = texture(gAlbedo, fragTexCoord);
    
    vec3 albedo = albedoMetallic.rgb;
    float metallic = albedoMetallic.a;
    
    metallic = 0;

    // 如果位置为零向量，说明是背景
    if (length(fragPos) < 0.001) {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    
    // 假设固定粗糙度（可以扩展 G-Buffer 存储）
    float roughness = 0.5;
    
    // 计算向量
    vec3 N = normalize(normal);
    vec3 V = normalize(ubo.viewPos.xyz - fragPos);

    // 基础反射率 F0
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // 场景多灯累加（公式与 GPU RT lightRadiance 同语义）
    vec3 Lo = vec3(0.0);
    int lightCount = int(ubo.lightCountPad.x);

    for (int i = 0; i < lightCount; ++i) {
        LightData ld = ubo.lights[i];
        int type = int(ld.colorType.a);

        vec3 toLight;
        float dist = 3.4e38;
        if (type == 0) {
            toLight = -ld.directionRange.xyz;
        } else {
            vec3 diff = ld.positionInt.xyz - fragPos;
            dist = length(diff);
            if (dist < 1e-6) continue;
            toLight = diff / dist;
        }

        float NdotL = dot(N, toLight);
        if (NdotL <= 0.0) continue;

        vec3 radiance = ld.colorType.rgb * ld.positionInt.w;
        if (type == 1) {
            float att = 1.0 / (ld.atten.x + ld.atten.y * dist + ld.atten.z * dist * dist);
            if (ld.directionRange.w > 0.0 && dist > ld.directionRange.w) {
                att *= max(0.0, 1.0 - (dist - ld.directionRange.w) / ld.directionRange.w);
            }
            radiance *= att;
        } else if (type == 2) {
            float cosTheta = dot(-toLight, ld.directionRange.xyz);
            float cosOuter = cos(ld.outerPad.x);
            float cosInner = cos(ld.atten.w);
            float spot = clamp((cosTheta - cosOuter) / max(cosInner - cosOuter, 1e-6), 0.0, 1.0);
            spot *= spot;
            float att = 1.0 / (ld.atten.x + ld.atten.y * dist + ld.atten.z * dist * dist);
            radiance *= spot * att;
        }

        // Cook-Torrance BRDF
        vec3 L = toLight;
        vec3 H = normalize(V + L);
        float NDF = distributionGGX(N, H, roughness);
        float G = geometrySmith(N, V, L, roughness);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        vec3 specular = numerator / denominator;

        // 能量守恒
        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;

        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }
    
    // 采样 SSAO 遮蔽值
    float ao = texture(gSSAO, fragTexCoord).r;

    // 环境光 (受 SSAO 调制)
    vec3 ambient = ubo.ambientColor.rgb * ubo.ambientColor.a * albedo * ao;
    
    // 最终颜色
    vec3 color = ambient + Lo;
    
    // HDR 色调映射
    color = color / (color + vec3(1.0));
    
    // Gamma 校正
    color = pow(color, vec3(1.0 / 2.2));
    
    outColor = vec4(color, 1.0);
}
