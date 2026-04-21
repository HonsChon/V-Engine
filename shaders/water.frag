#version 450

// 水面片段着色器 - 内置 SSR 反射
// 只对水面像素进行光线步进，效率更高
// 使用线性深度进行深度比较，提高远距离精度

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec4 fragClipPos;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform WaterUBO {
    mat4 model;
    mat4 view;
    mat4 projection;
    mat4 invView;
    mat4 invProjection;
    vec4 cameraPos;
    vec4 waterColor;       // RGB: 水的颜色, A: 透明度
    vec4 waterParams;      // x: 波浪速度, y: 波浪强度, z: 时间, w: 折射强度
    vec4 screenSize;       // xy: 屏幕尺寸, zw: nearPlane, farPlane
    vec4 ssrParams;        // x: maxDistance, y: maxSteps, z: thickness（线性深度空间）, w: reserved
} ubo;

// G-Buffer 采样器（用于 SSR）
layout(binding = 1) uniform sampler2D gPosition;   // 世界空间位置
layout(binding = 2) uniform sampler2D gNormal;     // 世界空间法线
layout(binding = 3) uniform sampler2D gDepth;      // 深度（非线性）

// 场景颜色（光照后）
layout(binding = 4) uniform sampler2D sceneColor;

// ============================================================
// SSR 核心函数 - 屏幕空间光线步进（使用线性深度）
// ============================================================

// 简单的伪随机函数，用于抖动
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// 将非线性深度（NDC）转换为线性深度（视图空间）
float linearizeDepth(float depth) {
    float near = ubo.screenSize.z;
    float far = ubo.screenSize.w;
    // 标准透视投影的深度线性化公式
    return near * far / (far - depth * (far - near));
}

// 将世界坐标转换为屏幕坐标 (UV + NDC depth)
// 步进在屏幕空间进行以保持透视正确性
vec3 worldToScreen(vec3 worldPos) {
    vec4 clipPos = ubo.projection * ubo.view * vec4(worldPos, 1.0);
    vec3 ndc = clipPos.xyz / clipPos.w;
    
    vec3 screenPos;
    screenPos.xy = ndc.xy * 0.5 + 0.5;  // UV [0,1]
    screenPos.z = ndc.z;  // NDC depth [0,1] for Vulkan
    
    return screenPos;
}

// 屏幕空间二分搜索细化（在 NDC 深度空间步进，用线性深度比较）
vec2 binarySearchScreen(vec2 startUV, vec2 endUV, float startNDCDepth, float endNDCDepth, float thickness) {
    vec2 midUV = startUV;
    float midNDCDepth = startNDCDepth;
    
    for (int i = 0; i < 8; i++) {
        midUV = (startUV + endUV) * 0.5;
        midNDCDepth = (startNDCDepth + endNDCDepth) * 0.5;
        
        if (midUV.x < 0.0 || midUV.x > 1.0 || midUV.y < 0.0 || midUV.y > 1.0) {
            break;
        }
        
        // 采样深度并转换为线性深度
        float sampledNDCDepth = texture(gDepth, midUV).r;
        float sampledLinearDepth = linearizeDepth(sampledNDCDepth);
        float rayLinearDepth = linearizeDepth(midNDCDepth);
        
        float delta = rayLinearDepth - sampledLinearDepth;
        
        if (delta > 0.0 && delta < thickness) {
            return midUV;
        } else if (delta > 0.0) {
            endUV = midUV;
            endNDCDepth = midNDCDepth;
        } else {
            startUV = midUV;
            startNDCDepth = midNDCDepth;
        }
    }
    
    return midUV;
}

