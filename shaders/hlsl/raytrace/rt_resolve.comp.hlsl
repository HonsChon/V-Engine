// =============================================================================
// rt_resolve.comp.hlsl — 输出解析（对应 Framebuffer::tonemapRGBA8）
//
// 曝光 × ACES(Narkowicz) × gamma 2.2 → RGBA8。
//
// set2: t0 colorIn(rgb)   u10 outColor(RGBA8)
// =============================================================================

#include "rt_common.hlsli"

Texture2D<float4> gColorIn : register(t0, space2);
RWTexture2D<unorm float4> gOutColor : register(u10, space2);

float3 acesTonemap(float3 x) {
    x *= 0.6;
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14),
                 0.0, 1.0);
}

[numthreads(8, 8, 1)]
void main(uint2 tid : SV_DispatchThreadID) {
    if (tid.x >= uint(gResolution.x) || tid.y >= uint(gResolution.y)) return;

    float3 c = gColorIn.Load(int3(tid, 0)).rgb * gExposure;
    c = acesTonemap(c);
    // gamma 2.2
    c = pow(max(c, 0.0), 1.0 / 2.2);
    gOutColor[tid] = float4(c, 1.0);
}
