// =============================================================================
// rt_restir_temporal.comp.hlsl — ReSTIR Phase C：时间复用（对应 restirPhaseTemporal）
//
// 重投影（prevViewProj）→ 深度/法线连续性验证 → GRIS 合并（spatial + prev）。
// 同时把当前 GBuffer（pos/normal）与最终蓄水池写入历史纹理（对应 Phase E）。
//
// set2: t0 pos t1 normal t2 albedo t3 emissive t4 spatialReservoir
//       t5 prevPos t6 prevNormal t7 prevReservoir
//       u0 finalReservoir u1 prevPosOut u2 prevNormalOut u3 prevReservoirOut
// =============================================================================

#include "rt_common.hlsli"

Texture2D<float4> gWorldPosTex    : register(t0, space2);
Texture2D<float4> gNormalRoughTex : register(t1, space2);
Texture2D<float4> gAlbedoMetalTex : register(t2, space2);
Texture2D<float4> gEmissiveAoTex  : register(t3, space2);
Texture2D<float4> gReservoirIn    : register(t4, space2);
Texture2D<float4> gPrevPosTex     : register(t5, space2);
Texture2D<float4> gPrevNormalTex  : register(t6, space2);
Texture2D<float4> gPrevReservoir  : register(t7, space2);

RWTexture2D<float4> gReservoirOut    : register(u10, space2);
RWTexture2D<float4> gPrevPosOut      : register(u11, space2);
RWTexture2D<float4> gPrevNormalOut   : register(u12, space2);
RWTexture2D<float4> gPrevReservoirOut: register(u13, space2);

[numthreads(8, 8, 1)]
void main(uint2 tid : SV_DispatchThreadID) {
    if (tid.x >= uint(gResolution.x) || tid.y >= uint(gResolution.y)) return;

    uint rng = pixelRngSeed(tid, gFrameIndex * 131u + 17u);

    float4 pos = gWorldPosTex.Load(int3(tid, 0));
    float4 nrm = gNormalRoughTex.Load(int3(tid, 0));
    Reservoir spatial = reservoirFromFloat4(gReservoirIn.Load(int3(tid, 0)));

    // 历史旁路输出（无条件写：下一帧的 prev）
    gPrevPosOut[tid] = pos;
    gPrevNormalOut[tid] = nrm;

    Reservoir outRes;
    outRes.sample = 0; outRes.wSum = 0; outRes.M = 0; outRes.W = 0;

    if (!reservoirValid(spatial)) {
        gReservoirOut[tid] = reservoirToFloat4(outRes);
        gPrevReservoirOut[tid] = reservoirToFloat4(outRes);
        return;
    }
    outRes = spatial;

    SurfaceInfo surf;
    surf.position = pos.xyz;
    surf.depth = pos.w;
    surf.normal = nrm.xyz;
    surf.roughness = nrm.w;
    float4 am = gAlbedoMetalTex.Load(int3(tid, 0));
    surf.albedo = am.rgb;
    surf.metallic = am.a;
    float4 ea = gEmissiveAoTex.Load(int3(tid, 0));
    surf.emissive = ea.rgb;
    surf.ao = ea.a;
    surf.valid = pos.w > 0.0;

    // 无历史 / 关闭时间复用 / 天空像素 → 直通
    if (gRestirTemporal == 0 || gHistoryValid == 0 || !surf.valid) {
        gReservoirOut[tid] = reservoirToFloat4(outRes);
        gPrevReservoirOut[tid] = reservoirToFloat4(outRes);
        return;
    }

    // 重投影：当前表面点 → 上一帧屏幕坐标
    float4 clip = mul(gPrevViewProj, float4(surf.position, 1.0));
    if (clip.w <= 0.0) {
        gReservoirOut[tid] = reservoirToFloat4(outRes);
        gPrevReservoirOut[tid] = reservoirToFloat4(outRes);
        return;
    }
    float3 ndc = clip.xyz / clip.w;
    if (ndc.x < -1.0 || ndc.x > 1.0 || ndc.y < -1.0 || ndc.y > 1.0) {
        gReservoirOut[tid] = reservoirToFloat4(outRes);
        gPrevReservoirOut[tid] = reservoirToFloat4(outRes);
        return;
    }
    // DX12 NDC：y=+1 顶部 → 行序 top-down
    float fx = (ndc.x + 1.0) * 0.5 * gResolution.x;
    float fy = (1.0 - ndc.y) * 0.5 * gResolution.y;
    int nx = clamp(int(fx), 0, int(gResolution.x) - 1);
    int ny = clamp(int(fy), 0, int(gResolution.y) - 1);
    uint2 ntid = uint2(nx, ny);

    float4 prevPos = gPrevPosTex.Load(int3(ntid, 0));
    float3 prevNormal = gPrevNormalTex.Load(int3(ntid, 0)).xyz;
    Reservoir prevRes = reservoirFromFloat4(gPrevReservoir.Load(int3(ntid, 0)));

    if (prevPos.w <= 0.0 || !reservoirValid(prevRes)) {
        gReservoirOut[tid] = reservoirToFloat4(outRes);
        gPrevReservoirOut[tid] = reservoirToFloat4(outRes);
        return;
    }

    // 深度/法线连续性验证（拒绝不连续表面，避免拖影）
    float dPrev = length(surf.position - gPrevCamPos);
    if (abs(dPrev - prevPos.w) > 0.1 * prevPos.w) {
        gReservoirOut[tid] = reservoirToFloat4(outRes);
        gPrevReservoirOut[tid] = reservoirToFloat4(outRes);
        return;
    }
    if (dot(prevNormal, surf.normal) < 0.9) {
        gReservoirOut[tid] = reservoirToFloat4(outRes);
        gPrevReservoirOut[tid] = reservoirToFloat4(outRes);
        return;
    }

    // GRIS 合并：本帧 spatial + 上一帧蓄水池（目标 = 当前表面）
    SurfaceInfo prevSurf = surf;   // ρ̂_r 用上一帧表面参数（法线/漫反射可能略异）
    prevSurf.position = prevPos.xyz;
    prevSurf.depth = prevPos.w;
    prevSurf.normal = prevNormal;

    Reservoir merged;
    merged.sample = 0; merged.wSum = 0; merged.M = 0; merged.W = 0;

    // 自身（shift = 1）
    if (rhoLight(spatial.sample, surf) > 0.0) {
        float w = spatial.wSum;
        merged.M += spatial.M;
        merged.wSum += w;
        if (w > 0.0 && rand01(rng) < w / merged.wSum) merged.sample = spatial.sample;
    }
    // 上一帧（shift 校正）
    float rhoAtTarget = rhoLight(prevRes.sample, surf);
    float rhoOwn = rhoLight(prevRes.sample, prevSurf);
    if (rhoAtTarget > 0.0 && rhoOwn > 0.0) {
        float w = prevRes.wSum * (rhoAtTarget / rhoOwn);
        merged.M += prevRes.M;
        merged.wSum += w;
        if (w > 0.0 && rand01(rng) < w / merged.wSum) merged.sample = prevRes.sample;
    }

    if (merged.M > 1e6) merged.M = 1e6;

    if (reservoirValid(merged)) {
        float rhoS = rhoLight(merged.sample, surf);
        merged.W = merged.wSum / (merged.M * max(rhoS, 1e-20));
        outRes = merged;
    }

    gReservoirOut[tid] = reservoirToFloat4(outRes);
    gPrevReservoirOut[tid] = reservoirToFloat4(outRes);
}