// 屏幕空间光线步进
// 在屏幕空间 (UV, NDC depth) 步进以保持透视正确性
// 深度比较时转换为线性深度以使用世界空间单位的厚度阈值
vec4 rayMarchScreenSpace(vec3 rayOrigin, vec3 rayDir) {
    float maxSteps = ubo.ssrParams.y;
    float thickness = ubo.ssrParams.z;  // 线性深度空间的厚度阈值
    
    // 计算光线起点和终点在屏幕空间的位置（UV + NDC depth）
    vec3 startScreen = worldToScreen(rayOrigin);
    
    // 在世界空间沿光线方向走一段距离作为终点
    float maxDistance = ubo.ssrParams.x;
    vec3 endWorld = rayOrigin + rayDir * maxDistance;
    vec3 endScreen = worldToScreen(endWorld);
    
    // 处理光线指向相机后方的情况（NDC depth < 0 或 > 1）
    if (endScreen.z < 0.0) {
        float t = (0.0 - startScreen.z) / (endScreen.z - startScreen.z);
        if (t > 0.0 && t < 1.0) {
            endScreen = mix(startScreen, endScreen, t);
        } else {
            return vec4(0.0);
        }
    }
    
    // 裁剪到远平面
    if (endScreen.z > 1.0) {
        float t = (1.0 - startScreen.z) / (endScreen.z - startScreen.z);
        if (t > 0.0 && t < 1.0) {
            endScreen = mix(startScreen, endScreen, t);
        }
    }
    
    // 在屏幕空间计算步进方向和距离
    vec3 screenDelta = endScreen - startScreen;
    float screenDistance = length(screenDelta.xy);
    
    // 如果屏幕空间距离太短，跳过
    if (screenDistance < 0.001) {
        return vec4(0.0);
    }
    
    // 计算步进参数
    float numSteps = min(maxSteps, screenDistance * ubo.screenSize.x);
    numSteps = max(numSteps, 32.0);
    
    // 添加抖动来打破规律性条纹
    float jitter = hash(gl_FragCoord.xy);
    
    vec3 stepScreen = screenDelta / numSteps;
    
    // 开始步进（加入起始抖动）
    vec3 currentScreen = startScreen + stepScreen * jitter;
    vec3 prevScreen = startScreen;
    
    for (int i = 0; i < int(numSteps); i++) {
        // 检查边界
        if (currentScreen.x < 0.0 || currentScreen.x > 1.0 || 
            currentScreen.y < 0.0 || currentScreen.y > 1.0 ||
            currentScreen.z < 0.0 || currentScreen.z > 1.0) {
            break;
        }
        
        // 采样深度缓冲
        float sampledNDCDepth = texture(gDepth, currentScreen.xy).r;
        
        // 转换为线性深度进行比较
        float sampledLinearDepth = linearizeDepth(sampledNDCDepth);
        float rayLinearDepth = linearizeDepth(currentScreen.z);
        
        float deltaDepth = rayLinearDepth - sampledLinearDepth;
        
        // 检测命中（使用线性深度空间的厚度阈值）
        if (deltaDepth > 0.0 && deltaDepth < thickness) {
            // 二分搜索细化命中点
            vec2 hitUV = binarySearchScreen(prevScreen.xy, currentScreen.xy, 
                                            prevScreen.z, currentScreen.z, thickness);
            
            // 背面剔除
            vec3 hitNormal = texture(gNormal, hitUV).rgb;
            if (dot(hitNormal, rayDir) > 0.0) {
                // 背面，跳过继续搜索
                prevScreen = currentScreen;
                currentScreen += stepScreen;
                continue;
            }
            
            // 计算衰减
            float edgeFade = 1.0 - max(
                abs(hitUV.x - 0.5) * 2.0,
                abs(hitUV.y - 0.5) * 2.0
            );
            edgeFade = clamp(edgeFade, 0.0, 1.0);
            edgeFade = pow(edgeFade, 2.0);
            
            float distanceFade = 1.0 - float(i) / numSteps;
            float fade = edgeFade * distanceFade;
            
            // 采样反射颜色
            vec3 reflectionColor = texture(sceneColor, hitUV).rgb;
            
            return vec4(reflectionColor, fade);
        }
        
        prevScreen = currentScreen;
        currentScreen += stepScreen;
    }
    
    // 未命中
    return vec4(0.0);
}

