#version 450

// FXAA 片段着色器 - 快速近似抗锯齿 (FXAA 3.11 console 版)
// 消除几何轮廓与透明相交边界上的锯齿；关闭时直通采样（无额外 blit 路径）

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D sceneColor;

layout(push_constant) uniform FXAAParams {
    vec4 params;   // x=1/width y=1/height z=enable(1/0) w=unused
} pc;

const float FXAA_SPAN_MAX    = 8.0;
const float FXAA_REDUCE_MUL  = 1.0 / 8.0;
const float FXAA_REDUCE_MIN  = 1.0 / 128.0;

const vec3 LUMA = vec3(0.299, 0.587, 0.114);

void main() {
    // 直通模式（enableFXAA 关闭时）
    if (pc.params.z < 0.5) {
        outColor = texture(sceneColor, fragTexCoord);
        return;
    }

    vec2 rcpFrame = pc.params.xy;

    // 采样 3x3 邻域（中心 + 四角）
    vec3 rgbNW = texture(sceneColor, fragTexCoord + vec2(-1.0, -1.0) * rcpFrame).rgb;
    vec3 rgbNE = texture(sceneColor, fragTexCoord + vec2( 1.0, -1.0) * rcpFrame).rgb;
    vec3 rgbSW = texture(sceneColor, fragTexCoord + vec2(-1.0,  1.0) * rcpFrame).rgb;
    vec3 rgbSE = texture(sceneColor, fragTexCoord + vec2( 1.0,  1.0) * rcpFrame).rgb;
    vec3 rgbM  = texture(sceneColor, fragTexCoord).rgb;

    float lumaNW = dot(rgbNW, LUMA);
    float lumaNE = dot(rgbNE, LUMA);
    float lumaSW = dot(rgbSW, LUMA);
    float lumaSE = dot(rgbSE, LUMA);
    float lumaM  = dot(rgbM,  LUMA);

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    // 平坦区域不做处理（性能关键：大多数像素在此早退）
    if (lumaMax - lumaMin < max(0.0312, lumaMax * 0.125)) {
        outColor = vec4(rgbM, 1.0);
        return;
    }

    // 估计边缘方向
    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max(
        (lumaNW + lumaNE + lumaSW + lumaSE) * 0.25 * FXAA_REDUCE_MUL,
        FXAA_REDUCE_MIN);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);

    // 沿边缘方向的采样步长（限制最大跨度）
    dir = clamp(dir * rcpDirMin,
                vec2(-FXAA_SPAN_MAX, -FXAA_SPAN_MAX),
                vec2(FXAA_SPAN_MAX, FXAA_SPAN_MAX)) * rcpFrame;

    // 沿边缘方向的双线采样（3 次混合）
    vec3 rgbA = 0.5 * (
        texture(sceneColor, fragTexCoord + dir * (1.0 / 3.0 - 0.5)).rgb +
        texture(sceneColor, fragTexCoord + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (
        texture(sceneColor, fragTexCoord + dir * -0.5).rgb +
        texture(sceneColor, fragTexCoord + dir *  0.5).rgb);

    // 混合结果越界（跨界到另一条边）时回退到 3 次采样结果
    float lumaB = dot(rgbB, LUMA);
    if ((lumaB < lumaMin) || (lumaB > lumaMax)) {
        outColor = vec4(rgbA, 1.0);
    } else {
        outColor = vec4(rgbB, 1.0);
    }
}
