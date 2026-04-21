#pragma once

#include "ComputePassBase.h"
#include "ComputePipeline.h"
#include "../nanite/NaniteCluster.h"
#include <glm/glm.hpp>
#include <vector>

class VulkanBuffer;

namespace Nanite {

/**
 * Cluster Culling �?Uniform 数据
 * 必须�?cluster_culling.comp 中的 CullingUniforms 完全匹配（std140 布局�?
 */
struct alignas(16) ClusterCullingUniforms {
    glm::mat4 viewMatrix;        // 64 bytes, offset 0
    glm::mat4 projMatrix;        // 64 bytes, offset 64
    glm::mat4 viewProjMatrix;    // 64 bytes, offset 128
    glm::vec4 frustumPlanes[6];  // 96 bytes, offset 192
    glm::vec4 cameraPosition;    // 16 bytes, offset 288 (xyz: position, w: unused)
    glm::uvec4 clusterCountPacked; // 16 bytes, offset 304 (x=count, y=frustumCull, z=coneCull, w=lodSelect)
    glm::vec4 screenParams;      // 16 bytes, offset 320 (x=width, y=height, z=errorScale, w=threshold)
    // Total: 336 bytes
};

/**
 * ClusterCullingPass - Nanite Cluster 剔除�?LOD 选择
 * 
 * Phase 3: Runtime Selection
 * 使用 Compute Shader �?GPU 上执行：
 * 1. 视锥剔除（Frustum Culling�?
 * 2. 法线锥剔除（Cone Culling�?
 * 3. LOD 选择（基于屏幕空间误差）
 */
class ClusterCullingPass : public ComputePassBase {
public:
    ClusterCullingPass(std::shared_ptr<VulkanDevice> device);
    ~ClusterCullingPass() override;

    void init() override;
    void record(VkCommandBuffer commandBuffer) override;
    
    /**
     * 带帧索引�?record（推荐使用此版本�?
     * @param frameIndex 当前帧索引（用于选择正确�?readback buffer�?
     */
    void record(VkCommandBuffer commandBuffer, uint32_t frameIndex);
    void cleanup() override;

    /**
     * 设置 Cluster 数据缓冲区（来自 NaniteManager�?
     */
    void setClusterBuffer(VkBuffer buffer, uint32_t clusterCount);

    /**
     * 设置变换缓冲区（每个 mesh 的世界矩阵）
     */
    void setTransformBuffer(VkBuffer buffer);

    /**
     * 更新剔除参数
     */
    void updateUniforms(const ClusterCullingUniforms& uniforms);

    /**
     * 从视图投影矩阵提取视锥平�?
     */
    static void extractFrustumPlanes(const glm::mat4& viewProj, glm::vec4 planes[6]);

    /**
     * 获取可见 Cluster 数量（需要从 GPU 回读�?
     */
    uint32_t getVisibleCount();

    /**
     * 获取可见 Cluster 索引缓冲�?
     */
    VkBuffer getVisibleIndicesBuffer() const;

    /**
     * 获取计数器缓冲区
     */
    VkBuffer getCounterBuffer() const;

    /**
     * 获取可见索引列表（CPU 端，需要先回读�?
     */
    const std::vector<uint32_t>& getVisibleIndices();

    /**
     * 重置计数器（每帧开始时调用�?
     */
    void resetCounters(VkCommandBuffer commandBuffer);

    /**
     * 帧结束后读取 GPU 数据
     * 应在命令缓冲区提交并等待完成后调�?
     */
    void readbackData(uint32_t frameIndex);

    /**
     * 是否已初始化
     */
    bool isReady() const { return m_initialized && m_clusterBuffer != VK_NULL_HANDLE; }

private:
    void createBuffers();
    void createDescriptorSet();
    void updateDescriptorSet();

    // 外部 buffer 引用
    VkBuffer m_clusterBuffer = VK_NULL_HANDLE;
    VkBuffer m_transformBuffer = VK_NULL_HANDLE;
    uint32_t m_clusterCount = 0;

    // 内部 buffer
    std::unique_ptr<VulkanBuffer> m_uniformBuffer;
    std::unique_ptr<VulkanBuffer> m_dummyStorageBuffer;  // 占位�?storage buffer
    std::unique_ptr<VulkanBuffer> m_visibleIndicesBuffer;
    std::unique_ptr<VulkanBuffer> m_counterBuffer;
    std::unique_ptr<VulkanBuffer> m_selectionStateBuffer;
    
    // 双缓�?readback 用于稳定�?GPU->CPU 数据传输
    // �?N 写入 buffer[N%2]，读�?buffer[(N+1)%2]（即上上帧的数据�?
    static constexpr uint32_t READBACK_BUFFER_COUNT = 2;
    std::unique_ptr<VulkanBuffer> m_readbackBuffers[READBACK_BUFFER_COUNT];
    uint32_t m_currentReadbackIndex = 0;  // 当前写入�?buffer 索引

    // Descriptor
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

    // 状�?
    bool m_initialized = false;
    bool m_descriptorsDirty = true;
    bool m_dataCopyPending = false;  // 标记是否有待读取的数�?
    uint32_t m_visibleCount = 0;
    std::vector<uint32_t> m_visibleIndicesCPU;

    static constexpr uint32_t WORKGROUP_SIZE = 64;
    static constexpr uint32_t MAX_CLUSTERS = 65536;
};

} // namespace Nanite
