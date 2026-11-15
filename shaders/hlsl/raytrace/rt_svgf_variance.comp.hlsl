// =============================================================================
// rt_svgf_variance.comp.hlsl — SVGF 方差估计（对应 denoiseHDR 的方差初始化）
//
// GPU 1spp 无跨帧累积：reset（相机/场景变化）时用 3×3 空间方差初始化，
// 平稳帧用时间 EMA（(lum - mean_prev)²，SVGF 惯例）；随后 3×3 平滑。
// 之后 3 层 à-trous 使用该方差（与 CPU 行为一致：方差不随迭代更新）。
//
// set2: t0 color(rgb+lum) t1 prevColor(rgb+EMA mean) t2 prevVar(R32F) t3 pos(valid)
//       u0 colorOut(rgb + EMA mean) u1 varOut(R32F)
// =============================================================================

#include "rt_common.hlsli"

Texture2D<float4> gColorIn     : register(t0, space2);
Texture2D<float4> gPrevColor   : register(t1, space2);
Texture2D<float>  gPrevVar     : register(t2, space2);
Texture2D<float4> gWorldPosTex : register(t3, space2);

RWTexture2D<float4> gColorOut : register(u10, space2);
RWTexture2D<float>  gVarOut   : register(u11, space2);

[numthreads(8, 8, 1)]
void main(uint2 tid : SV_DispatchThreadID) {
    if (tid.x >= uint(gResolution.x) || tid.y >= uint(gResolution.y)) return;

    float4 c = gColorIn.Load(int3(tid, 0));
    float lum = luminanceOf(c.rgb);

    // ---- 时间 EMA / 空间初始化 ----
    float mean, var;
    if (gReset != 0) {
        mean = lum;
        // 3×3 空间方差（仅同有效表面）
        float4 pos0 = gWorldPosTex.Load(int3(tid, 0));
        bool valid0 = pos0.w > 0.0;
        float sum = 0.0, sumSq = 0.0;
        int n = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                int nx = int(tid.x) + dx;
                int ny = int(tid.y) + dy;
                if (nx < 0 || ny < 0 || nx >= int(gResolution.x) || ny >= int(gResolution.y)) continue;
                uint2 np = uint2(nx, ny);
                if (gWorldPosTex.Load(int3(np, 0)).w <= 0.0 && valid0) continue;
                float l = luminanceOf(gColorIn.Load(int3(np, 0)).rgb);
                sum += l;
                sumSq += l * l;
                ++n;
            }
        }
        float invN = 1.0 / max(n, 1);
        float e = sum * invN;
        float e2 = sumSq * invN;
        var = max(e2 - e * e, 0.0);
    } else {
        float prevMean = gPrevColor.Load(int3(tid, 0)).a;
        float prevVar = gPrevVar.Load(int3(tid, 0));
        const float ALPHA = 0.2;
        mean = lerp(prevMean, lum, ALPHA);
        var = lerp(prevVar, (lum - prevMean) * (lum - prevMean), ALPHA);
    }

    gColorOut[tid] = float4(c.rgb, mean);

    // 注：CPU 版此处另有方差 3×3 预平滑；GPU 单 pass 无法读改写邻域，且
    // EMA/空间初始化本身已平滑，直接写入（差异点，见 docs）。
    gVarOut[tid] = var;
}
