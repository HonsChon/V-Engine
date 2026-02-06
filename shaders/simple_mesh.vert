#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inTangent;

layout(location = 0) out vec3 fragColor;

// 绕Y轴旋转矩阵
mat3 rotateY(float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return mat3(
        c,  0.0, s,
        0.0, 1.0, 0.0,
        -s, 0.0, c
    );
}

// 绕X轴旋转矩阵
mat3 rotateX(float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return mat3(
        1.0, 0.0, 0.0,
        0.0, c,   -s,
        0.0, s,   c
    );
}

void main() {
    // 旋转角度（弧度）
    float angleY = 0.6;  // 约35度绕Y轴
    float angleX = 0.4;  // 约23度绕X轴
    
    // 应用旋转变换
    mat3 rotation = rotateX(angleX) * rotateY(angleY);
    vec3 rotatedPos = rotation * inPosition;
    
    // 缩放并移动到相机前方
    vec3 pos = rotatedPos * 0.4;
    pos.z += 2.5;  // 移动到 z = 2.5 的位置（相机前方）
    
    // 简单透视投影
    float fov = 1.0;  // 视野角度因子
    float near = 0.1;
    float far = 100.0;
    
    // 透视除法
    float perspectiveZ = pos.z;
    vec2 perspectiveXY = pos.xy * fov / perspectiveZ;
    
    // 计算深度值（映射到 [0, 1]）
    float depth = (pos.z - near) / (far - near);
    
    gl_Position = vec4(perspectiveXY, depth, 1.0);
    
    // 旋转法线并基于法线着色
    vec3 rotatedNormal = rotation * inNormal;
    
    // 简单光照：使用定向光
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    float diffuse = max(dot(rotatedNormal, lightDir), 0.0);
    
    // 基于法线的颜色 + 漫反射光照
    vec3 baseColor = abs(rotatedNormal) * 0.5 + 0.3;
    fragColor = baseColor * (0.3 + 0.7 * diffuse);  // 环境光 + 漫反射
}
