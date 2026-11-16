#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragTangent;
layout(location = 4) in vec3 fragBitangent;
layout(location = 5) in vec3 fragViewPos;
layout(location = 6) in vec4 fragMaterialParams;  // x=opacity y=alphaMode z=alphaCutoff

layout(location = 0) out vec4 outColor;

// UBO - 全局共享（相机 + 场景多灯；与 pbr.vert 声明完全一致）
struct LightData {
    vec4 colorType;       // rgb 颜色, a 类型(0 Dir / 1 Point / 2 Spot)
    vec4 positionInt;     // xyz 世界位置, a 强度
    vec4 directionRange;  // xyz 方向, a range
    vec4 atten;           // x const, y linear, z quadratic, w 内锥角(rad)
    vec4 outerPad;        // x 外锥角(rad)
};

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec4 viewPos;
    vec4 viewportInfo;
    vec4 lightCountPad;              // x = lightCount
    LightData lights[16];
} ubo;

// 纹理采样器 (Set 1)
layout(set = 1, binding = 0) uniform sampler2D albedoMap;
layout(set = 1, binding = 1) uniform sampler2D normalMap;
layout(set = 1, binding = 2) uniform sampler2D specularMap;  // 用作金属度/粗糙度控制

const float PI = 3.14159265359;

// 默认材质参数（当纹理不可用时的回退）
const float DEFAULT_METALLIC = 0.0;   // 非金属
const float DEFAULT_ROUGHNESS = 0.5;  // 中等粗糙度
const float DEFAULT_AO = 1.0;         // 无遮蔽

// 从法线贴图获取法线（切线空间到世界空间）
vec3 getNormalFromMap() {
    // 采样法线贴图
    vec3 tangentNormal = texture(normalMap, fragTexCoord).xyz * 2.0 - 1.0;
    
    // 构建 TBN 矩阵
    vec3 N = normalize(fragNormal);
    vec3 T = normalize(fragTangent);
    vec3 B = normalize(fragBitangent);
    mat3 TBN = mat3(T, B, N);
    
    // 转换到世界空间
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
    // 从纹理采样材质参数（RGB + alpha 一次采样）
    vec4 albedoRGBA = texture(albedoMap, fragTexCoord);
    vec3 albedo = pow(albedoRGBA.rgb, vec3(2.2));  // sRGB 到线性空间
    
    // 从高光贴图获取金属度和粗糙度
    // Spec Mask: 白色 = 高光/金属, 黑色 = 非高光/粗糙
    vec3 specMask = texture(specularMap, fragTexCoord).rgb;
    float specValue = (specMask.r + specMask.g + specMask.b) / 3.0;
    
    // 使用高光贴图控制粗糙度（反转：高光 = 低粗糙度）
    float roughness = 1.0 - specValue * 0.8;  // 保留一些基础粗糙度
    roughness = clamp(roughness, 0.05, 1.0);
    
    // 金属度：根据高光强度
    float metallic = specValue * 0.3;  // 地球主要是非金属
    
    float ao = DEFAULT_AO;
    
    // 从法线贴图获取法线
    vec3 N = getNormalFromMap();
    // 双面渲染（透明管线 cull None）：背面（内表面）法线翻转让其正确受光，
    // 否则 NdotL=0 → 内表面只剩环境光 ≈ 纯黑，透出"黑壳"
    if (!gl_FrontFacing) N = -N;
    vec3 V = normalize(fragViewPos - fragWorldPos);
    
    // Calculate reflectance at normal incidence
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);
    
    // Reflectance equation（场景多灯累加；公式与 GPU RT lightRadiance 同语义）
    vec3 Lo = vec3(0.0);
    int lightCount = int(ubo.lightCountPad.x);

    for (int i = 0; i < lightCount; ++i) {
        LightData ld = ubo.lights[i];
        int type = int(ld.colorType.a);

        vec3 toLight;
        float dist = 3.4e38;
        if (type == 0) {
            // 方向光：无衰减
            toLight = -ld.directionRange.xyz;
        } else {
            vec3 diff = ld.positionInt.xyz - fragWorldPos;
            dist = length(diff);
            if (dist < 1e-6) continue;
            toLight = diff / dist;
        }

        float NdotL = dot(N, toLight);
        if (NdotL <= 0.0) continue;

        vec3 radiance = ld.colorType.rgb * ld.positionInt.w;
        if (type == 1) {
            // 点光：物理衰减 + range 外平滑衰减
            float att = 1.0 / (ld.atten.x + ld.atten.y * dist + ld.atten.z * dist * dist);
            if (ld.directionRange.w > 0.0 && dist > ld.directionRange.w) {
                att *= max(0.0, 1.0 - (dist - ld.directionRange.w) / ld.directionRange.w);
            }
            radiance *= att;
        } else if (type == 2) {
            // 聚光：锥角平方过渡 + 物理衰减
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
        float NDF = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;  // 金属没有漫反射

        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        vec3 specular = numerator / denominator;

        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }
    
    // Ambient lighting (简化的环境光)
    vec3 ambient = vec3(0.03) * albedo * ao;
    
    vec3 color = ambient + Lo;
    
    // HDR tonemapping (Reinhard)
    color = color / (color + vec3(1.0));

    // Gamma correction
    color = pow(color, vec3(1.0/2.2));

    // Alpha 模式（由管线 + RenderSystem 分桶配合）：
    //   0 = Opaque: alpha 恒为 1（不透明管线绘制）
    //   1 = Mask:   alpha < cutoff 的片元 discard（植被 alpha test，不透明管线绘制）
    //   2 = Blend:  半透明混合管线绘制（SrcAlpha/OneMinusSrcAlpha，深度写关）
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