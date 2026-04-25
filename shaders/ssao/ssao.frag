#version 450

// SSAO 采样计算片段着色器
// 读取 deinterleaved Position/Normal 子纹理层
// 使用 64 个采样核心 + push constant 传入的层旋转角度计算遮蔽因子

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out float outAO;

// 采样核心 UBO
#define KERNEL_SIZE 64

layout(binding = 0) uniform SSAOParams {
    vec4 samples[KERNEL_SIZE];  // xyz: 采样偏移, w: unused
    mat4 projection;            // 投影矩阵（用于将视图空间转换为屏幕空间）
    mat4 view;                  // 视图矩阵（用于将世界空间转换为视图空间）
    float radius;               // 采样半径
    float bias;                 // 偏移量（防止自遮蔽）
    float power;                // 遮蔽强度
    int kernelSize;             // 实际使用的采样数
} ubo;

// Deinterleaved Position 和 Normal (当前层)
layout(binding = 1) uniform sampler2DArray positionArray;
layout(binding = 2) uniform sampler2DArray normalArray;

// Push Constants: 当前层信息
layout(push_constant) uniform PushConstants {
    int layerIndex;            // 当前处理的层 (0-15)
    float rotationAngle;       // 当前层的旋转角度
    int subWidth;              // 子纹理宽度
    int subHeight;             // 子纹理高度
} pc;

// 根据旋转角度构建 TBN 矩阵的旋转向量
vec3 getRotationVector(float angle) {
    return vec3(cos(angle), sin(angle), 0.0);
}

void main() {
    // 从 deinterleaved 子纹理中采样当前像素的世界空间位置和法线
    vec3 texCoord3D = vec3(fragTexCoord, float(pc.layerIndex));
    vec3 fragPos = texture(positionArray, texCoord3D).xyz;
    vec3 normal = normalize(texture(normalArray, texCoord3D).xyz);
    
    // 如果位置为零向量，说明是背景，AO = 1.0（无遮蔽）
    if (length(fragPos) < 0.001) {
        outAO = 1.0;
        return;
    }
    
    // 将世界空间位置/法线转换到视图空间
    vec3 fragPosView = (ubo.view * vec4(fragPos, 1.0)).xyz;
    vec3 normalView = normalize((ubo.view * vec4(normal, 0.0)).xyz);
    
    // 使用固定旋转角度构建旋转向量（替代噪声纹理）
    vec3 randomVec = getRotationVector(pc.rotationAngle);
    
    // 构建 TBN 矩阵（Gram-Schmidt 正交化）
    vec3 tangent = normalize(randomVec - normalView * dot(randomVec, normalView));
    vec3 bitangent = cross(normalView, tangent);
    mat3 TBN = mat3(tangent, bitangent, normalView);
    
    // 采样遍历
    float occlusion = 0.0;
    int actualKernelSize = min(ubo.kernelSize, KERNEL_SIZE);
    
    for (int i = 0; i < actualKernelSize; ++i) {
        // 获取采样点（切线空间 -> 视图空间）
        vec3 samplePos = TBN * ubo.samples[i].xyz;
        samplePos = fragPosView + samplePos * ubo.radius;
        
        // 将采样点投影到屏幕空间
        vec4 offset = ubo.projection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;                    // 透视除法
        offset.xyz = offset.xyz * 0.5 + 0.5;       // 变换到 [0, 1]
        
        // 在子纹理中采样对应位置的深度（需要转换到子纹理 UV）
        // 注意：这里直接用投影后的 UV 采样子纹理（子纹理覆盖同一视口范围）
        vec3 sampleTexCoord = vec3(offset.xy, float(pc.layerIndex));
        
        // 边界检查
        if (offset.x < 0.0 || offset.x > 1.0 || offset.y < 0.0 || offset.y > 1.0) {
            continue;
        }
        
        // 获取采样点处的实际世界位置，转到视图空间取深度
        vec3 sampledPos = texture(positionArray, sampleTexCoord).xyz;
        float sampleDepth = (ubo.view * vec4(sampledPos, 1.0)).z;
        
        // 范围检查（确保采样点在有效范围内）
        float rangeCheck = smoothstep(0.0, 1.0, ubo.radius / abs(fragPosView.z - sampleDepth));
        
        // 比较深度：如果采样深度 >= 当前片段深度 + bias，则被遮蔽
        occlusion += (sampleDepth >= samplePos.z + ubo.bias ? 1.0 : 0.0) * rangeCheck;
    }
    
    // 计算最终 AO 值
    occlusion = 1.0 - (occlusion / float(actualKernelSize));
    outAO = pow(occlusion, ubo.power);
}
