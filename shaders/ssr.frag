#version 450

// SSR 片段着色器 - 屏幕空间反射
// 基于 G-Buffer 进行光线步进

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

// 将世界坐标转换为屏幕坐标
vec3 worldToScreen(vec3 worldPos) {
    vec4 clipPos = ssr.projection * ssr.view * vec4(worldPos, 1.0);
    clipPos.xyz /= clipPos.w;
    
    // NDC to screen coordinates
    vec3 screenPos;
    screenPos.xy = clipPos.xy * 0.5 + 0.5;
    screenPos.y = 1.0 - screenPos.y;  // Vulkan Y 翻转
    screenPos.z = clipPos.z;
    
    return screenPos;
}

// 将屏幕坐标转换为视空间坐标
vec3 screenToView(vec2 uv, float depth) {
    // 从深度缓冲重建视空间位置
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y = -clipPos.y;  // Vulkan Y 翻转
    
    vec4 viewPos = ssr.invProjection * clipPos;
    return viewPos.xyz / viewPos.w;
}

// 二分搜索细化交点
vec3 binarySearch(vec3 rayOrigin, vec3 rayDir, float startDepth) {
    float depth = startDepth;
    vec3 hitPos = rayOrigin;
    
    for (int i = 0; i < 8; i++) {
        vec3 screenPos = worldToScreen(hitPos);
        
        if (screenPos.x < 0.0 || screenPos.x > 1.0 || 
            screenPos.y < 0.0 || screenPos.y > 1.0) {
            break;
        }
        
        float sampledDepth = texture(gDepth, screenPos.xy).r;
        float deltaDepth = screenPos.z - sampledDepth;
        
        if (deltaDepth > 0.0) {
            hitPos -= rayDir * depth;
        } else {
            hitPos += rayDir * depth;
        }
        
        depth *= 0.5;
    }
    
    return hitPos;
}

// 光线步进
vec4 rayMarch(vec3 rayOrigin, vec3 rayDir) {
    vec3 currentPos = rayOrigin;
    float stepSize = ssr.maxDistance / ssr.maxSteps;
    
    for (int i = 0; i < int(ssr.maxSteps); i++) {
        currentPos += rayDir * stepSize;
        
        // 转换到屏幕空间
        vec3 screenPos = worldToScreen(currentPos);
        
        // 检查是否超出屏幕边界
        if (screenPos.x < 0.0 || screenPos.x > 1.0 || 
            screenPos.y < 0.0 || screenPos.y > 1.0 ||
            screenPos.z < 0.0 || screenPos.z > 1.0) {
            break;
        }
        
        // 采样深度
        float sampledDepth = texture(gDepth, screenPos.xy).r;
        float deltaDepth = screenPos.z - sampledDepth;
        
        // 检测交点
        if (deltaDepth > 0.0 && deltaDepth < ssr.thickness) {
            // 使用二分搜索细化交点
            vec3 hitPos = binarySearch(currentPos - rayDir * stepSize, rayDir, stepSize);
            vec3 hitScreen = worldToScreen(hitPos);
            
            // 计算衰减（基于距离和屏幕边缘）
            float edgeFade = 1.0 - max(
                abs(hitScreen.x - 0.5) * 2.0,
                abs(hitScreen.y - 0.5) * 2.0
            );
            edgeFade = clamp(edgeFade, 0.0, 1.0);
            edgeFade = pow(edgeFade, 2.0);
            
            // 距离衰减
            float distanceFade = 1.0 - float(i) / ssr.maxSteps;
            
            float fade = edgeFade * distanceFade;
            
            // 采样反射颜色
            vec3 reflectionColor = texture(sceneColor, hitScreen.xy).rgb;
            
            return vec4(reflectionColor, fade);
        }
        
        // 自适应步长 - 远离物体时增大步长
        if (deltaDepth < -ssr.thickness) {
            stepSize *= 1.5;
        }
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
    
    // 计算反射方向
    vec3 viewDir = normalize(worldPos - ssr.cameraPos.xyz);
    vec3 reflectDir = reflect(viewDir, normal);
    
    // 进行光线步进
    vec4 reflection = rayMarch(worldPos + reflectDir * 0.1, reflectDir);
    
    // 获取原始场景颜色
    vec3 sceneCol = texture(sceneColor, fragTexCoord).rgb;
    
    // 基于金属度/反射率混合
    float metallic = albedo.a;
    float reflectivity = max(metallic, 0.04); // 基础反射率
    
    // 混合反射和场景颜色
    vec3 finalColor = mix(sceneCol, reflection.rgb, reflection.a * reflectivity);
    
    outColor = vec4(finalColor, 1.0);
    outColor = reflection;
}
