/**
 * @file SoftwareRaytracer.h
 * @brief CPU 软光追渲染器 — Whitted / 路径追踪双积分器
 *
 * 从 ECS 场景采集扁平化数据（网格 + SAH BVH / 材质 / 光源），
 * 线程池分块并行渲染到 HDR 累积帧缓冲。
 *
 * 用法：
 *   1. buildFromScene()  — 场景或变换变化时调用（重建实例，网格 BVH 按路径缓存重建）
 *   2. setCamera()       — 相机参数变化时调用
 *   3. renderFrame()     — 每帧调用（渐进累积，相机/场景变化需 resetAccumulation()）
 */

#pragma once

#include "RaytracerTypes.h"
#include "BVH.h"
#include "Framebuffer.h"
#include "ThreadPool.h"
#include "Restir.h"
#include "CpuTexture.h"

#include <glm/glm.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace VEngine {

class Scene;

namespace RT {

/// 渲染算法
enum class Algorithm {
    Whitted,            // 经典光线追踪：直接光 + 镜面反射 + 折射
    PathTracing,        // 蒙特卡洛路径追踪：余弦采样 + NEE + RR，渐进收敛
    PathTracingRestir   // 路径追踪 + ReSTIR 直接光（无偏 MIS 蓄水池时空复用）
};

/// 渲染设置（UI 可调）
struct RaytraceSettings {
    Algorithm algorithm = Algorithm::Whitted;
    int samplesPerPixel = 1;    // 每次 renderFrame 的每像素样本数
    int maxBounces = 5;         // 最大弹射次数
    bool shadows = true;        // 阴影射线开关
    float skyIntensity = 1.0f;  // 环境天光强度

    // ========== ReSTIR（PathTracingRestir 模式）==========
    int restirCandidates = 32;        // 初始候选数 M（每像素每帧）
    int restirSpatialNeighbors = 3;   // 空间复用邻居数 K
    bool restirTemporal = true;       // 时间复用开关
    bool restirShadowInTarget = false;// 可见性计入目标函数（更准，每候选 1 阴影射线）
};

/// 网格数据（局部空间三角形 + BVH，按 meshPath 去重共享）
struct RTMeshData {
    std::vector<RTTriangle> triangles;
    BVH bvh;
};

/// 场景实例（网格引用 + 世界变换的逆 + 材质）
struct RTInstance {
    uint32_t meshIndex = 0;
    glm::mat4 invWorld = glm::mat4(1.0f);
    glm::mat3 normalMatrix = glm::mat3(1.0f);   // 世界法线 = normalMatrix * 局部法线
    RTMaterial material;
};

/// 世界空间命中信息（含逐命中贴图采样后的着色参数）
struct SceneHit {
    RayHit hit;
    glm::vec3 position = glm::vec3(0.0f);
    ShadedMaterial shaded;   // shaded.normal 即最终着色法线
};

class SoftwareRaytracer {
public:
    SoftwareRaytracer();
    ~SoftwareRaytracer();

    SoftwareRaytracer(const SoftwareRaytracer&) = delete;
    SoftwareRaytracer& operator=(const SoftwareRaytracer&) = delete;

    // ========== 场景 ==========

    /// 从 ECS 场景采集几何/材质/光照（网格按 meshPath 去重，透明物体跳过）
    void buildFromScene(Scene* scene);

    // ========== 相机 ==========

    /// 设置相机（proj 须为 GL 惯例透视矩阵，不含 Vulkan Y 翻转）
    void setCamera(const glm::mat4& view, const glm::mat4& proj,
                   uint32_t width, uint32_t height);

    // ========== 渲染 ==========

    /// 清空累积缓冲（相机/场景/设置变化时调用）
    void resetAccumulation();

    /// 渲染一帧（分块并行，按 settings.samplesPerPixel 采样并累积）
    void renderFrame(uint32_t frameIndex);

    const Framebuffer& framebuffer() const { return fb_; }

    // ========== 统计 ==========
    uint32_t instanceCount() const { return static_cast<uint32_t>(instances_.size()); }
    uint32_t meshCount() const { return static_cast<uint32_t>(meshes_.size()); }
    uint32_t totalTriangles() const;
    uint32_t totalNodes() const;
    uint32_t lightCount() const { return static_cast<uint32_t>(lights_.size()); }
    float lastFrameMs() const { return lastFrameMs_; }
    uint64_t rayCount() const { return rayCount_.load(std::memory_order_relaxed); }

    RaytraceSettings settings;

private:
    // ---- 求交 ----
    bool intersectScene(const glm::vec3& orig, const glm::vec3& dir,
                        float tMax, SceneHit& out) const;
    bool occluded(const glm::vec3& orig, const glm::vec3& dir, float tMax) const;

    // ---- 贴图 ----
    /// 加载（或命中缓存）贴图并返回 textures_ 下标；空路径/失败返回 -1
    int loadTextureIndex(const std::string& path, bool srgb);
    /// 逐命中材质求值：UV 插值 → 采样各贴图 → 合并常量因子
    void evaluateShadedMaterial(const RTTriangle& tri, const RTInstance& inst,
                                float u, float v, const glm::vec3& geomNormal,
                                const glm::vec3& dir, ShadedMaterial& out) const;

