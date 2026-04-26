#version 450

// SSAO 4×4 Box Blur 片段着色器
// 对全分辨率 AO 纹理执行简单的均值模糊，消除采样噪点和 reinterleave 接缝

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out float outBlurredAO;

// 输入：reinterleave 后的全分辨率 AO 纹理
layout(binding = 0) uniform sampler2D aoInput;

void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(aoInput, 0));
    
    float result = 0.0;
    
    // 4×4 box blur (offset: -1.5 to +1.5)
    for (int x = -2; x < 2; ++x) {
        for (int y = -2; y < 2; ++y) {
            vec2 offset = vec2(float(x) + 0.5, float(y) + 0.5) * texelSize;
            result += texture(aoInput, fragTexCoord + offset).r;
        }
    }
    
    outBlurredAO = result / 16.0;
}
