#version 450

// 水面片段着色器 - 带 SSR 反射

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec4 fragClipPos;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform WaterUBO {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 cameraPos;
    vec4 waterColor;       // RGB: 水的颜色, A: 透明度
    vec4 waterParams;      // x: 波浪速度, y: 波浪强度, z: 时间, w: 折射强度
    vec4 screenSize;       // xy: 屏幕尺寸
} ubo;

// 反射纹理（SSR 结果）
layout(binding = 1) uniform sampler2D reflectionTexture;

// 场景深度
layout(binding = 2) uniform sampler2D sceneDepth;

// 场景颜色（用于折射）
layout(binding = 3) uniform sampler2D sceneColor;

// Fresnel 系数计算
float fresnelSchlick(float cosTheta, float F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    // 归一化法线
    vec3 normal = normalize(fragNormal);
    
    // 计算视线方向
    vec3 viewDir = normalize(ubo.cameraPos.xyz - fragWorldPos);
    
    // 计算屏幕坐标
    vec2 screenCoord = (fragClipPos.xy / fragClipPos.w) * 0.5 + 0.5;

    
    // 添加水面扰动
    float time = ubo.waterParams.z;
    float distortionStrength = ubo.waterParams.w * 0.02;
    
    vec2 distortion = vec2(
        sin(fragWorldPos.x * 4.0 + time * 2.0) + sin(fragWorldPos.z * 3.0 + time * 1.5),
        cos(fragWorldPos.z * 4.0 + time * 2.0) + cos(fragWorldPos.x * 3.0 + time * 1.5)
    ) * distortionStrength;
    
    // 反射坐标
    vec2 reflectCoord = screenCoord + distortion;
    reflectCoord = clamp(reflectCoord, 0.001, 0.999);
    
    // 折射坐标（向下偏移一些）
    vec2 refractCoord = screenCoord + distortion * 0.5;
    refractCoord = clamp(refractCoord, 0.001, 0.999);
    
    // 采样反射颜色
    vec3 reflectionColor = texture(reflectionTexture, reflectCoord).rgb;
    
    // 采样折射颜色（水下场景）
    vec3 refractionColor = texture(sceneColor, refractCoord).rgb;
    
    // 水的基础颜色
    vec3 waterBaseColor = ubo.waterColor.rgb;
    
    // 混合折射和水颜色
    vec3 underwaterColor = mix(refractionColor, waterBaseColor, 0.3);
    
    // 计算 Fresnel 效果
    float NdotV = max(dot(normal, viewDir), 0.0);
    float fresnel = fresnelSchlick(NdotV, 0.02);  // 水的 F0 约为 0.02
    
    // 根据 Fresnel 混合反射和折射
    vec3 finalColor = mix(underwaterColor, reflectionColor, fresnel);
    
    // 添加高光
    vec3 lightDir = normalize(vec3(1.0, 1.0, 0.5));
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), 256.0);
    vec3 specular = vec3(1.0) * spec * 0.5;
    
    finalColor += specular;
    
    // 深度边缘软化（可选）
    float sceneDepthValue = texture(sceneDepth, screenCoord).r;
    float waterDepth = gl_FragCoord.z;
    float depthDiff = sceneDepthValue - waterDepth;
    float edgeSoftness = smoothstep(0.0, 0.01, depthDiff);
    
    // 输出最终颜色
    float alpha = mix(ubo.waterColor.a, 1.0, fresnel);
    outColor = vec4(finalColor, alpha * edgeSoftness);
    outColor = vec4(reflectionColor,1);
}
