#version 450

// SSAO 采样计算片段着色器 (参照 NVIDIA Donut ComputeAO)
// 在子纹理 UV 空间偏移采样 → 重建 view-space 3D 坐标 → Alchemy AO 几何关系评估
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
    float amount;               // AO 强度乘数 (参照 NVIDIA ComputeAO)
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

// ---- 参照 NVIDIA ComputeAO ----
// V = 未归一化向量 (从当前像素 P 指向采样点 S)
// N = 当前像素法线 (view-space)
// InvR2 = 1.0 / (radius * radius)
float ComputeAO(vec3 V, vec3 N, float InvR2) {
    float VdotV = dot(V, V);
    float NdotV = dot(N, V) * inversesqrt(VdotV);    // = dot(N, normalize(V))
    float lambertian = clamp(NdotV - ubo.bias, 0.0, 1.0);
    float falloff = clamp(1.0 - VdotV * InvR2, 0.0, 1.0);
    return clamp(lambertian * falloff * ubo.amount, 0.0, 1.0);
}

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
    // 注意: Vulkan 翻转 Y 轴后 projection[1][1] 为负，需取 abs
    float screenRadiusX = (ubo.radius / abs(P.z)) * abs(ubo.projection[0][0]) * 0.5 * float(pc.subWidth);
    float screenRadiusY = (ubo.radius / abs(P.z)) * abs(ubo.projection[1][1]) * 0.5 * float(pc.subHeight);

    // 如果屏幕空间半径 < 1 texel，无意义
    float avgScreenRadius = (screenRadiusX + screenRadiusY) * 0.5;
    if (avgScreenRadius < 1.0) {
        outAO = 1.0;
        return;
    }

    // texel → UV 的转换系数
    vec2 texelSize = vec2(1.0 / float(pc.subWidth), 1.0 / float(pc.subHeight));

    float InvR2 = 1.0 / (ubo.radius * ubo.radius);
    float result = 0.0;
    int actualKernelSize = min(ubo.kernelSize, KERNEL_SIZE);
    float numValidSamples = 0.0;

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

        // 参照 NVIDIA ComputeAO: V = S - P (未归一化)
        vec3 V = S - P;
        result += ComputeAO(V, N, InvR2);
        numValidSamples += 1.0;
    }

    // 归一化
    if (numValidSamples > 0.0) {
        result /= numValidSamples;
    }

    // 输出: 1.0 = 无遮蔽, 0.0 = 完全遮蔽
    float ao = 1.0 - result;
    outAO = pow(ao, ubo.power);
}