#version 450

// SSR 片段着色器 - 屏幕空间反射
// 基于 G-Buffer 进行屏幕空间光线步进
// 改进版：使用 UV 空间步进、抖动、背面剔除

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

// G-Buffer 采样器
layout(binding = 0) uniform sampler2D gPosition;   // 世界空间位置
layout(binding = 1) uniform sampler2D gNormal;     // 世界空间法线
layout(binding = 2) uniform sampler2D gAlbedo;     // 反照率
layout(binding = 3) uniform sampler2D gDepth;      // 深度

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
    float thickness;       // 厚度阈值
    float maxSteps;        // 最大步进次数
} ssr;

// ============================================================
// 工具函数
// ============================================================

// 伪随机函数，用于抖动以打破条纹
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// 将世界坐标转换为屏幕坐标 (UV + NDC深度)
vec3 worldToScreen(vec3 worldPos) {
    vec4 clipPos = ssr.projection * ssr.view * vec4(worldPos, 1.0);
    vec3 ndc = clipPos.xyz / clipPos.w;
    
    vec3 screenPos;
    screenPos.xy = ndc.xy * 0.5 + 0.5;  // UV [0,1]
    screenPos.z = ndc.z;                 // NDC 深度 [0,1] in Vulkan
    
    return screenPos;
}

// ============================================================
// 屏幕空间二分搜索细化
// ============================================================
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

// ============================================================
// 屏幕空间光线步进
// ============================================================
vec4 rayMarchScreenSpace(vec3 rayOrigin, vec3 rayDir) {
    float maxSteps = ssr.maxSteps;
    float thickness = ssr.thickness;
    
    // 计算光线起点在屏幕空间的位置
    vec3 startScreen = worldToScreen(rayOrigin);
    
    // 在世界空间沿光线方向走一段距离作为终点
    vec3 endWorld = rayOrigin + rayDir * ssr.maxDistance;
    vec3 endScreen = worldToScreen(endWorld);
    
    // 处理光线指向相机后方的情况
    if (endScreen.z < 0.0 || endScreen.z > 1.0) {
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
    
    // 计算步进次数（基于屏幕空间距离）
    float numSteps = min(maxSteps, screenDistance * ssr.screenSize.x);
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
        
        // 检测命中（使用较小的阈值以提高精度）
        if (deltaDepth > 0.0 && deltaDepth < 0.0005) {
            // 二分搜索细化命中点
            vec2 hitUV = binarySearchScreen(prevUV, currentUV, prevDepth, currentDepth, thickness);
            
            // 背面剔除：检查命中点的法线是否背对光线
            vec3 hitNormal = texture(gNormal, hitUV).rgb;
            if (dot(hitNormal, rayDir) > 0.0) {
                // 命中背面，跳过继续搜索
                prevUV = currentUV;
                prevDepth = currentDepth;
                currentUV += stepUV;
                currentDepth += stepDepth;
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
        
        prevUV = currentUV;
        prevDepth = currentDepth;
        currentUV += stepUV;
        currentDepth += stepDepth;
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
