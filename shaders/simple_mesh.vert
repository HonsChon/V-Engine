#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;

layout(location = 0) out vec3 fragColor;

void main() {
    // 简单的变换：轻微缩放和旋转
    vec3 pos = inPosition * 0.5;
    gl_Position = vec4(pos, 1.0);
    
    // 基于法线的颜色
    fragColor = abs(inNormal);
}