// ============================================================
// Fresnel 系数计算
// ============================================================
float fresnelSchlick(float cosTheta, float F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ============================================================
// 主函数
// ============================================================
void main() {
    // 水面法线（直接从顶点插值，比从 G-Buffer 采样更准确）
    vec3 normal = normalize(fragNormal);
    
    // 计算视线方向（从片元指向相机）
    vec3 viewDir = normalize(ubo.cameraPos.xyz - fragWorldPos);
    
    // 添加水面波纹扰动（可选）
    float time = ubo.waterParams.z;
    float waveStrength = ubo.waterParams.y;
    
    // 简单的法线扰动 - 暂时关闭以便调试
    vec3 perturbedNormal = normal;
     if (waveStrength > 0.001) {
         float wave1 = sin(fragWorldPos.x * 4.0 + time * 2.0) * cos(fragWorldPos.z * 3.0 + time * 1.5);
         float wave2 = sin(fragWorldPos.x * 2.0 - time * 1.0) * cos(fragWorldPos.z * 5.0 + time * 2.0);
         vec3 waveNormal = vec3(wave1, 0.0, wave2) * waveStrength;
         perturbedNormal = normalize(normal + waveNormal);
     }
    
    // ============================================================
    // 计算反射方向 - 核心修正
    // ============================================================
    // 入射方向：从相机到水面片元
    vec3 incidentDir = -viewDir;  // 即 normalize(fragWorldPos - cameraPos)
    
    // 使用 reflect 函数计算光学反射方向
    // reflect(I, N) = I - 2.0 * dot(N, I) * N
    // 对于水平水面 (normal = 0,1,0)，如果入射方向向下，反射方向应该向上
    vec3 reflectDir = reflect(incidentDir, perturbedNormal);
    
    // ========== 屏幕坐标计算 ==========
    // 使用 gl_FragCoord 来计算屏幕 UV，这比用 clipPos 更可靠
    // gl_FragCoord.xy 是像素坐标，范围 [0, screenSize)
    vec2 screenCoord = gl_FragCoord.xy / ubo.screenSize.xy;
    
    // ========== SSR 光线步进（屏幕空间版本，使用线性深度）==========
    // 从水面位置沿反射方向进行光线追踪
    // 反射方向向上，会找到水面上方的场景物体
    vec4 reflection = rayMarchScreenSpace(fragWorldPos + reflectDir * 0.05, reflectDir);
    
    // 采样折射颜色（水下场景）
    float distortionStrength = ubo.waterParams.w * 0.02;
    vec2 refractCoord = screenCoord;
    if (distortionStrength > 0.001) {
        vec2 distortion = vec2(
            sin(fragWorldPos.x * 4.0 + time * 2.0) + sin(fragWorldPos.z * 3.0 + time * 1.5),
            cos(fragWorldPos.z * 4.0 + time * 2.0) + cos(fragWorldPos.x * 3.0 + time * 1.5)
        ) * distortionStrength;
        refractCoord = clamp(screenCoord + distortion * 0.5, 0.001, 0.999);
    }
    
    vec3 refractionColor = texture(sceneColor, refractCoord).rgb;
    
    // 水的基础颜色
    vec3 waterBaseColor = ubo.waterColor.rgb;
    
    // 混合折射和水颜色
    vec3 underwaterColor = mix(refractionColor, waterBaseColor, 0.3);
    underwaterColor = waterBaseColor; //先不考虑折射

    // ========== Fresnel 混合 ==========
    float NdotV = max(dot(normal, viewDir), 0.0);
    // 提高 F0 值使反射更明显（物理正确的水 F0=0.02，但可以艺术性调整）
    float fresnel = fresnelSchlick(NdotV, 0.1);  // 提高基础反射率
    
    // 增强 Fresnel 效果，使边缘反射更强
    fresnel = pow(fresnel, 0.8);  // 指数小于1会增强整体反射
    fresnel = clamp(fresnel + 0.15, 0.0, 1.0);  // 添加最小反射量
    
    // 根据 Fresnel 和反射强度混合
    vec3 reflectionColor = reflection.rgb;
    float reflectionStrength = reflection.a;
    
    // 增强反射强度
    reflectionStrength = pow(reflectionStrength, 0.7);  // 使弱反射更可见
    reflectionStrength = clamp(reflectionStrength * 1.5, 0.0, 1.0);  // 整体增强
    
    // 如果没有找到反射，使用天空色或环境色
    if (reflectionStrength < 0.01) {
        // 简单的天空色
        reflectionColor = mix(vec3(0.5, 0.7, 1.0), vec3(0.2, 0.4, 0.8), viewDir.y * 0.5 + 0.5);
        reflectionStrength = 0.5;  // 提高环境反射强度
    }
    
    // 混合反射和折射
    // 使用更高的混合权重让反射更明显
    float reflectionFactor = fresnel * reflectionStrength;
    reflectionFactor = max(reflectionFactor, 0.2);  // 保证最小反射量
    vec3 finalColor = mix(underwaterColor, reflectionColor, reflectionFactor);
    // ========== 高光 ==========
    vec3 lightDir = normalize(vec3(1.0, 1.0, 0.5));
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(perturbedNormal, halfDir), 0.0), 256.0);
    vec3 specular = vec3(1.0) * spec * 0.5;
    finalColor += specular;
    
    // ========== 深度遮挡测试（基于 G-Buffer）==========
    // 结合深度比较和世界空间高度来判断遮挡关系
    
    float sceneNonLinearDepth = texture(gDepth, screenCoord).r;
    vec3 sceneWorldPos = texture(gPosition, screenCoord).rgb;
    
    // 计算深度
    vec4 waterViewPos = ubo.view * vec4(fragWorldPos, 1.0);
    float waterLinearDepth = -waterViewPos.z;
    float sceneLinearDepth = linearizeDepth(sceneNonLinearDepth);
    
    // 远平面检测
    float farPlane = ubo.screenSize.w;
    bool isBackground = sceneLinearDepth >= farPlane * 0.99;
    
    // ===== 核心遮挡逻辑 =====
    // 情况1：场景物体在水面上方（sceneWorldPos.y > fragWorldPos.y）
    //        且离相机更近（sceneLinearDepth < waterLinearDepth）
    //        → 物体遮挡水面
    //
    // 情况2：场景物体在水面下方（sceneWorldPos.y < fragWorldPos.y）
    //        → 无论深度如何，水面应该可见（水面覆盖水下物体）
    //
    // 情况3：场景物体在水面上方但离相机更远
    //        → 水面可见（水面在物体前面）
    
    float heightDiff = sceneWorldPos.y - fragWorldPos.y;  // 正值=物体在水上，负值=物体在水下
    float depthDiff = sceneLinearDepth - waterLinearDepth; // 正值=物体更远，负值=物体更近
    
    float edgeSoftness = 1.0;
    
    if (!isBackground) {
        // 物体在水面上方且离相机更近 → 遮挡水面
        if (heightDiff > 0.01 && depthDiff < 0.0) {
            // 使用 smoothstep 软化边缘
            edgeSoftness = smoothstep(-0.05, 0.0, depthDiff);
        }
        // 物体在水面下方 → 水面始终可见
        else if (heightDiff < -0.01) {
            edgeSoftness = 1.0;
        }
        // 物体和水面大致同高度 → 使用深度判断
        else {
            edgeSoftness = smoothstep(-0.02, 0.1, depthDiff);
        }
    }
    
    // 输出最终颜色
    float alpha = mix(ubo.waterColor.a, 1.0, fresnel);
    outColor = vec4(finalColor, alpha * edgeSoftness);
    
    // ========== 调试模式 ==========
    // 取消注释下面的行来调试不同阶段
    
    // 1. 可视化反射方向（应该大部分是向上的，Y分量为正，显示绿色）
    // outColor = vec4(reflectDir * 0.5 + 0.5, 1.0);
    
    // 2. 可视化法线方向（水平水面应该是(0,1,0)，显示为(0.5, 1, 0.5) 即绿色）
    // outColor = vec4(normal * 0.5 + 0.5, 1.0);
    
    // 3. 仅显示反射结果
    // outColor = vec4(reflectionColor, 1.0);
    
    // 4. 可视化命中强度（白色=找到反射，黑色=未找到）
    // outColor = vec4(vec3(reflectionStrength), 1.0);
    
    // 9. 调试：可视化 SSR 命中的屏幕 UV（红=X，绿=Y，蓝=未命中）
    // 这可以帮助看到 SSR 究竟采样的是哪里
    // vec3 hitScreen = worldToScreen(fragWorldPos + reflectDir * 0.05);
    // outColor = vec4(hitScreen.xy, reflection.a > 0.01 ? 0.0 : 1.0, 1.0);
    
    // 5. 仅显示折射/水下颜色
    // outColor = vec4(underwaterColor, 1.0);
    
    // 6. 仅显示 Fresnel 值
    // outColor = vec4(vec3(fresnel), 1.0);
    
    // 7. 可视化折射采样坐标（UV）
    // 如果是渐变色（左边黑，右边红；上边黑，下边绿），说明采样坐标正确
    // outColor = vec4(screenCoord.x, screenCoord.y, 0.0, 1.0);
    
    // 8. 直接采样 sceneColor（无折射扰动），看看 sceneColor 里到底有什么
    // outColor = vec4(specular, 1.0);
    
    // 10. 可视化线性深度差
    // outColor = vec4(vec3(depthDiff * 0.1), 1.0);
}