#version 450

// Push Constants - 每个物体独立的变换数据 + 材质透明参数
// 144 字节（两 mat4 + vec4），Vertex|Fragment 可见
layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 normalMatrix;
    vec4 materialParams;  // x=opacity, y=alphaMode(0/1/2), z=alphaCutoff, w=unused
} push;

// UBO - 全局共享的数据（相机、场景多灯）
// 与 pbr.frag / transparent.frag 声明完全一致（共用管线的跨阶段块必须匹配）
struct LightData {
    vec4 colorType;       // rgb 颜色, a 类型(0 Dir / 1 Point / 2 Spot)
    vec4 positionInt;     // xyz 世界位置, a 强度
    vec4 directionRange;  // xyz 方向, a range
    vec4 atten;           // x const, y linear, z quadratic, w 内锥角(rad)
    vec4 outerPad;        // x 外锥角(rad)
};

layout(binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    vec4 viewPos;
    vec4 viewportInfo;
    vec4 lightCountPad;              // x = lightCount
    LightData lights[16];
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
layout(location = 6) out vec4 fragMaterialParams;

void main() {
    // Transform position to world space (使用 Push Constants 的 model)
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    
    // Transform normal to world space (使用 Push Constants 的 normalMatrix)
    fragNormal = normalize(mat3(push.normalMatrix) * inNormal);
    
    // Transform tangent to world space
    fragTangent = normalize(mat3(push.normalMatrix) * inTangent);
    
    // Calculate bitangent
    fragBitangent = cross(fragNormal, fragTangent);
    
    // Pass through texture coordinates
    fragTexCoord = inTexCoord;
    
    // Pass view position
    fragViewPos = ubo.viewPos.xyz;

    // 传递材质透明参数
    fragMaterialParams = push.materialParams;

    // Transform to clip space
    gl_Position = ubo.proj * ubo.view * worldPos;
}