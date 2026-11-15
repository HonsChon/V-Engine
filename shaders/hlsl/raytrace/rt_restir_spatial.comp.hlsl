// =============================================================================
// rt_restir_spatial.comp.hlsl — ReSTIR Phase B：空间复用（对应 restirPhaseSpatial）
//
// 自身 + K 个随机邻居（±4..12px，平面距离/法线拒绝），无偏 shift-corrected
// 合并（对应 gatherMergeReservoirs）。
//
// set2: t0 pos t1 normal t2 albedo t3 emissive t4 initialReservoir
//       u0 spatialReservoir(RGBA32F)
// =============================================================================

#include "rt_common.hlsli"

Texture2D<float4> gWorldPosTex    : register(t0, space2);
Texture2D<float4> gNormalRoughTex : register(t1, space2);
Texture2D<float4> gAlbedoMetalTex : register(t2, space2);
Texture2D<float4> gEmissiveAoTex  : register(t3, space2);
Texture2D<float4> gReservoirIn    : register(t4, space2);

RWTexture2D<float4> gReservoirOut : register(u10, space2);

SurfaceInfo loadSurfaceAt(uint2 pix) {
    SurfaceInfo s;
    float4 pos = gWorldPosTex.Load(int3(pix, 0));
    s.valid = pos.w > 0.0;
    s.position = pos.xyz;
    s.depth = pos.w;
    float4 nr = gNormalRoughTex.Load(int3(pix, 0));
    s.normal = nr.xyz;
    s.roughness = nr.w;
    float4 am = gAlbedoMetalTex.Load(int3(pix, 0));
    s.albedo = am.rgb;
    s.metallic = am.a;
    float4 ea = gEmissiveAoTex.Load(int3(pix, 0));
    s.emissive = ea.rgb;
    s.ao = ea.a;
    return s;
}

[numthreads(8, 8, 1)]
void main(uint2 tid : SV_DispatchThreadID) {
    if (tid.x >= uint(gResolution.x) || tid.y >= uint(gResolution.y)) return;

    uint rng = pixelRngSeed(tid, gFrameIndex * 31u + 7u);

    Reservoir outRes;
    outRes.sample = 0; outRes.wSum = 0; outRes.M = 0; outRes.W = 0;

    SurfaceInfo surf = loadSurfaceAt(tid);
    Reservoir selfRes = reservoirFromFloat4(gReservoirIn.Load(int3(tid, 0)));
    if (!surf.valid || !reservoirValid(selfRes)) {
        gReservoirOut[tid] = reservoirToFloat4(outRes);
        return;
    }

    // 候选集合：自身 + K 个随机邻居（最多 9 个，与 CPU 一致）
    int K = clamp(int(gRestirSpatialNeighbors), 0, 8);

    // 逐候选合并（无偏 shift-corrected merge；几何拒绝后再并入）
    // CPU 版先收集数组再合并，此处逐个处理数学等价：
    //   w_r = wSum_r × ρ̂_t(y_r)/ρ̂_r(y_r)，M' = ΣM_r
    {
        // 自身（shift = 1：直接并入）
        float rhoAtTarget = rhoLight(selfRes.sample, surf);
        if (rhoAtTarget > 0.0) {
            float w = selfRes.wSum;
            outRes.M += selfRes.M;
            outRes.wSum += w;
            if (w > 0.0 && rand01(rng) < w / outRes.wSum) outRes.sample = selfRes.sample;
        }
    }

    for (int k = 0; k < K; ++k) {
        // 偏移 ±4..±12 px
        int dx = int(rand01(rng) * 9.0) + 4;
        int dy = int(rand01(rng) * 9.0) + 4;
        if (rand01(rng) < 0.5) dx = -dx;
        if (rand01(rng) < 0.5) dy = -dy;
        int nx = int(tid.x) + dx;
        int ny = int(tid.y) + dy;
        if (nx < 0 || ny < 0 || nx >= int(gResolution.x) || ny >= int(gResolution.y)) continue;

        uint2 ntid = uint2(nx, ny);
        Reservoir nr = reservoirFromFloat4(gReservoirIn.Load(int3(ntid, 0)));
        SurfaceInfo ns = loadSurfaceAt(ntid);
        if (!ns.valid || !reservoirValid(nr)) continue;

        // 几何相似性拒绝（与样本值无关 → 不影响无偏性）
        float3 dp = ns.position - surf.position;
        float planeDist = abs(dot(surf.normal, dp));
        bool depthOK = (abs(ns.depth - surf.depth) < 0.2 * surf.depth)
                    || (planeDist < 0.1 * surf.depth);
        if (!depthOK) continue;
        if (dot(ns.normal, surf.normal) < 0.75) continue;

        // 无偏合并：shift 因子 ρ̂_t/ρ̂_r
        float rhoAtTarget = rhoLight(nr.sample, surf);
        if (rhoAtTarget <= 0.0) continue;
        float rhoOwn = rhoLight(nr.sample, ns);
        if (rhoOwn <= 0.0) continue;

        float w = nr.wSum * (rhoAtTarget / rhoOwn);
        outRes.M += nr.M;
        outRes.wSum += w;
        if (w > 0.0 && rand01(rng) < w / outRes.wSum) outRes.sample = nr.sample;
    }

    // M 跨帧累积上限
    if (outRes.M > 1e6) outRes.M = 1e6;

    if (reservoirValid(outRes)) {
        float rhoS = rhoLight(outRes.sample, surf);
        outRes.W = outRes.wSum / (outRes.M * max(rhoS, 1e-20));
    }

    gReservoirOut[tid] = reservoirToFloat4(outRes);
}