    // ---- 积分器 ----
    glm::vec3 traceWhitted(const glm::vec3& orig, const glm::vec3& dir,
                           int depth, uint32_t& rng) const;
    glm::vec3 tracePath(const glm::vec3& orig, const glm::vec3& dir,
                        uint32_t& rng) const;
    /// 从已求交的主命中继续路径（ReSTIR：bounce0 直接光由 directOverride 注入）
    glm::vec3 tracePathFrom(const SceneHit& hit, const glm::vec3& dir,
                            uint32_t& rng, const glm::vec3* directOverride) const;

    // ---- 光照 ----
    /// Whitted 直接光：遍历全部光源，Lambert + 阴影射线
    glm::vec3 directLightingWhitted(const SceneHit& hit, uint32_t& rng) const;
    /// 路径追踪 NEE：随机取一个光源做直接光估计
    glm::vec3 estimateDirectLight(const SceneHit& hit, uint32_t& rng) const;
    /// 程序化天光（渐变）
    glm::vec3 skyColor(const glm::vec3& dir) const;

    // ---- ReSTIR ----
    /// 目标函数 ρ：表面 surf 与光源 lightIdx 的未遮挡直接光贡献（标量=luminance）
    float rhoLight(uint32_t lightIdx, const SurfaceInfo& surf) const;
    /// 目标函数的 RGB 版（最终着色用）
    glm::vec3 rhoLightRGB(uint32_t lightIdx, const SurfaceInfo& surf) const;
    /// 光源在表面处是否被遮挡（阴影射线）
    bool lightShadowed(uint32_t lightIdx, const SurfaceInfo& surf) const;
    /// 蓄水池合并（shift 校正池化，无偏；同目标退化为朴素池化）
    void gatherMergeReservoirs(const Reservoir* const* reservoirs,
                               const SurfaceInfo* const* surfs,
                               int count, const SurfaceInfo& target,
                               Reservoir& out, uint32_t& rng) const;

    // ---- 分块渲染 ----
    void renderTile(uint32_t x0, uint32_t y0, uint32_t tileW, uint32_t tileH,
                    uint32_t frameIndex, int spp, uint32_t tileSeed);
    /// 分块派发工具（各 phase 共用；任务只写自己的像素/行区间）
    void dispatchTiles(const std::function<void(uint32_t, uint32_t, uint32_t, uint32_t)>& fn);

    // ---- ReSTIR 渲染 phases ----
    void renderFrameRestir(uint32_t frameIndex, int spp);
    void restirPhaseInitial(uint32_t x0, uint32_t y0, uint32_t tw, uint32_t th,
                            uint32_t frameIndex);
    void restirPhaseSpatial(uint32_t x0, uint32_t y0, uint32_t tw, uint32_t th,
                            uint32_t frameIndex);
    void restirPhaseTemporal(uint32_t x0, uint32_t y0, uint32_t tw, uint32_t th,
                             uint32_t frameIndex);
    void restirPhaseShade(uint32_t x0, uint32_t y0, uint32_t tw, uint32_t th,
                          uint32_t frameIndex, int spp);
    void resizeRestirBuffers();

    // ---- 数据 ----
    std::vector<RTMeshData> meshes_;
    std::vector<RTInstance> instances_;
    std::vector<RTLight> lights_;
    std::unordered_map<std::string, uint32_t> meshIndexByPath_;

    // ---- 贴图（CPU 采样）----
    std::vector<std::shared_ptr<CpuTexture>> textures_;
    CpuTextureCache textureCache_;
    std::unordered_map<std::string, int> textureIndexByPath_;

    Framebuffer fb_;
    std::unique_ptr<ThreadPool> pool_;

    // ---- 相机 ----
    glm::vec3 camPos_ = glm::vec3(0.0f);
    glm::mat4 invViewProj_ = glm::mat4(1.0f);
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    // ---- ReSTIR 逐像素缓冲 ----
    std::vector<SurfaceInfo> restirGBuffer_;        // 当前帧主命中表面
    std::vector<Reservoir> restirInitial_;          // Phase A 输出
    std::vector<Reservoir> restirSpatial_;          // Phase B 输出
    std::vector<Reservoir> restirFinal_;            // Phase C 输出（着色用）
    std::vector<Reservoir> restirPrev_;             // 上一帧最终蓄水池（时间复用）
    std::vector<SurfaceInfo> restirPrevGBuffer_;    // 上一帧表面（时间复用验证/求值）
    glm::mat4 prevViewProj_ = glm::mat4(1.0f);      // 上一帧 viewProj（重投影）
    glm::vec3 prevCamPos_ = glm::vec3(0.0f);
    bool restirHistoryValid_ = false;               // 时间历史是否可用

    // ---- 统计 ----
    float lastFrameMs_ = 0.0f;
    mutable std::atomic<uint64_t> rayCount_{0};
};

} // namespace RT
} // namespace VEngine
