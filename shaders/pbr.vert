#version 450

layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 normalMatrix;
    vec4 viewPos;      // 使用 vec4 确保 std140 对齐
    vec4 lightPos;     // 使用 vec4 确保 std140 对齐
    vec4 lightColor;   // 使用 vec4 确保 std140 对齐
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragTangent;
layout(location = 4) out vec3 fragBitangent;
layout(location = 5) out vec3 fragViewPos;
layout(location = 6) out vec3 fragLightPos;

void main() {
    // Transform position to world space
    vec4 worldPos = ubo.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    
    // Transform normal to world space
    fragNormal = normalize(mat3(ubo.normalMatrix) * inNormal);
    
    // Transform tangent to world space
    fragTangent = normalize(mat3(ubo.normalMatrix) * inTangent);
    
    // Calculate bitangent
    fragBitangent = cross(fragNormal, fragTangent);
    
    // Pass through texture coordinates
    fragTexCoord = inTexCoord;
    
    // Pass view and light positions (提取 xyz 分量)
    fragViewPos = ubo.viewPos.xyz;
    fragLightPos = ubo.lightPos.xyz;
    
    // Transform to clip space
    gl_Position = ubo.proj * ubo.view * worldPos;
}