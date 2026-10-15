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

#include <glm/glm.hpp>
#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace VEngine {

class Scene;

namespace RT {

/// 渲染算法
enum class Algorithm {
    Whitted,        // 经典光线追踪：直接光 + 镜面反射 + 折射
    PathTracing     // 蒙特卡洛路径追踪：余弦采样 + NEE + RR，渐进收敛
};

/// 渲染设置（UI 可调）
struct RaytraceSettings {
    Algorithm algorithm = Algorithm::Whitted;
    int samplesPerPixel = 1;    // 每次 renderFrame 的每像素样本数
    int maxBounces = 5;         // 最大弹射次数
    bool shadows = true;        // 阴影射线开关
    float skyIntensity = 1.0f;  // 环境天光强度
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

/// 世界空间命中信息（含插值法线与材质）
struct SceneHit {
    RayHit hit;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
    const RTMaterial* material = nullptr;
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

    // ---- 积分器 ----
    glm::vec3 traceWhitted(const glm::vec3& orig, const glm::vec3& dir,
                           int depth, uint32_t& rng) const;
    glm::vec3 tracePath(const glm::vec3& orig, const glm::vec3& dir,
                        uint32_t& rng) const;

    // ---- 光照 ----
    /// Whitted 直接光：遍历全部光源，Lambert + 阴影射线
    glm::vec3 directLightingWhitted(const SceneHit& hit, uint32_t& rng) const;
    /// 路径追踪 NEE：随机取一个光源做直接光估计
    glm::vec3 estimateDirectLight(const SceneHit& hit, uint32_t& rng) const;
    /// 程序化天光（渐变）
    glm::vec3 skyColor(const glm::vec3& dir) const;

    // ---- 分块渲染 ----
    void renderTile(uint32_t x0, uint32_t y0, uint32_t tileW, uint32_t tileH,
                    uint32_t frameIndex, int spp, uint32_t tileSeed);

    // ---- 数据 ----
    std::vector<RTMeshData> meshes_;
    std::vector<RTInstance> instances_;
    std::vector<RTLight> lights_;
    std::unordered_map<std::string, uint32_t> meshIndexByPath_;

    Framebuffer fb_;
    std::unique_ptr<ThreadPool> pool_;

    // ---- 相机 ----
    glm::vec3 camPos_ = glm::vec3(0.0f);
    glm::mat4 invViewProj_ = glm::mat4(1.0f);
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    // ---- 统计 ----
    float lastFrameMs_ = 0.0f;
    mutable std::atomic<uint64_t> rayCount_{0};
};

} // namespace RT
} // namespace VEngine
