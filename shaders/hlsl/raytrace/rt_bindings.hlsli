// =============================================================================
// rt_bindings.hlsli — 全局资源声明（set0 帧常量 + set1 场景资源）
//
// 与 C++ 侧 GPURayTracer 的绑定布局一一对应：
//   layout 0 (space0): b0 UBO
//   layout 1 (space1): t0 TLAS / t1-t2 顶点索引(raw) / t3 材质 / t4 实例 /
//                      t5 灯光 / t6 贴图数组(count=N) / s0 采样器
// =============================================================================

#ifndef RT_BINDINGS_HLSLI
#define RT_BINDINGS_HLSLI

// ---- 帧常量（root constants，b0 space3 = layout 数 3）----
// 注：曾用 descriptor-table CBV（b0 space0），在部分驱动上 cbuffer 高位寄存器
// （reg10+）读到未定义数据导致死循环挂起；root constants 通路经全量验证可靠，
// 故整个 RTFrame 走 push constants（224B = 56 DWORD，root sig 限额内）。
cbuffer RTFrame : register(b0, space3) {
    float4x4 gInvViewProj;      // 本帧 inverse(view*proj)
    float4x4 gPrevViewProj;     // 上一帧 view*proj（时间复用重投影）
    float3   gCamPos;
    uint     gFrameIndex;
    float2   gResolution;       // RT 分辨率
    uint     gLightCount;
    float    gSkyIntensity;
    uint     gMaxBounces;
    float    gRayEpsilon;       // 1e-4
    uint     gRestirCandidates;      // M
    uint     gRestirSpatialNeighbors;// K
    uint     gRestirTemporal;        // 0/1
    uint     gShadows;               // 0/1
    uint     gShadowInTarget;        // 0/1（可见性计入目标函数）
    float    gPrevCamPad;            // (对齐占位)
    float3   gPrevCamPos;
    uint     gHistoryValid;     // 时间复用历史是否有效
    // ---- 各 compute pass 的通用参数（原独立 PushConstants 合并）----
    uint     gStep;             // à-trous 步长（1/2/4）
    uint     gReset;            // SVGF 历史重置
    float    gExposure;         // 曝光
    float    gPushPad;
}

// ---- set 1：场景资源 ----
RaytracingAccelerationStructure gTLAS      : register(t0, space1);
ByteAddressBuffer               gVertices  : register(t1, space1);
ByteAddressBuffer               gIndices   : register(t2, space1);
StructuredBuffer<RTMaterial>    gMaterials : register(t3, space1);
StructuredBuffer<RTInstance>    gInstances : register(t4, space1);
StructuredBuffer<RTLight>       gLights    : register(t5, space1);
Texture2D<float4>               gTextures[] : register(t6, space1);
// 采样器在 layout1 的 binding 7（t0-t6 被 SRV 占用，绑定号需唯一）→ s7
SamplerState                    gLinearSampler : register(s7, space1);

#endif // RT_BINDINGS_HLSLI
