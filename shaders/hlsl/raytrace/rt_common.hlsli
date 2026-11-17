// =============================================================================
// rt_common.hlsli — GPU 光追共享定义（与 src/raytrace/ CPU 参照实现逐条对应）
//
// 绑定约定（与引擎 DX12 根签名约定一致，见 docs/DX12-RHI-Notes.md §11）：
//   set 0 (space0): b0  RTFrame 常量缓冲
//   set 1 (space1): t0  TLAS  t1 顶点(raw)  t2 索引(raw)  t3 材质(structured)
//                   t4 实例(structured)   t5 灯光(structured)
//                   t6 贴图数组(bindless, 数量由引擎传入)   s0 线性采样器
//   set 2 (space2): 各 pass 的输入 SRV / 输出 UAV（在各自 shader 内声明）
//   push (space3):  b0 PushConstants（layout 数 = 3 时）
// =============================================================================

#ifndef RT_COMMON_HLSLI
#define RT_COMMON_HLSLI

// =============================================================================
// 随机数 — PCG hash（对应 RaytracerTypes.h pcgHash / rand01）
// =============================================================================

uint pcgHash(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float rand01(inout uint rng) {
    rng = pcgHash(rng);
    return float(rng) * (1.0 / 4294967296.0);
}

float randSym(inout uint rng) {
    return rand01(rng) * 2.0 - 1.0;
}

uint pixelRngSeed(uint2 pix, uint frameIndex) {
    return pcgHash(pix.x + pix.y * 7919u + frameIndex * 104729u);
}

// =============================================================================
// 结构体 — 与 C++ 侧 GPURaytracer 上传的字节布局严格一致
// （须先于 rt_bindings.hlsli：其中的资源声明引用这些类型）
// =============================================================================

// 顶点布局: pos(12) normal(12) uv(8) tangent(12) = 44B
#define RT_VERTEX_STRIDE 44

struct RTMaterial {
    float4 albedoMetallic;    // rgb albedo, a metallic
    float4 roughAoEmStrOp;    // r roughness, g ao, b emissiveStrength, a opacity
    float4 emissive;          // rgb emissive color
    int4   texAlbNrmMR;       // x albedoTex, y normalTex, z metallicRoughTex, w aoTex
    int4   texEmisExtra;      // x emissiveTex, yzw unused
};

struct RTInstance {
    uint   materialIndex;
    uint   vertexOffsetBytes;   // 合并大 VB 内本 mesh 起始字节
    uint   indexOffsetBytes;    // 合并大 IB 内本 mesh 起始字节
    uint   _pad;
    float4 nrmRow0;             // 法线矩阵 3x3 行存储（world normal = M * local normal）
    float4 nrmRow1;
    float4 nrmRow2;
};

// 对应 RaytracerTypes.h RTLight
struct RTLight {
    float3 color;
    int    type;              // 0 Directional, 1 Point, 2 Spot
    float3 position;
    float  intensity;
    float3 direction;
    float  range;
    float4 atten;             // x constant, y linear, z quadratic, w innerCone(rad)
    float  outerCone;         // rad
    float3 _pad;
};

struct ShadedMaterial {
    float3 albedo;
    float3 normal;            // 世界空间（含法线贴图，朝向射线来向一侧）
    float3 emissive;          // 已含 emissiveStrength 与贴图
    float  metallic;
    float  roughness;
    float  ao;
};

float3 shadedF0(ShadedMaterial m) {
    return lerp(float3(0.04, 0.04, 0.04), m.albedo, m.metallic);
}

// 有效漫反射因子（对应 ShadedMaterial::diffuseFactor；粗糙金属保留漫反射）
float3 diffuseFactorOf(ShadedMaterial m) {
    float gloss = 1.0 - m.roughness;
    return m.albedo * (1.0 - m.metallic * gloss) * m.ao;
}

// ---- 全局资源（set0 + set1；引用上面的结构体类型）----
#include "rt_bindings.hlsli"

// =============================================================================
// 灯光辐射（对应 RTLight::radiance）
// =============================================================================

static const float FLT_MAX_X = 3.402823466e+38;

float3 lightRadiance(RTLight light, float3 toLight, float distance) {
    float3 result = light.color * light.intensity;
    if (light.type == 0) {
        return result;   // Directional
    }
    float att = 1.0 / (light.atten.x
                     + light.atten.y * distance
                     + light.atten.z * distance * distance);
    if (light.type == 1) {
        // Point：range 外平滑衰减
        if (light.range > 0.0 && distance > light.range) {
            att *= max(0.0, 1.0 - (distance - light.range) / light.range);
        }
        return result * att;
    }
    // Spot
    float cosTheta = dot(-toLight, light.direction);
    float cosOuter = cos(light.outerCone);
    float cosInner = cos(light.atten.w);
    float spot = saturate((cosTheta - cosOuter) / max(cosInner - cosOuter, 1e-6));
    spot *= spot;
    return result * spot * att;
}

// =============================================================================
// 几何/材质取回（对应 SoftwareRaytracer::evaluateShadedMaterial）
// =============================================================================

float3 loadVertexPos(RTInstance inst, uint vi) {
    return asfloat(gVertices.Load3(inst.vertexOffsetBytes + vi * RT_VERTEX_STRIDE));
}
float3 loadVertexNormal(RTInstance inst, uint vi) {
    return asfloat(gVertices.Load3(inst.vertexOffsetBytes + vi * RT_VERTEX_STRIDE + 12));
}
float2 loadVertexUV(RTInstance inst, uint vi) {
    return asfloat(gVertices.Load2(inst.vertexOffsetBytes + vi * RT_VERTEX_STRIDE + 24));
}
float3 loadVertexTangent(RTInstance inst, uint vi) {
    return asfloat(gVertices.Load3(inst.vertexOffsetBytes + vi * RT_VERTEX_STRIDE + 32));
}

uint3 loadTriangleIndices(RTInstance inst, uint primIndex) {
    return uint3(
        gIndices.Load(inst.indexOffsetBytes + primIndex * 12),
        gIndices.Load(inst.indexOffsetBytes + primIndex * 12 + 4),
        gIndices.Load(inst.indexOffsetBytes + primIndex * 12 + 8));
}

float3 transformNormal(RTInstance inst, float3 n) {
    return normalize(float3(
        dot(inst.nrmRow0.xyz, n),
        dot(inst.nrmRow1.xyz, n),
        dot(inst.nrmRow2.xyz, n)));
}

float3 sampleTexRGB(int texIndex, float2 uv) {
    if (texIndex < 0) return float3(1, 1, 1);
    return gTextures[NonUniformResourceIndex(texIndex)].SampleLevel(gLinearSampler, uv, 0).rgb;
}

float sampleTexChannel(int texIndex, float2 uv, int ch) {
    if (texIndex < 0) return 1.0;
    float4 c = gTextures[NonUniformResourceIndex(texIndex)].SampleLevel(gLinearSampler, uv, 0);
    return ch == 0 ? c.r : (ch == 1 ? c.g : c.b);
}

float3 sampleTexNormal(int texIndex, float2 uv) {
    if (texIndex < 0) return float3(0, 0, 1);
    float4 c = gTextures[NonUniformResourceIndex(texIndex)].SampleLevel(gLinearSampler, uv, 0);
    return normalize(c.xyz * 2.0 - 1.0);
}

/// 逐命中材质求值（几何法线已知；primIndex 为 BLAS 局部三角形索引）
ShadedMaterial evalMaterial(uint instanceIdx, uint primIndex, float2 bary, float3 geomNormal, float3 rayDir) {
    RTInstance inst = gInstances[instanceIdx];
    RTMaterial mat = gMaterials[inst.materialIndex];

    ShadedMaterial outM;
    outM.albedo = mat.albedoMetallic.rgb;
    outM.metallic = mat.albedoMetallic.a;
    outM.roughness = mat.roughAoEmStrOp.x;
    outM.ao = mat.roughAoEmStrOp.y;
    outM.emissive = mat.emissive.rgb * mat.roughAoEmStrOp.z;
    outM.normal = geomNormal;

    bool anyTex = mat.texAlbNrmMR.x >= 0 || mat.texAlbNrmMR.y >= 0 || mat.texAlbNrmMR.z >= 0
               || mat.texAlbNrmMR.w >= 0 || mat.texEmisExtra.x >= 0;
    if (anyTex) {
        uint3 idx = loadTriangleIndices(inst, primIndex);
        float w = 1.0 - bary.x - bary.y;
        float2 uv = loadVertexUV(inst, idx.x) * w
                  + loadVertexUV(inst, idx.y) * bary.x
                  + loadVertexUV(inst, idx.z) * bary.y;

        if (mat.texAlbNrmMR.x >= 0) outM.albedo *= sampleTexRGB(mat.texAlbNrmMR.x, uv);
        if (mat.texAlbNrmMR.z >= 0) {
            // glTF metallicRoughness 打包：G=roughness B=metallic（因子相乘）
            outM.metallic *= sampleTexChannel(mat.texAlbNrmMR.z, uv, 2);
            outM.roughness *= sampleTexChannel(mat.texAlbNrmMR.z, uv, 1);
        }
        if (mat.texAlbNrmMR.w >= 0) outM.ao *= sampleTexChannel(mat.texAlbNrmMR.w, uv, 0);
        if (mat.texEmisExtra.x >= 0) outM.emissive *= sampleTexRGB(mat.texEmisExtra.x, uv);
        if (mat.texAlbNrmMR.y >= 0) {
            // tangent-space 法线贴图：TBN（顶点切线插值 + Gram-Schmidt）
            float3 localT = loadVertexTangent(inst, idx.x) * w
                          + loadVertexTangent(inst, idx.y) * bary.x
                          + loadVertexTangent(inst, idx.z) * bary.y;
            float3 T = transformNormal(inst, localT);
            T = T - geomNormal * dot(geomNormal, T);
            float tLen = length(T);
            if (tLen > 1e-5) {
                T /= tLen;
                float3 B = cross(geomNormal, T);
                float3 tn = sampleTexNormal(mat.texAlbNrmMR.y, uv);
                float3 mapped = normalize(T * tn.x + B * tn.y + geomNormal * tn.z);
                // 钳制在几何法线半球内（视角无关）。原实现按射线来向翻转：
                // 掠射视角 + 强法线贴图扰动会把法线翻到几何表面以下，
                // 阴影 ray 起点偏移随之沉入表面 → 地板自交黑斑随相机移动。
                if (dot(mapped, geomNormal) < 0.0) {
                    mapped = normalize(mapped
                        - 2.0 * geomNormal * dot(mapped, geomNormal));
                }
                outM.normal = mapped;
            }
        }
    }

    outM.metallic = clamp(outM.metallic, 0.0, 1.0);
    outM.roughness = clamp(outM.roughness, 0.03, 1.0);
    outM.ao = clamp(outM.ao, 0.0, 1.0);
    return outM;
}

// =============================================================================
// 采样 / Fresnel 工具（对应 RaytracerTypes.h）
// =============================================================================

void orthoBasis(float3 n, out float3 b1, out float3 b2) {
    float3 up = abs(n.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    b1 = normalize(cross(up, n));
    b2 = cross(n, b1);
}

float3 cosineSampleHemisphere(float3 n, inout uint rng) {
    float r1 = rand01(rng);
    float r2 = rand01(rng);
    float phi = 6.2831853 * r1;
    float cosTheta = sqrt(1.0 - r2);
    float sinTheta = sqrt(r2);

    float3 b1, b2;
    orthoBasis(n, b1, b2);
    return normalize(b1 * (sinTheta * cos(phi)) + b2 * (sinTheta * sin(phi)) + n * cosTheta);
}

float3 schlickFresnel(float3 f0, float cosTheta) {
    return f0 + (1.0 - f0) * pow(max(0.0, 1.0 - cosTheta), 5.0);
}

// =============================================================================
// 天光（对应 SoftwareRaytracer::skyColor）
// =============================================================================

float3 skyColor(float3 dir) {
    float t = 0.5 * (dir.y + 1.0);
    float3 horizon = float3(0.75, 0.78, 0.80);
    float3 zenith = float3(0.25, 0.45, 0.80);
    return lerp(horizon, zenith, t) * gSkyIntensity;
}

// =============================================================================
// 相机光线（对应 SoftwareRaytracer.cpp generateRay；DX12 NDC，py 从顶部行 0 开始）
// =============================================================================

void generateRay(uint2 pix, float jx, float jy, out float3 orig, out float3 dir) {
    float ndcX = 2.0 * (pix.x + jx) / gResolution.x - 1.0;
    float ndcY = 1.0 - 2.0 * (pix.y + jy) / gResolution.y;

    float4 nearP = mul(gInvViewProj, float4(ndcX, ndcY, -1.0, 1.0));
    float4 farP = mul(gInvViewProj, float4(ndcX, ndcY, 1.0, 1.0));
    nearP /= nearP.w;
    farP /= farP.w;

    orig = nearP.xyz;
    dir = normalize(farP.xyz - nearP.xyz);
}

// =============================================================================
// ReSTIR（对应 Restir.h Reservoir + SoftwareRaytracer rhoLight*）
// =============================================================================

// 蓄水池以 float4 存储：x=sample（光源索引），y=wSum，z=M，w=W
struct Reservoir {
    uint   sample;
    float  wSum;
    float  M;
    float  W;
};

bool reservoirValid(Reservoir r) { return r.wSum > 0.0 && r.M > 0.0; }

Reservoir reservoirFromFloat4(float4 v) {
    Reservoir r;
    r.sample = uint(v.x);
    r.wSum = v.y;
    r.M = v.z;
    r.W = v.w;
    return r;
}

float4 reservoirToFloat4(Reservoir r) {
    return float4(float(r.sample), r.wSum, r.M, r.W);
}

// 表面信息（从 RT-GBuffer 纹理重建；对应 Restir.h SurfaceInfo）
struct SurfaceInfo {
    float3 position;
    float3 normal;
    float  depth;      // 到相机距离
    float3 albedo;
    float  metallic;
    float  roughness;
    float  ao;
    float3 emissive;
    bool   valid;
};

float3 surfaceDiffuseFactor(SurfaceInfo s) {
    float gloss = 1.0 - s.roughness;
    return s.albedo * (1.0 - s.metallic * gloss) * s.ao;
}

// 目标函数 ρ̂（Lambert 未遮挡贡献；对应 rhoLightRGB / rhoLight）
float3 rhoLightRGB(uint lightIdx, SurfaceInfo surf) {
    if (!surf.valid || lightIdx >= gLightCount) return float3(0, 0, 0);
    RTLight light = gLights[lightIdx];

    float3 toLight;
    float distance;
    if (light.type == 0) {
        toLight = -light.direction;
        distance = FLT_MAX_X;
    } else {
        float3 diff = light.position - surf.position;
        distance = length(diff);
        if (distance < 1e-6) return float3(0, 0, 0);
        toLight = diff / distance;
    }

    float nDotL = dot(surf.normal, toLight);
    if (nDotL <= 0.0) return float3(0, 0, 0);

    return surfaceDiffuseFactor(surf) * (1.0 / 3.14159265)
         * lightRadiance(light, toLight, distance) * nDotL;
}

float rhoLight(uint lightIdx, SurfaceInfo surf) {
    float3 rgb = rhoLightRGB(lightIdx, surf);
    return 0.2126 * rgb.r + 0.7152 * rgb.g + 0.0722 * rgb.b;
}

float luminanceOf(float3 c) {
    return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
}

// =============================================================================
// 可见性（Tier 1.1 inline raytracing；对应 occluded()）
// =============================================================================

bool occludedInline(float3 orig, float3 dir, float tMax) {
    RayDesc ray;
    ray.Origin = orig;
    ray.Direction = dir;
    ray.TMin = 1e-4;
    ray.TMax = tMax;
    RayQuery<0> q;   // RAY_FLAG_NONE：由 flags 决定；提交策略在循环内
    q.TraceRayInline(gTLAS, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, ray);
    while (q.Proceed()) { }
    return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

// 光源几何方向与距离（含方向光）
void lightDirDist(RTLight light, float3 from, out float3 toLight, out float distance) {
    if (light.type == 0) {
        toLight = -light.direction;
        distance = FLT_MAX_X;
    } else {
        float3 diff = light.position - from;
        distance = length(diff);
        toLight = distance > 1e-6 ? diff / distance : float3(0, 1, 0);
    }
}

// 光源是否遮挡（对应 lightShadowed）
bool lightShadowed(uint lightIdx, SurfaceInfo surf) {
    if (lightIdx >= gLightCount) return true;
    RTLight light = gLights[lightIdx];

    float3 toLight;
    float distance;
    if (light.type == 0) {
        toLight = -light.direction;
        distance = FLT_MAX_X;
    } else {
        float3 diff = light.position - surf.position;
        distance = length(diff);
        if (distance < 1e-6) return false;
        toLight = diff / distance;
    }
    return occludedInline(surf.position + surf.normal * gRayEpsilon, toLight,
                          distance - 2.0 * gRayEpsilon);
}

#endif // RT_COMMON_HLSLI
