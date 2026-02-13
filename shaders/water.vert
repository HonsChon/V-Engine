#version 450

// 水面顶点着色器

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec4 fragClipPos;

layout(binding = 0) uniform WaterUBO {
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 cameraPos;
    vec4 waterColor;       // RGB: 水的颜色, A: 透明度
    vec4 waterParams;      // x: 波浪速度, y: 波浪强度, z: 时间, w: 折射强度
    vec4 screenSize;       // xy: 屏幕尺寸
} ubo;

void main() {
    vec3 worldPos = vec3(ubo.model * vec4(inPosition, 1.0));
    
    // 简单的波浪效果（可选）
    // float wave = sin(worldPos.x * 2.0 + ubo.waterParams.z * ubo.waterParams.x) * 
    //              cos(worldPos.z * 2.0 + ubo.waterParams.z * ubo.waterParams.x) * 
    //              ubo.waterParams.y;
    // worldPos.y += wave;
    
    fragWorldPos = worldPos;
    fragNormal = mat3(transpose(inverse(ubo.model))) * inNormal;
    fragTexCoord = inTexCoord;
    
    vec4 clipPos = ubo.projection * ubo.view * vec4(worldPos, 1.0);
    fragClipPos = clipPos;
    gl_Position = clipPos;
}
