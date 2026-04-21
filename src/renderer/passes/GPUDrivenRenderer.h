#pragma once

#include "FrustumCullingPass.h"
#include <memory>
#include <vector>

class VulkanDevice;
class Camera;

/**
 * GPUDrivenRenderer - GPU 驱动渲染管理�?
 * 
 * 整合所�?GPU Culling �?Indirect Drawing 功能�?
 * 这是 Nanite 风格渲染的核心入口�?
 */
class GPUDrivenRenderer {
public:
    struct Config {
        uint32_t maxInstances = 100000;      // 最大实例数
        bool enableFrustumCulling = true;    // 启用视锥剔除
        bool enableOcclusionCulling = false; // 启用遮挡剔除（后续实现）
        bool enableLODSelection = true;      // 启用 LOD 选择（已实现�?
    };

    struct Statistics {
        uint32_t totalInstances = 0;
        uint32_t visibleInstances = 0;
        uint32_t culledInstances = 0;
        float cullingTimeMs = 0.0f;
    };

public:
    GPUDrivenRenderer(std::shared_ptr<VulkanDevice> device, const Config& config = Config{});
    ~GPUDrivenRenderer();

    /**
     * 初始化所�?Pass
     */
    void init();

    /**
     * 准备每帧数据
     * @param instances 场景中所有实例的数据
     * @param viewMatrix 相机视图矩阵
     * @param projMatrix 相机投影矩阵
     * @param cameraPos 相机位置
     */
    void prepare(const std::vector<GPUInstanceData>& instances,
                 const glm::mat4& viewMatrix,
                 const glm::mat4& projMatrix,
                 const glm::vec3& cameraPos);

    /**
     * 执行 GPU Culling（在 Compute Queue 上）
     */
    void executeCulling(VkCommandBuffer commandBuffer);

    /**
     * 获取间接绘制缓冲�?
     */
    VkBuffer getIndirectDrawBuffer() const;

    /**
     * 获取可见实例索引缓冲�?
     */
    VkBuffer getVisibleIndicesBuffer() const;

    /**
     * 获取可见物体数量（需�?GPU->CPU 回读�?
     */
    uint32_t getVisibleCount() const;

    /**
     * 获取可见实体索引列表（方案B：GPU->CPU 回读压缩索引�?
     * @return 可见实体的原始索引列�?
     */
    const std::vector<uint32_t>& getVisibleIndices();

    /**
     * 获取统计信息
     */
    const Statistics& getStatistics() const { return stats; }

    /**
     * 是否启用 GPU Culling
     */
    bool isEnabled() const { return config.enableFrustumCulling; }

    /**
     * 设置是否启用
     */
    void setEnabled(bool enabled) { config.enableFrustumCulling = enabled; }

private:
    std::shared_ptr<VulkanDevice> device;
    Config config;
    Statistics stats;

    // Culling Passes
    std::unique_ptr<FrustumCullingPass> frustumCullingPass;
    
    // 后续扩展
    // std::unique_ptr<OcclusionCullingPass> occlusionCullingPass;
    // std::unique_ptr<LODSelectionPass> lodSelectionPass;
};
