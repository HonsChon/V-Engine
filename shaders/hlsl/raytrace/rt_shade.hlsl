// =============================================================================
// rt_shade.hlsl — 着色 pass（对应 restirPhaseShade + tracePathFrom）
//
// 直接光 = 蓄水池估计 W·g(y)·V(y)（firefly clamp 8.0）；
// 间接光 = 路径追踪（bounce0 直接光注入，后续 NEE 2 采样 + Fresnel 分瓣
// + 3 次弹射后 Russian Roulette）。阴影查询走 Tier 1.1 inline raytracing。
//
// SBT 导出：rgShade / msSkyShade / chShade（hit group "hgShade"）
// set2 输入: t0 pos t1 normal t2 albedo t3 emissive t4 finalReservoir
// set2 输出: u0 color(RGBA32F, rgb=radiance, a=luminance)
// =============================================================================

#include "rt_common.hlsli"

Texture2D<float4> gWorldPosTex    : register(t0, space2);
Texture2D<float4> gNormalRoughTex : register(t1, space2);
Texture2D<float4> gAlbedoMetalTex : register(t2, space2);
Texture2D<float4> gEmissiveAoTex  : register(t3, space2);
Texture2D<float4> gReservoirTex   : register(t4, space2);

RWTexture2D<float4> gColor        : register(u10, space2);

struct ShadePayload {
    float3 radiance;    // 回传途中逐级累积
    float3 throughput;  // 当前权重
    uint   rng;
    uint   depth;       // 已跳数（首次递归 = 1；>= gMaxBounces 不再着色）
};

