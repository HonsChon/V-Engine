// =============================================================================
// rt_gbuffer.hlsl — RT-GBuffer pass（对应 restirPhaseInitial 的主命中部分）
//
// 主光线 → 世界位置/法线/反照率/材质参数/自发光 GBuffer。
// miss 时 gWorldPos.xyz = 天空色、w = 0（w>0 表示有效命中，w 同时是相机距离）。
//
// SBT 导出：rgGBuffer / msSky / chGBuffer（hit group "hgGBuffer"）
// set2: u0 worldPos(RGBA32F) u1 normal+rough(RGBA16F) u2 albedo+metal(RGBA8)
//       u3 emissive+ao(RGBA16F)
// =============================================================================

#include "rt_common.hlsli"

RWTexture2D<float4> gWorldPos      : register(u0, space2);
RWTexture2D<float4> gNormalRough   : register(u1, space2);
RWTexture2D<float4> gAlbedoMetal   : register(u2, space2);
RWTexture2D<float4> gEmissiveAo    : register(u3, space2);

struct HitPayload {
    float3 normal;      // 插值几何法线（世界空间）
    float  hitT;        // >0 命中（世界 t）；miss shader 置 -1
    float2 bary;
    uint   instanceIdx;
    uint   primIdx;
};

[shader("raygeneration")]
void rgGBuffer() {
    uint2 pix = DispatchRaysIndex().xy;
    uint rng = pixelRngSeed(pix, gFrameIndex);
    float jx = rand01(rng);
    float jy = rand01(rng);

    float3 orig, dir;
    generateRay(pix, jx, jy, orig, dir);

    RayDesc ray;
    ray.Origin = orig;
    ray.Direction = dir;
    ray.TMin = 1e-4;
    ray.TMax = 1e30;

    HitPayload payload = { float3(0, 1, 0), -1.0, float2(0, 0), 0, 0 };
    TraceRay(gTLAS, RAY_FLAG_NONE, 0xFF, 0 /*hit group*/,
             0 /*geometry stride*/, 0 /*miss: sky*/, ray, payload);

    if (payload.hitT <= 0.0) {
        gWorldPos[pix] = float4(skyColor(dir), 0.0);
        gNormalRough[pix] = float4(0, 0, 0, 1);
        gAlbedoMetal[pix] = float4(0, 0, 0, 0);
        gEmissiveAo[pix] = float4(0, 0, 0, 1);
        return;
    }

    float3 worldPos = orig + dir * payload.hitT;
    ShadedMaterial mat = evalMaterial(payload.instanceIdx, payload.primIdx,
                                      payload.bary, payload.normal, dir);

    float3 n = mat.normal;
    // 双面几何：仅当几何法线背向相机时翻转。判定不使用法线贴图扰动结果
    // （扰动后的法线仍保持在几何半球内，由 evalMaterial 保证）。
    if (dot(payload.normal, -dir) < 0.0) n = -n;

    float depth = length(worldPos - gCamPos);
    gWorldPos[pix] = float4(worldPos, depth);
    gNormalRough[pix] = float4(n, mat.roughness);
    gAlbedoMetal[pix] = float4(mat.albedo, mat.metallic);
    gEmissiveAo[pix] = float4(mat.emissive, mat.ao);
}

[shader("closesthit")]
void chGBuffer(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr) {
    payload.hitT = RayTCurrent();
    payload.instanceIdx = InstanceIndex();
    payload.primIdx = PrimitiveIndex();

    // 几何法线：插值顶点法线（局部空间）→ 实例法线矩阵 → 世界
    RTInstance inst = gInstances[InstanceIndex()];
    uint3 idx = loadTriangleIndices(inst, PrimitiveIndex());
    float w = 1.0 - attr.barycentrics.x - attr.barycentrics.y;
    float3 localN = loadVertexNormal(inst, idx.x) * w
                  + loadVertexNormal(inst, idx.y) * attr.barycentrics.x
                  + loadVertexNormal(inst, idx.z) * attr.barycentrics.y;
    payload.normal = transformNormal(inst, localN);
    payload.bary = attr.barycentrics;
}

[shader("miss")]
void msSky(inout HitPayload payload) {
    payload.hitT = -1.0;
}
