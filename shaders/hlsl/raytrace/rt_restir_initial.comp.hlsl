// =============================================================================
// rt_restir_initial.comp.hlsl — ReSTIR Phase A：初始候选（对应 restirPhaseInitial）
//
// 读 RT-GBuffer，均匀采样光源 p=1/N，w = ρ̂/p，流式 WRS 蓄水池。
// shadowInTarget 开启时可见性计入目标函数（inline shadow ray）。
//
// set2: t0 pos t1 normal t2 albedo t3 emissive（输入 GBuffer）
//       u10 initialReservoir(RGBA32F)
// =============================================================================

#include "rt_common.hlsli"

Texture2D<float4> gWorldPosTex    : register(t0, space2);
Texture2D<float4> gNormalRoughTex : register(t1, space2);
Texture2D<float4> gAlbedoMetalTex : register(t2, space2);
Texture2D<float4> gEmissiveAoTex  : register(t3, space2);

RWTexture2D<float4> gReservoirOut : register(u10, space2);

[numthreads(8, 8, 1)]
void main(uint2 tid : SV_DispatchThreadID) {
    if (tid.x >= uint(gResolution.x) || tid.y >= uint(gResolution.y)) return;

    uint rng = pixelRngSeed(tid, gFrameIndex);

    // 表面（复用 Phase A 同一种子的两次抖动随机数以对齐 CPU 行为）
    float jx = rand01(rng);
    float jy = rand01(rng);

    float4 pos = gWorldPosTex.Load(int3(tid, 0));
    Reservoir res;
    res.sample = 0; res.wSum = 0; res.M = 0; res.W = 0;

    if (pos.w <= 0.0 || gLightCount == 0) {
        gReservoirOut[tid] = reservoirToFloat4(res);
        return;
    }

    SurfaceInfo surf;
    surf.position = pos.xyz;
    surf.depth = pos.w;
    surf.normal = gNormalRoughTex.Load(int3(tid, 0)).xyz;
    surf.roughness = gNormalRoughTex.Load(int3(tid, 0)).w;
    float4 am = gAlbedoMetalTex.Load(int3(tid, 0));
    surf.albedo = am.rgb;
    surf.metallic = am.a;
    float4 ea = gEmissiveAoTex.Load(int3(tid, 0));
    surf.emissive = ea.rgb;
    surf.ao = ea.a;
    surf.valid = true;

    // 初始候选：均匀采样光源 p=1/N，w = ρ̂/p
    int M = max(1, int(gRestirCandidates));
    bool shadowInTarget = (gShadows != 0) && (gShadowInTarget != 0);

    for (int c = 0; c < M; ++c) {
        uint li = min(gLightCount - 1, uint(rand01(rng) * gLightCount));
        float w = rhoLight(li, surf) * float(gLightCount);
        if (w > 0.0 && shadowInTarget && lightShadowed(li, surf)) {
            w = 0.0;
        }
        // 流式并入
        res.M += 1.0;
        res.wSum += w;
        if (w > 0.0 && rand01(rng) < w / res.wSum) {
            res.sample = li;
        }
    }

    if (reservoirValid(res)) {
        float rhoS = rhoLight(res.sample, surf);
        res.W = res.wSum / (res.M * max(rhoS, 1e-20));
    }

    gReservoirOut[tid] = reservoirToFloat4(res);
}
