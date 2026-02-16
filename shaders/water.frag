#version 450

// 水面片段着色器 - 内置 SSR 反射
// 只对水面像素进行光线步进，效率更高

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
    vec4 screenSize;       // xy: 屏幕尺寸
    vec4 ssrParams;        // x: maxDistance, y: maxSteps, z: thickness, w: reserved
} ubo;

// G-Buffer 采样器（用于 SSR）
layout(binding = 1) uniform sampler2D gPosition;   // 世界空间位置
layout(binding = 2) uniform sampler2D gNormal;     // 世界空间法线
layout(binding = 3) uniform sampler2D gDepth;      // 深度

// 场景颜色（光照后）
layout(binding = 4) uniform sampler2D sceneColor;

// ============================================================
// SSR 核心函数 - 屏幕空间光线步进
// ============================================================

// 简单的伪随机函数，用于抖动
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// 将世界坐标转换为屏幕坐标 (UV + NDC深度)
vec3 worldToScreen(vec3 worldPos) {
    vec4 clipPos = ubo.projection * ubo.view * vec4(worldPos, 1.0);
    vec3 ndc = clipPos.xyz / clipPos.w;
    
    vec3 screenPos;
    screenPos.xy = ndc.xy * 0.5 + 0.5;  // UV [0,1]
    screenPos.z = ndc.z;                 // NDC 深度 [0,1] in Vulkan
    
    return screenPos;
}

// 屏幕空间二分搜索细化
vec2 binarySearchScreen(vec2 startUV, vec2 endUV, float startDepth, float endDepth, float thickness) {
    vec2 midUV = startUV;
    float midDepth = startDepth;
    
    for (int i = 0; i < 8; i++) {
        midUV = (startUV + endUV) * 0.5;
        midDepth = (startDepth + endDepth) * 0.5;
        
        if (midUV.x < 0.0 || midUV.x > 1.0 || midUV.y < 0.0 || midUV.y > 1.0) {
            break;
        }
        
        float sampledDepth = texture(gDepth, midUV).r;
        float delta = midDepth - sampledDepth;
        
        if (delta > 0.0 && delta < thickness) {
            return midUV;
        } else if (delta > 0.0) {
            endUV = midUV;
            endDepth = midDepth;
        } else {
            startUV = midUV;
            startDepth = midDepth;
        }
    }
    
    return midUV;
}

// 屏幕空间光线步进
vec4 rayMarchScreenSpace(vec3 rayOrigin, vec3 rayDir) {
    float maxSteps = ubo.ssrParams.y;
    float thickness = ubo.ssrParams.z;
    
    // 计算光线起点和终点在屏幕空间的位置
    vec3 startScreen = worldToScreen(rayOrigin);
    
    // 在世界空间沿光线方向走一段距离作为终点
    float maxDistance = ubo.ssrParams.x;
    vec3 endWorld = rayOrigin + rayDir * maxDistance;
    vec3 endScreen = worldToScreen(endWorld);
    
    // 处理光线指向相机后方的情况
    if (endScreen.z < 0.0 || endScreen.z > 1.0) {
        // 裁剪到有效范围
        if (endScreen.z < 0.0) {
            float t = (0.001 - startScreen.z) / (endScreen.z - startScreen.z);
            if (t > 0.0 && t < 1.0) {
                endScreen = mix(startScreen, endScreen, t);
            } else {
                return vec4(0.0);
            }
        }
    }
    
    // 在屏幕空间计算步进方向和距离
    vec2 screenDelta = endScreen.xy - startScreen.xy;
    float screenDistance = length(screenDelta);
    
    // 如果屏幕空间距离太短，跳过
    if (screenDistance < 0.001) {
        return vec4(0.0);
    }
    
    // 计算步进参数
    float numSteps = min(maxSteps, screenDistance * ubo.screenSize.x);
    numSteps = max(numSteps, 32.0);
    
    // 添加抖动来打破规律性条纹
    float jitter = hash(gl_FragCoord.xy);
    
    vec2 stepUV = screenDelta / numSteps;
    float stepDepth = (endScreen.z - startScreen.z) / numSteps;
    
    // 开始步进（加入起始抖动）
    vec2 currentUV = startScreen.xy + stepUV * jitter;
    float currentDepth = startScreen.z + stepDepth * jitter;
    vec2 prevUV = startScreen.xy;
    float prevDepth = startScreen.z;
    
    for (int i = 0; i < int(numSteps); i++) {
        // 检查边界
        if (currentUV.x < 0.0 || currentUV.x > 1.0 || 
            currentUV.y < 0.0 || currentUV.y > 1.0 ||
            currentDepth < 0.0 || currentDepth > 1.0) {
            break;
        }
        
        // 采样深度缓冲
        float sampledDepth = texture(gDepth, currentUV).r;
        float deltaDepth = currentDepth - sampledDepth;
        
        // 检测命中
        if (deltaDepth > 0.0 && deltaDepth < 0.0005) {
            // 二分搜索细化命中点
            vec2 hitUV = binarySearchScreen(prevUV, currentUV, prevDepth, currentDepth, thickness);
            
            // 背面剔除
            vec3 hitNormal = texture(gNormal, hitUV).rgb;
            if (dot(hitNormal, rayDir) > 0.0) {
                // 背面，跳过继续搜索
                prevUV = currentUV;
                prevDepth = currentDepth;
                currentUV += stepUV;
                currentDepth += stepDepth;
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
        
        prevUV = currentUV;
        prevDepth = currentDepth;
        currentUV += stepUV;
        currentDepth += stepDepth;
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
    
    // ========== SSR 光线步进（屏幕空间版本）==========
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
    
    // ========== Fresnel 混合 ==========
    float NdotV = max(dot(normal, viewDir), 0.0);
    float fresnel = fresnelSchlick(NdotV, 0.02);  // 水的 F0 约为 0.02
    
    // 根据 Fresnel 和反射强度混合
    vec3 reflectionColor = reflection.rgb;
    float reflectionStrength = reflection.a;
    
    // 如果没有找到反射，使用天空色或环境色
    if (reflectionStrength < 0.01) {
        // 简单的天空色
        reflectionColor = mix(vec3(0.5, 0.7, 1.0), vec3(0.2, 0.4, 0.8), viewDir.y * 0.5 + 0.5);
        reflectionStrength = 0.3;
    }
    
    // 混合反射和折射
    vec3 finalColor = mix(underwaterColor, reflectionColor, fresnel * reflectionStrength);
    
    // ========== 高光 ==========
    vec3 lightDir = normalize(vec3(1.0, 1.0, 0.5));
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(perturbedNormal, halfDir), 0.0), 256.0);
    vec3 specular = vec3(1.0) * spec * 0.5;
    finalColor += specular;
    
    // ========== 深度边缘软化 ==========
    float sceneDepthValue = texture(gDepth, screenCoord).r;
    float waterDepth = gl_FragCoord.z;
    float depthDiff = sceneDepthValue - waterDepth;
    float edgeSoftness = smoothstep(0.0, 0.01, depthDiff);
    
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
    // outColor = vec4(texture(sceneColor, screenCoord).rgb, 1.0);
}