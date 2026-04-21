#version 450

// SSR 片段着色器 - 屏幕空间反射
// 基于 G-Buffer 进行屏幕空间光线步进
// 改进版：使用线性深度进行深度比较，提高远距离精度

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

// G-Buffer 采样器
layout(binding = 0) uniform sampler2D gPosition;   // 世界空间位置
layout(binding = 1) uniform sampler2D gNormal;     // 世界空间法线
layout(binding = 2) uniform sampler2D gAlbedo;     // 反照率
layout(binding = 3) uniform sampler2D gDepth;      // 深度（非线性）

// 场景颜色（光照后）
layout(binding = 4) uniform sampler2D sceneColor;

// SSR 参数
layout(binding = 5) uniform SSRParams {
    mat4 projection;
    mat4 view;
    mat4 invProjection;
    mat4 invView;
    vec4 cameraPos;
    vec4 screenSize;       // xy: 屏幕尺寸, zw: 1/屏幕尺寸
    float maxDistance;     // 最大光线步进距离
    float resolution;      // 分辨率因子
    float thickness;       // 厚度阈值（线性深度空间，世界单位）
    float maxSteps;        // 最大步进次数
    float nearPlane;       // 近平面距离
    float farPlane;        // 远平面距离
} ssr;

// ============================================================
// 工具函数
// ============================================================

// 伪随机函数，用于抖动以打破条纹
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// 将非线性深度（NDC）转换为线性深度（视图空间）
// 对于 Vulkan 反向 Z: depth 范围 [0, 1]，0 为远平面，1 为近平面（实际是正向Z时）
// 标准 Vulkan: depth 范围 [0, 1]，0 为近平面，1 为远平面
float linearizeDepth(float depth) {
    float near = ssr.nearPlane;
    float far = ssr.farPlane;
    // 标准透视投影的深度线性化公式
    // z_linear = near * far / (far - depth * (far - near))
    return near * far / (far - depth * (far - near));
}

// 将世界坐标转换为屏幕坐标 (UV + NDC depth)
// 步进在屏幕空间 (UV, NDC depth) 进行以保持透视正确性
vec3 worldToScreen(vec3 worldPos) {
    vec4 clipPos = ssr.projection * ssr.view * vec4(worldPos, 1.0);
    vec3 ndc = clipPos.xyz / clipPos.w;
    
    vec3 screenPos;
    screenPos.xy = ndc.xy * 0.5 + 0.5;  // UV [0,1]
    screenPos.z = ndc.z;  // NDC depth [0,1] for Vulkan
    
    return screenPos;
}

// ============================================================
// 屏幕空间二分搜索细化（在 NDC 深度空间步进，用线性深度比较）
// ============================================================
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

// ============================================================
// 屏幕空间光线步进
// 在屏幕空间 (UV, NDC depth) 步进以保持透视正确性
// 深度比较时转换为线性深度以使用世界空间单位的厚度阈值
// ============================================================
vec4 rayMarchScreenSpace(vec3 rayOrigin, vec3 rayDir) {
    float maxSteps = ssr.maxSteps;
    float thickness = ssr.thickness;  // 线性深度空间的厚度阈值
    
    // 计算光线起点和终点在屏幕空间的位置（UV + NDC depth）
    vec3 startScreen = worldToScreen(rayOrigin);
    
    // 在世界空间沿光线方向走一段距离作为终点
    vec3 endWorld = rayOrigin + rayDir * ssr.maxDistance;
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
    
    // 计算步进次数（基于屏幕空间距离）
    float numSteps = min(maxSteps, screenDistance * ssr.screenSize.x);
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
            
            // 背面剔除：检查命中点的法线是否背对光线
            vec3 hitNormal = texture(gNormal, hitUV).rgb;
            if (dot(hitNormal, rayDir) > 0.0) {
                // 命中背面，跳过继续搜索
                prevScreen = currentScreen;
                currentScreen += stepScreen;
                continue;
            }
            
            // 计算边缘衰减
            float edgeFade = 1.0 - max(
                abs(hitUV.x - 0.5) * 2.0,
                abs(hitUV.y - 0.5) * 2.0
            );
            edgeFade = clamp(edgeFade, 0.0, 1.0);
            edgeFade = pow(edgeFade, 2.0);
            
            // 距离衰减
            float distanceFade = 1.0 - float(i) / numSteps;
            
            float fade = edgeFade * distanceFade;
            
            // 采样反射颜色
            vec3 reflectionColor = texture(sceneColor, hitUV).rgb;
            
            return vec4(reflectionColor, fade);
        }
        
        prevScreen = currentScreen;
        currentScreen += stepScreen;
    }
    
    // 未找到交点
    return vec4(0.0);
}

void main() {
    // 采样 G-Buffer
    vec3 worldPos = texture(gPosition, fragTexCoord).rgb;
    // 直接读取世界空间法线（gbuffer.frag 已直接存储，无需逆映射）
    vec3 normal = normalize(texture(gNormal, fragTexCoord).rgb);
    vec4 albedo = texture(gAlbedo, fragTexCoord);
    float depth = texture(gDepth, fragTexCoord).r;
    
    // 如果深度为最远（没有几何体），直接返回场景颜色
    if (depth >= 0.9999) {
        outColor = texture(sceneColor, fragTexCoord);
        return;
    }
    
    // ============================================================
    // 计算反射方向 - 修正版
    // ============================================================
    // viewDir: 从片元指向相机
    vec3 viewDir = normalize(ssr.cameraPos.xyz - worldPos);
    // incidentDir: 从相机指向片元（入射方向）
    vec3 incidentDir = -viewDir;
    // reflect(I, N) 需要入射方向，返回反射方向
    vec3 reflectDir = reflect(incidentDir, normal);
    
    // 进行屏幕空间光线步进
    vec4 reflection = rayMarchScreenSpace(worldPos + reflectDir * 0.05, reflectDir);
    
    // 获取原始场景颜色
    vec3 sceneCol = texture(sceneColor, fragTexCoord).rgb;
    
    // 基于金属度/反射率混合
    float metallic = albedo.a;
    float reflectivity = max(metallic, 0.04); // 基础反射率
    
    // 混合反射和场景颜色
    vec3 finalColor = mix(sceneCol, reflection.rgb, reflection.a * reflectivity);
    
    outColor = vec4(finalColor, 1.0);
    
    // 调试：直接输出反射结果
     outColor = reflection;
    
    // 调试：可视化反射方向 (应该大部分朝上)
    // outColor = vec4(reflectDir * 0.5 + 0.5, 1.0);
    
    // 调试：可视化法线
    // outColor = vec4(normal * 0.5 + 0.5, 1.0);
}