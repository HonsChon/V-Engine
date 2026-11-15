/**
 * @file GPURayTracer.h
 * @brief GPU 硬件光追（DXR）— RT-GBuffer + ReSTIR DI + 1spp PT + SVGF
 *
 * 与 CPU 版（src/raytrace/SoftwareRaytracer，Algorithm::PathTracingRestir）逐阶段
 * 对应的 GPU 实现，仅 DX12 后端（Vulkan 未实现 VK_KHR_ray_tracing_pipeline）。
 *
 * 帧管线（分辨率吃 rtResolutionScale）：
 *   [DispatchRays] RT-GBuffer   → pos/normal/albedo/emissive GBuffer
 *   [compute]      ReSTIR 初始候选（M 候选，rhoLight 目标函数）
 *   [compute]      ReSTIR 空间复用（K 邻域，无偏 shift 合并，RayQuery 阴影）
 *   [compute]      ReSTIR 时间复用（prevViewProj 重投影 + 连续性验证）
 *   [DispatchRays] 着色（W·g·V 直接光 + 递归 PT 间接光，firefly clamp）
 *   [compute]      SVGF 方差估计 + 3 层 à-trous（可关）
 *   [compute]      ACES tonemap → 输出纹理（FXAA passthrough 消费）
 */

#pragma once

#include "RenderSettings.h"

#include "RHITypes.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// RHI
class RHIDevice;
class RHICommandBuffer;
class RHIBuffer;
class RHITexture;
class RHISampler;
class RHIBindingLayout;
class RHIBindingGroup;
class RHIPipeline;
class RHIBLAS;
class RHITLAS;
class RHIShader;

namespace VEngine {
class Scene;

namespace GRT {

/// GPU 光追设置（RenderSettings 相关字段的快照）
struct GRTSettings {
    int   maxBounces = 5;
    bool  shadows = true;
    float skyIntensity = 1.0f;
    float exposure = 1.0f;
    int   restirCandidates = 32;
    int   restirSpatialNeighbors = 3;
    bool  restirTemporal = true;
    bool  restirShadowInTarget = false;
    bool  denoiser = true;
};

/// 帧统计（UI 展示）
struct GRTStats {
    uint32_t instances = 0;
    uint32_t meshes = 0;         // BLAS 数
    uint32_t triangles = 0;
    uint32_t lights = 0;
    uint32_t textures = 0;
    double   lastAsBuildMs = 0.0;   // 最近一次场景重建耗时
    double   lastFrameMs = 0.0;     // CPU 录制耗时（近似 GPU）
};

// ---- 与 HLSL 严格一致的结构体（见 shaders/hlsl/raytrace/rt_common.hlsli）----

struct RTMaterialGPU {          // 80B
    glm::vec4 albedoMetallic;   // rgb albedo, a metallic
    glm::vec4 roughAoEmStrOp;   // r roughness, g ao, b emissiveStrength, a opacity
    glm::vec4 emissive;         // rgb emissive
    glm::ivec4 texAlbNrmMR;     // albedo/normal/metallicRough/ao 贴图索引
    glm::ivec4 texEmisExtra;    // x emissiveTex
};
static_assert(sizeof(RTMaterialGPU) == 80, "RTMaterialGPU must match HLSL RTMaterial");

struct RTInstanceGPU {          // 64B
    uint32_t  materialIndex = 0;
    uint32_t  vertexOffsetBytes = 0;
    uint32_t  indexOffsetBytes = 0;
    uint32_t  pad = 0;
    glm::vec4 nrmRow0;          // 法线矩阵 3x3 行存储
    glm::vec4 nrmRow1;
    glm::vec4 nrmRow2;
};
static_assert(sizeof(RTInstanceGPU) == 64, "RTInstanceGPU must match HLSL RTInstance");

struct RTLightGPU {             // 80B
    glm::vec3 color;
    int32_t   type = 1;         // 0 Dir, 1 Point, 2 Spot
    glm::vec3 position;
    float     intensity = 1.0f;
    glm::vec3 direction;
    float     range = 10.0f;
    glm::vec4 atten;            // x const, y linear, z quadratic, w innerCone(rad)
    float     outerCone = 0.3f;
    float     pad[3];
};
static_assert(sizeof(RTLightGPU) == 80, "RTLightGPU must match HLSL RTLight");

class GPURayTracer {
public:
    static constexpr uint32_t kMaxBindlessTextures = 1024;

    explicit GPURayTracer(RHIDevice* device);
    ~GPURayTracer();

    GPURayTracer(const GPURayTracer&) = delete;
    GPURayTracer& operator=(const GPURayTracer&) = delete;

    /// DXR Tier 检查（Tier 1.1：RayQuery 在 ReSTIR/spatial/NEE 中必需）
    static bool isSupported(RHIDevice* device);