// ---- 从 RT-GBuffer 重建表面 ----
SurfaceInfo loadSurface(uint2 pix) {
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

// ---- NEE：2 光源采样（对应 estimateDirectLight）----
float3 estimateDirectLightNEE(float3 pos, ShadedMaterial sm, inout uint rng) {
    if (gLightCount == 0) return float3(0, 0, 0);

    float3 result = float3(0, 0, 0);
    const int NEE_LIGHT_SAMPLES = 2;

    for (int ls = 0; ls < NEE_LIGHT_SAMPLES; ++ls) {
        uint lightIdx = min(gLightCount - 1, uint(rand01(rng) * gLightCount));
        RTLight light = gLights[lightIdx];
        float lightPdf = 1.0 / float(gLightCount);

        float3 toLight;
        float distance;
        lightDirDist(light, pos, toLight, distance);

        float nDotL = dot(sm.normal, toLight);
        if (nDotL <= 0.0) continue;

        if (gShadows != 0) {
            float shadowT = distance - 2.0 * gRayEpsilon;
            if (occludedInline(pos + sm.normal * gRayEpsilon, toLight, shadowT)) {
                continue;
            }
        }

        float3 f = diffuseFactorOf(sm) * (1.0 / 3.14159265);
        result += f * lightRadiance(light, toLight, distance) * nDotL / lightPdf;
    }
    return result / float(NEE_LIGHT_SAMPLES);
}

[shader("raygeneration")]
void rgShade() {
    uint2 pix = DispatchRaysIndex().xy;

    // 复现 GBuffer pass 的主光线（同一种子的前两次 rand01 即抖动）
    uint rng = pixelRngSeed(pix, gFrameIndex);
    float jx = rand01(rng);
    float jy = rand01(rng);
    float3 orig, dir;
    generateRay(pix, jx, jy, orig, dir);

    SurfaceInfo surf = loadSurface(pix);
    if (!surf.valid) {
        float3 sky = skyColor(dir);
        gColor[pix] = float4(sky, luminanceOf(sky));
        return;
    }

    ShadedMaterial sm;
    sm.albedo = surf.albedo;
    sm.normal = surf.normal;
    sm.emissive = surf.emissive;
    sm.metallic = surf.metallic;
    sm.roughness = surf.roughness;
    sm.ao = surf.ao;

    // ---- ReSTIR 直接光：W × g(y) × V(y)（firefly clamp 8.0）----
    float3 direct = float3(0, 0, 0);
    Reservoir res = reservoirFromFloat4(gReservoirTex.Load(int3(pix, 0)));
    if (reservoirValid(res) && res.W > 0.0) {
        float3 rgb = rhoLightRGB(res.sample, surf);
        if (dot(rgb, rgb) > 0.0 && (gShadows == 0 || !lightShadowed(res.sample, surf))) {
            direct = res.W * rgb;
            float lum = luminanceOf(direct);
            const float DIRECT_CLAMP = 8.0;
            if (lum > DIRECT_CLAMP) direct *= DIRECT_CLAMP / lum;
        }
    }

    // ---- tracePathFrom 移植：bounce0 = GBuffer 表面 ----
    float3 radiance = sm.emissive + direct;

    // BSDF 采样（bounce0）
    float cosTheta = dot(sm.normal, -dir);
    float3 fresnel = schlickFresnel(shadedF0(sm), cosTheta);
    float gloss = 1.0 - sm.roughness;
    float specProb = clamp(luminanceOf(fresnel) * (0.1 + 0.9 * gloss * gloss), 0.05, 0.95);

    uint rngS = pcgHash(pixelRngSeed(pix, gFrameIndex));

    float3 newDir;
    float3 beta;   // 本段吞吐
    if (rand01(rngS) < specProb) {
        newDir = reflect(dir, sm.normal);
        float rough = sm.roughness * sm.roughness;
        if (rough > 1e-3) {
            float3 jitter = float3(randSym(rngS), randSym(rngS), randSym(rngS));
            newDir += jitter * rough;
        }
        newDir = normalize(newDir);
        if (dot(newDir, sm.normal) <= 0.0) {
            // 反射进表面 → 终止
            gColor[pix] = float4(radiance, luminanceOf(radiance));
            return;
        }
        beta = fresnel / specProb;
    } else {
        newDir = cosineSampleHemisphere(sm.normal, rngS);
        beta = sm.albedo * (1.0 - sm.metallic * gloss) * sm.ao / (1.0 - specProb);
    }

    ShadePayload payload;
    payload.radiance = float3(0, 0, 0);
    payload.throughput = beta;
    payload.rng = rngS;
    payload.depth = 1;

    RayDesc ray;
    ray.Origin = surf.position + sm.normal * gRayEpsilon;
    ray.Direction = newDir;
    ray.TMin = 1e-4;
    ray.TMax = 1e30;
    TraceRay(gTLAS, RAY_FLAG_NONE, 0xFF, 1 /*hit group 1*/,
             0 /*geometry stride 1*/, 0 /*miss: sky*/, ray, payload);

    radiance += payload.radiance;
    gColor[pix] = float4(radiance, luminanceOf(radiance));
}

[shader("closesthit")]
void chShade(inout ShadePayload payload, in BuiltInTriangleIntersectionAttributes attr) {
    // 深度上限（对应 CPU 循环 bounce < maxBounces）
    if (payload.depth >= gMaxBounces) return;

    float3 rayDir = WorldRayDirection();
    float3 pos = WorldRayOrigin() + rayDir * RayTCurrent();

    // 几何法线：顶点插值（局部）→ 实例法线矩阵（世界）→ 朝向射线来向
    RTInstance inst = gInstances[InstanceIndex()];
    {
        uint3 idx = loadTriangleIndices(inst, PrimitiveIndex());
        float w = 1.0 - attr.barycentrics.x - attr.barycentrics.y;
        float3 localN = loadVertexNormal(inst, idx.x) * w
                      + loadVertexNormal(inst, idx.y) * attr.barycentrics.x
                      + loadVertexNormal(inst, idx.z) * attr.barycentrics.y;
        float3 n = transformNormal(inst, localN);
        if (dot(n, -rayDir) < 0.0) n = -n;

        ShadedMaterial sm = evalMaterial(InstanceIndex(), PrimitiveIndex(),
                                         attr.barycentrics, n, rayDir);
        sm.normal = n;

        // 自发光
        payload.radiance += payload.throughput * sm.emissive;

        // NEE（2 光源采样）
        payload.radiance += payload.throughput * estimateDirectLightNEE(pos, sm, payload.rng);

        // Russian Roulette：3 次弹射后按吞吐量概率终止（CPU 在到达第 4+ 个顶点时检查）
        if (payload.depth > 3) {
            float pCont = clamp(max(payload.throughput.r,
                                    max(payload.throughput.g, payload.throughput.b)), 0.05, 0.95);
            if (rand01(payload.rng) > pCont) return;
            payload.throughput /= pCont;
        }

        // ---- BSDF 采样下一条光线 ----
        float cosTheta = dot(sm.normal, -rayDir);
        float3 fresnel = schlickFresnel(shadedF0(sm), cosTheta);
        float gloss = 1.0 - sm.roughness;
        float specProb = clamp(luminanceOf(fresnel) * (0.1 + 0.9 * gloss * gloss), 0.05, 0.95);

        float3 newDir;
        if (rand01(payload.rng) < specProb) {
            newDir = reflect(rayDir, sm.normal);
            float rough = sm.roughness * sm.roughness;
            if (rough > 1e-3) {
                float3 jitter = float3(randSym(payload.rng), randSym(payload.rng), randSym(payload.rng));
                newDir += jitter * rough;
            }
            newDir = normalize(newDir);
            if (dot(newDir, sm.normal) <= 0.0) return;   // 反射进表面 → 终止
            payload.throughput *= fresnel / specProb;
        } else {
            newDir = cosineSampleHemisphere(sm.normal, payload.rng);
            payload.throughput *= sm.albedo * (1.0 - sm.metallic * gloss) * sm.ao / (1.0 - specProb);
        }

        payload.depth += 1;

        RayDesc ray;
        ray.Origin = pos + sm.normal * gRayEpsilon;
        ray.Direction = newDir;
        ray.TMin = 1e-4;
        ray.TMax = 1e30;
        TraceRay(gTLAS, RAY_FLAG_NONE, 0xFF, 1 /*hit group 1*/,
                 0 /*geometry stride 1*/, 0 /*miss: sky*/, ray, payload);
    }
}

[shader("miss")]
void msSkyShade(inout ShadePayload payload) {
    payload.radiance += payload.throughput * skyColor(WorldRayDirection());
}
