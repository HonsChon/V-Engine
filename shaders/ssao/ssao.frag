#version 450

// SSAO 采样计算片段着色器 (Alchemy AO 风格)
// 在子纹理 UV 空间偏移采样 → 重建 view-space 3D 坐标 → 几何关系评估
// 配合 Deinterleaved Texturing: 子纹理中 1 texel 偏移 = 全分辨率 4 texel 偏移

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out float outAO;

#define KERNEL_SIZE 64

layout(binding = 0) uniform SSAOParams {
    vec4 samples[KERNEL_SIZE];  // xy: 单位圆盘上的 2D 采样方向, z: 径向缩放, w: unused
    mat4 projection;            // 投影矩阵
    mat4 view;                  // 视图矩阵（世界空间 → 视图空间）
    float radius;               // 采样半径 (view-space 单位)
    float bias;                 // 法线偏移量（防止平面自遮蔽）
    float power;                // 遮蔽强度指数
    int kernelSize;             // 实际使用的采样数
} ubo;

// Deinterleaved Position 和 Normal (当前层)
layout(binding = 1) uniform sampler2DArray positionArray;
layout(binding = 2) uniform sampler2DArray normalArray;

// Push Constants: 当前层信息
layout(push_constant) uniform PushConstants {
    int layerIndex;            // 当前处理的层 (0-15)
    float rotationAngle;       // 当前层的旋转角度 (用于打散采样核)
    int subWidth;              // 子纹理宽度
    int subHeight;             // 子纹理高度
} pc;

void main() {
    vec3 texCoord3D = vec3(fragTexCoord, float(pc.layerIndex));
    vec3 fragPosWorld = texture(positionArray, texCoord3D).xyz;
    vec3 normalWorld = normalize(texture(normalArray, texCoord3D).xyz);

    // 背景像素：无遮蔽
    if (dot(fragPosWorld, fragPosWorld) < 0.000001) {
        outAO = 1.0;
        return;
    }

    // 转换到 view space
    vec3 P = (ubo.view * vec4(fragPosWorld, 1.0)).xyz;
    vec3 N = normalize((ubo.view * vec4(normalWorld, 0.0)).xyz);

    // 每层固定旋转角度构建 2D 旋转矩阵，用于打散采样核
    float cosA = cos(pc.rotationAngle);
    float sinA = sin(pc.rotationAngle);
    mat2 rotation = mat2(cosA, -sinA, sinA, cosA);

    // 根据当前像素深度计算屏幕空间采样半径 (texel 单位)
    // 使用投影矩阵的焦距: focalLen = projection[0][0] (水平) 和 projection[1][1] (垂直)
    // 屏幕空间半径 = (worldRadius / |P.z|) * focalLen * 0.5 * subTextureSize
    float screenRadiusX = (ubo.radius / abs(P.z)) * ubo.projection[0][0] * 0.5 * float(pc.subWidth);
    float screenRadiusY = (ubo.radius / abs(P.z)) * ubo.projection[1][1] * 0.5 * float(pc.subHeight);

    // 限制最小/最大屏幕空间半径 (以 texel 为单位)
    float avgScreenRadius = (screenRadiusX + screenRadiusY) * 0.5;
    if (avgScreenRadius < 1.0) {
        outAO = 1.0;
        return;
    }

    // texel → UV 的转换系数
    vec2 texelSize = vec2(1.0 / float(pc.subWidth), 1.0 / float(pc.subHeight));

    float occlusion = 0.0;
    int actualKernelSize = min(ubo.kernelSize, KERNEL_SIZE);
    int validSamples = 0;

    for (int i = 0; i < actualKernelSize; ++i) {
        // samples[i].xy: 单位圆盘方向, samples[i].z: 径向缩放 (靠近中心密度高)
        vec2 diskDir = rotation * ubo.samples[i].xy;
        float radialScale = ubo.samples[i].z;

        // 屏幕空间偏移 (texel 单位) → UV 偏移
        vec2 uvOffset = diskDir * radialScale * vec2(screenRadiusX, screenRadiusY) * texelSize;
        vec2 sampleUV = fragTexCoord + uvOffset;

        // 边界检查
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) {
            continue;
        }

        // 从子纹理采样该位置的世界坐标 → 重建 view-space 3D 坐标
        vec3 samplePosWorld = texture(positionArray, vec3(sampleUV, float(pc.layerIndex))).xyz;

        // 跳过无效采样（背景像素）
        if (dot(samplePosWorld, samplePosWorld) < 0.000001) {
            continue;
        }

        vec3 S = (ubo.view * vec4(samplePosWorld, 1.0)).xyz;

        // ---- Alchemy AO 几何关系评估 ----
        // V: 从当前像素 P 指向采样点 S 的向量
        vec3 V = S - P;
        float distSq = dot(V, V);
        float radiusSq = ubo.radius * ubo.radius;

        // 距离超出采样半径 → 跳过
        if (distSq > radiusSq) {
            continue;
        }

        // 法线·方向 评估: dot(N, normalize(V)) 反映 S 是否在 P 的法线半球内
        // 等价于 dot(N, V) / |V|，用 dot(N,V) / sqrt(distSq) 避免额外 normalize
        float NdotV = dot(N, V);

        // 只有当 S 在法线半球内 (NdotV > bias * |V|) 才计入遮蔽
        // bias 用于防止平面自遮蔽
        float dist = sqrt(distSq);
        float cosAngle = NdotV / (dist + 0.0001);

        if (cosAngle <= ubo.bias) {
            validSamples++;
            continue;
        }

        // 距离衰减: 越近遮蔽越强，越远衰减到 0
        // 使用 1 - distSq/radiusSq 的平滑衰减
        float falloff = 1.0 - distSq / radiusSq;
        falloff = falloff * falloff;  // 更平滑的二次衰减

        // AO 贡献 = 法线对齐度 × 距离衰减
        occlusion += cosAngle * falloff;
        validSamples++;
    }

    // 归一化并输出
    if (validSamples > 0) {
        occlusion /= float(validSamples);
    }

    float ao = 1.0 - clamp(occlusion, 0.0, 1.0);
    outAO = pow(ao, ubo.power);
}