    /// 场景变化时重建（BLAS/TLAS/合并几何/材质/灯光/贴图绑定）。内部 waitIdle。
    void buildFromScene(Scene* scene);

    /// 相机 / 分辨率设置（变化时重建 GBuffer 纹理与历史）
    void setCamera(const glm::mat4& view, const glm::mat4& proj,
                   uint32_t width, uint32_t height);

    /// 相机或场景变化 → 历史失效（方差/时间复用重置）
    void resetHistory() { historyValid_ = false; }

    /// 录制一帧（所有 pass 依次派发；每帧调用一次）
    void renderFrame(RHICommandBuffer* cmd, uint32_t frameSlot, const GRTSettings& settings);

    /// 输出纹理（R8G8B8A8_UNORM，Sampled|Storage；FXAA passthrough 消费）
    RHITexture* getOutputTexture() const { return outputTex_.get(); }

    uint32_t getWidth() const { return width_; }
    uint32_t getHeight() const { return height_; }
    const GRTStats& getStats() const { return stats_; }
    uint64_t getFrameIndex() const { return frameIndex_; }

private:
    // ---- 初始化 ----
    void createStaticResources();     // layout0/1 + scene binding group + sampler
    void createPipelines();           // 2 RTPSO + 6 compute
    std::shared_ptr<RHITexture> makeTexture(RHIFormat format);
    void createPassTargets();         // GBuffer/颜色/方差/历史纹理 + pass 绑定组
    void cleanupPassTargets();

    // ---- 帧内小工具 ----
    void dispatchCompute(RHICommandBuffer* cmd, uint32_t frameSlot,
                         RHIPipeline* pipe, RHIBindingGroup* group,
                         uint32_t groupsX, uint32_t groupsY,
                         const void* pushData, uint32_t pushSize);
    /// 写 SBT 记录（32B identifier 拷贝到 64B 对齐记录）
    bool writeSBTRecord(std::vector<uint8_t>& sbt, size_t recordIndex,
                        RHIPipeline* pipe, const std::string& name);

    RHIDevice* device_ = nullptr;

    // ---- 常量 ----
    // 帧常量走 root constants（push constants b0 space3 = layout 数 3）：
    // descriptor-table CBV 在部分驱动上高位寄存器读到未定义数据（reg10+ 死循环
    // 挂起），root constants 通路经全量验证可靠。224B = 56 DWORD（root sig 限额内）。
    struct FrameUBO {                 // 224B，与 rt_bindings.hlsli RTFrame 一致
        // 注意 uint 字段必须以整数位模式写入（float 位模式会让 shader 读到
        // 巨大整数 —— ReSTIR 循环边界曾因此变十亿级 → GPU TDR 挂起）
        glm::mat4 invViewProj;        // reg0-3
        glm::mat4 prevViewProj;       // reg4-7
        glm::vec3 camPos;  uint32_t frameIndex;      // reg8
        glm::vec2 resolution; uint32_t lightCount; float skyIntensity; // reg9
        uint32_t maxBounces; float rayEpsilon;                       // reg10
        uint32_t restirCandidates; uint32_t restirSpatialNeighbors;
        uint32_t restirTemporal; uint32_t shadows;                   // reg11
        uint32_t shadowInTarget; float prevCamPad;
        glm::vec3 prevCamPos; uint32_t historyValid;                 // reg12
        uint32_t step; uint32_t reset; float exposure; float pushPad;  // reg13
    };
    static_assert(sizeof(FrameUBO) == 224, "FrameUBO must match HLSL RTFrame");
    FrameUBO frameData_{};            // 本帧常量（renderFrame 开头填充）

    // ---- 管线与布局 ----
    std::shared_ptr<RHIBindingLayout> layoutFrame_;     // set0: b0 UBO
    std::shared_ptr<RHIBindingLayout> layoutScene_;     // set1: t0..t6 + s7
    std::shared_ptr<RHIPipeline> gbufferPipe_;
    std::shared_ptr<RHIPipeline> shadePipe_;
    std::shared_ptr<RHIPipeline> restirInitPipe_;
    std::shared_ptr<RHIPipeline> restirSpatialPipe_;
    std::shared_ptr<RHIPipeline> restirTemporalPipe_;
    std::shared_ptr<RHIPipeline> svgfVarPipe_;
    std::shared_ptr<RHIPipeline> svgfAtrousPipe_;
    std::shared_ptr<RHIPipeline> resolvePipe_;

    // ---- 帧常量 UBO（双缓冲）----
    std::shared_ptr<RHIBuffer> frameUBO_[2];
    std::shared_ptr<RHIBindingGroup> frameGroups_[2];

