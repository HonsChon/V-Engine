// =============================================================================
// rt_svgf_atrous.comp.hlsl — SVGF à-trous 联合双边滤波（对应 denoiseFilterBlock）
//
// 5×5 二项式核 × step 间距；边缘权重：法线 pow(n,32) + 深度高斯 +
// SVGF 方差自适应颜色权重（相对亮度上限钳制；参数 G=1.33 CAP=0.15 POW=1.0
// 与 CPU 默认一致）。天空像素直通。
//
// set2: t0 colorIn(rgb) t1 varIn(R32F) t2 pos t3 normal
//       u0 colorOut  u1 varOut（直通拷贝，保持绑定期望）
// =============================================================================

#include "rt_common.hlsli"

Texture2D<float4> gColorIn     : register(t0, space2);
Texture2D<float>  gVarIn       : register(t1, space2);
Texture2D<float4> gWorldPosTex : register(t2, space2);
Texture2D<float4> gNormalTex   : register(t3, space2);

RWTexture2D<float4> gColorOut : register(u10, space2);
RWTexture2D<float>  gVarOut   : register(u11, space2);

static const float spatialKernel[5][5] = {
    { 1.0 / 256.0,  4.0 / 256.0,  6.0 / 256.0,  4.0 / 256.0, 1.0 / 256.0 },
    { 4.0 / 256.0, 16.0 / 256.0, 24.0 / 256.0, 16.0 / 256.0, 4.0 / 256.0 },
    { 6.0 / 256.0, 24.0 / 256.0, 36.0 / 256.0, 24.0 / 256.0, 6.0 / 256.0 },
    { 4.0 / 256.0, 16.0 / 256.0, 24.0 / 256.0, 16.0 / 256.0, 4.0 / 256.0 },
    { 1.0 / 256.0,  4.0 / 256.0,  6.0 / 256.0,  4.0 / 256.0, 1.0 / 256.0 },
};

[numthreads(8, 8, 1)]
void main(uint2 tid : SV_DispatchThreadID) {
    if (tid.x >= uint(gResolution.x) || tid.y >= uint(gResolution.y)) return;

    float3 c0 = gColorIn.Load(int3(tid, 0)).rgb;
    // 注：不写 gVarOut（方差由 variance pass 维护，à-trous 仅直通——
    // 避免同纹理 SRV/UAV 读写冲突）；u11 槽位保留给布局契约。

    float4 pos0 = gWorldPosTex.Load(int3(tid, 0));
    if (pos0.w <= 0.0) {
        gColorOut[tid] = float4(c0, 0.0);   // 天空确定性 → 不滤
        return;
    }
    float3 n0 = gNormalTex.Load(int3(tid, 0)).xyz;
    float depth0 = pos0.w;

    // ---- SVGF 颜色权重（方差自适应 + 相对亮度上限钳制）----
    const float svgfG = 1.33;
    const float svgfCap = 0.15;
    const float svgfPow = 1.0;
    float var = gVarIn.Load(int3(tid, 0));
    float lum0 = luminanceOf(c0);
    float stepScale = pow(float(gStep), svgfPow);
    float capLum = svgfCap * max(lum0, 1e-6);
    float sigmaC2 = min(svgfG * var * stepScale, capLum * capLum) + 1e-8;
    float invTwoSigmaC2 = 1.0 / (2.0 * sigmaC2);

    // 深度容忍度随步长放大
    float sigmaDepth = 0.02 * depth0 * float(gStep);
    float invTwoSigmaD2 = 1.0 / (2.0 * sigmaDepth * sigmaDepth + 1e-20);

    float3 sum = float3(0, 0, 0);
    float wsum = 0.0;

    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            int nx = int(tid.x) + dx * int(gStep);
            int ny = int(tid.y) + dy * int(gStep);
            if (nx < 0 || ny < 0 || nx >= int(gResolution.x) || ny >= int(gResolution.y)) continue;

            uint2 np = uint2(nx, ny);
            float4 pos1 = gWorldPosTex.Load(int3(np, 0));
            if (pos1.w <= 0.0) continue;   // 不跨天空边界

            float3 n1 = gNormalTex.Load(int3(np, 0)).xyz;

            // 法线权重（幂次锐化）
            float nDot = max(0.0, dot(n0, n1));
            float wn = pow(nDot, 32.0);

            // 深度权重（高斯）
            float dd = pos1.w - depth0;
            float wd = exp(-dd * dd * invTwoSigmaD2);

            // 颜色权重（SVGF 核心）
            float3 c1 = gColorIn.Load(int3(np, 0)).rgb;
            float dl = luminanceOf(c1) - lum0;
            float wc = exp(-dl * dl * invTwoSigmaC2);

            float w = spatialKernel[dy + 2][dx + 2] * wn * wd * wc;
            if (w <= 0.0) continue;

            sum += c1 * w;
            wsum += w;
        }
    }

    gColorOut[tid] = wsum > 0.0 ? float4(sum / wsum, 0.0) : float4(c0, 0.0);
}
