#version 450

// TransparentPass - 延迟模式下的半透明前向绘制
// 基于 pbr.frag，额外采样 GBuffer 深度做手动深度剔除
//（不能开启固定管线深度测试：合成阶段的深度缓冲已被清空，
//  不透明深度在 GBuffer 的深度纹理里，WaterPass 同款模式）

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragTangent;
layout(location = 4) in vec3 fragBitangent;
layout(location = 5) in vec3 fragViewPos;
layout(location = 6) in vec3 fragLightPos;
layout(location = 7) in vec4 fragMaterialParams;  // x=opacity y=alphaMode z=alphaCutoff

layout(location = 0) out vec4 outColor;

// Set 0: 全局 UBO + GBuffer 深度
layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec4 viewPos;
    vec4 lightPos;
    vec4 lightColor;
    vec4 viewportInfo;  // x=width y=height
} ubo;

layout(binding = 1) uniform sampler2D gDepth;   // GBuffer 深度（NDC [0,1]，WaterPass 同款）

// Set 1: 材质纹理（与 ForwardPass 共用布局）
layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D specularMap;

const float PI = 3.14159265359;

vec3 getNormalFromMap() {
    vec3 tangentNormal = texture(normalMap, fragTexCoord).xyz * 2.0 - 1.0;
    vec3 N = normalize(fragNormal);
    vec3 T = normalize(fragTangent);
    vec3 B = normalize(fragBitangent);
    mat3 TBN = mat3(T, B, N);
    return normalize(TBN * tangentNormal);
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
    // ---- 手动深度剔除：与 GBuffer 中已写入的不透明深度比较 ----
    vec2 screenUV = gl_FragCoord.xy / ubo.viewportInfo.xy;
    float sceneDepth = texture(gDepth, screenUV).r;
    // 少量偏移避免与不透明表面 z-fighting
    if (gl_FragCoord.z > sceneDepth + 0.001) discard;

    // ---- PBR 着色（与 pbr.frag 一致）----
    vec4 albedoRGBA = texture(albedoMap, fragTexCoord);
    vec3 albedo = pow(albedoRGBA.rgb, vec3(2.2));

    vec3 specMask = texture(specularMap, fragTexCoord).rgb;
    float specValue = (specMask.r + specMask.g + specMask.b) / 3.0;
    float roughness = clamp(1.0 - specValue * 0.8, 0.05, 1.0);
    float metallic = specValue * 0.3;
    float ao = 1.0;

    vec3 N = getNormalFromMap();
    vec3 V = normalize(fragViewPos - fragWorldPos);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    vec3 Lo = vec3(0.0);
    vec3 L = normalize(fragLightPos - fragWorldPos);
    vec3 H = normalize(V + L);
    float distance = length(fragLightPos - fragWorldPos);
    float attenuation = 1.0 / (distance * distance);
    vec3 lightColor = vec3(300.0, 300.0, 300.0);
    vec3 radiance = lightColor * attenuation;

    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;

    float NdotL = max(dot(N, L), 0.0);
    Lo += (kD * albedo / PI + specular) * radiance * NdotL;

    vec3 ambient = vec3(0.03) * albedo * ao;
    vec3 color = ambient + Lo;

    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0/2.2));

    // ---- alpha（此 pass 只处理 Blend 模式，保留完整逻辑以备用）----
    float alpha = 1.0;
    if (fragMaterialParams.y < 0.5) {
        alpha = 1.0;
    } else if (fragMaterialParams.y < 1.5) {
        if (albedoRGBA.a * fragMaterialParams.x < fragMaterialParams.z) discard;
        alpha = 1.0;
    } else {
        alpha = clamp(albedoRGBA.a * fragMaterialParams.x, 0.0, 1.0);
    }

    outColor = vec4(color, alpha);
}