    // ---- 场景资源（buildFromScene 重建）----
    std::shared_ptr<RHIBindingGroup> sceneGroup_;       // set1 绑定组
    std::shared_ptr<RHIBuffer> mergedVB_;
    std::shared_ptr<RHIBuffer> mergedIB_;
    std::shared_ptr<RHIBuffer> materialBuf_;
    std::shared_ptr<RHIBuffer> instanceBuf_;
    std::shared_ptr<RHIBuffer> lightBuf_;
    std::shared_ptr<RHIBuffer> blasScratch_;
    std::shared_ptr<RHIBuffer> tlasScratch_;
    std::shared_ptr<RHIBuffer> instanceDescBuf_;        // DXR instance descs (64B/个)
    std::vector<std::shared_ptr<RHIBLAS>> blas_;
    std::shared_ptr<RHITLAS> tlas_;
    std::unordered_map<std::string, uint32_t> meshIndexByPath_;  // meshPath → BLAS 下标
    std::unordered_map<std::string, int> textureIndexByPath_;    // 贴图路径 → bindless 下标

    // ---- 每帧绑定组（set2 各 pass；ping-pong 用静态变体组，避免运行时
    //      updateTexture —— 部分情况下该路径的多条目组描述符改写不生效）----
    struct PassResources {
        std::shared_ptr<RHIBindingLayout> layout;       // set2: 本 pass IO
        std::shared_ptr<RHIBindingGroup> group;
    };
    PassResources gbufferPass_;
    PassResources restirInitPass_;
    PassResources restirSpatialPass_;
    PassResources restirTemporalPass_[2];   // 变体：histIdx 0/1
    PassResources shadePass_;
    PassResources svgfVarPass_[2];          // 变体：histIdx 0/1
    PassResources atrousStep1_[2];          // 变体：输入 histColor_[0]/[1]
    PassResources atrousStep2_;             // colorFiltered → colorTmp
    PassResources atrousStep3_;             // colorTmp → colorFiltered
    PassResources resolvePass_[2];          // 变体：denoiser 开（colorFiltered）/关（colorTex）

    // ---- SBT ----
    std::shared_ptr<RHIBuffer> sbtBuf_;
    static constexpr uint32_t kSBTRecordSize = 64;
    // 记录布局：[0]rgGBuffer [1]rgShade [2]msSky [3]msSkyShade [4]hgGBuffer [5]hgShade

    // ---- 采样器 ----
    std::shared_ptr<RHISampler> linearSampler_;

    // ---- GBuffer / 中间纹理（setCamera 重建）----
    std::shared_ptr<RHITexture> worldPosTex_;     // RGBA32F
    std::shared_ptr<RHITexture> normalRoughTex_;  // RGBA16F
    std::shared_ptr<RHITexture> albedoMetalTex_;  // RGBA8
    std::shared_ptr<RHITexture> emissiveAoTex_;   // RGBA16F
    std::shared_ptr<RHITexture> reservoirInitTex_;   // RGBA32F
    std::shared_ptr<RHITexture> reservoirSpatialTex_;// RGBA32F
    std::shared_ptr<RHITexture> reservoirFinalTex_;  // RGBA32F
    std::shared_ptr<RHITexture> colorTex_;        // RGBA32F（着色输出 rgb + lum）
    std::shared_ptr<RHITexture> colorFilteredTex_;// RGBA32F（à-trous 乒乓 A）
    std::shared_ptr<RHITexture> colorTmpTex_;     // RGBA32F（à-trous 乒乓 B）
    std::shared_ptr<RHITexture> varTex_;          // R32F（方差 EMA）
    // ---- 历史（ping-pong：时间复用 + SVGF 方差；索引 histIdx_ / 1-histIdx_）----
    std::shared_ptr<RHITexture> histPos_[2];      // RGBA32F
    std::shared_ptr<RHITexture> histNormal_[2];   // RGBA16F
    std::shared_ptr<RHITexture> histReservoir_[2];// RGBA32F
    std::shared_ptr<RHITexture> histColor_[2];    // RGBA32F（rgb + EMA mean lum）
    std::shared_ptr<RHITexture> histVar_[2];      // R32F
    uint32_t histIdx_ = 0;
    std::shared_ptr<RHITexture> outputTex_;       // RGBA8 UNORM

    // ---- 状态 ----
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    glm::mat4 view_ = glm::mat4(1.0f);
    glm::mat4 proj_ = glm::mat4(1.0f);
    glm::mat4 lastFrameViewProj_ = glm::mat4(0.0f);
    glm::vec3 lastFrameCamPos_ = glm::vec3(0.0f);
    bool historyValid_ = false;
    bool asDirty_ = true;          // 场景重建后待录制 AS 构建
    uint64_t frameIndex_ = 0;
    uint32_t lightCount_ = 0;
    bool sceneBuilt_ = false;
    bool initialized_ = false;

    GRTStats stats_;
};

} // namespace GRT
} // namespace VEngine
