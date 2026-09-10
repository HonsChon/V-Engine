#pragma once

#include "ComputePassBase.h"
#include "../nanite/NaniteCluster.h"
#include <glm/glm.hpp>
#include <vector>
#include <memory>

class RHIBuffer;
class RHIBindingGroup;
class RHICommandBuffer;

namespace Nanite {

/**
 * Cluster Culling Uniform 数据 (std140 布局)
 */
struct alignas(16) ClusterCullingUniforms {
    glm::mat4 viewMatrix;
    glm::mat4 projMatrix;
    glm::mat4 viewProjMatrix;
    glm::vec4 frustumPlanes[6];
    glm::vec4 cameraPosition;
    glm::uvec4 clusterCountPacked;
    glm::vec4 screenParams;
};

/**
 * ClusterCullingPass - Nanite Cluster 剔除和LOD 选择 (Pure RHI)
 */
class ClusterCullingPass : public ComputePassBase {
public:
    ClusterCullingPass(RHIDevice* rhiDevice);
    ~ClusterCullingPass() override;

    void init() override;
    void record(RHICommandBuffer* cmd) override;
    void record(RHICommandBuffer* cmd, uint32_t frameIndex);
    void cleanup() override;

    /**
     * 设置 Cluster 数据缓冲区（来自 NaniteManager，RHI buffer）
     */
    void setClusterBuffer(RHIBuffer* buffer, uint32_t clusterCount);

    /**
     * 设置变换缓冲区（每个 mesh 的世界矩阵）
     */
    void setTransformBuffer(RHIBuffer* buffer);

    void updateUniforms(const ClusterCullingUniforms& uniforms);
    static void extractFrustumPlanes(const glm::mat4& viewProj, glm::vec4 planes[6]);

    uint32_t getVisibleCount();
    RHIBuffer* getVisibleIndicesBuffer() const { return m_visibleIndicesBuffer_.get(); }
    RHIBuffer* getCounterBuffer() const { return m_counterBuffer_.get(); }
    const std::vector<uint32_t>& getVisibleIndices();

    void resetCounters(RHICommandBuffer* cmd);
    void readbackData(uint32_t frameIndex);

    bool isReady() const { return m_initialized && m_clusterBuffer_ != nullptr; }

private:
    void createBuffers();
    void createComputePipeline();
    void createDescriptorSet();
    void updateDescriptorSet();

    // External buffer references (NOT owned)
    RHIBuffer* m_clusterBuffer_ = nullptr;
    RHIBuffer* m_transformBuffer_ = nullptr;
    uint32_t m_clusterCount_ = 0;

    // Internal RHI buffers
    std::shared_ptr<RHIBuffer> m_uniformBuffer_;
    std::shared_ptr<RHIBuffer> m_dummyStorageBuffer_;
    std::shared_ptr<RHIBuffer> m_visibleIndicesBuffer_;
    std::shared_ptr<RHIBuffer> m_counterBuffer_;
    std::shared_ptr<RHIBuffer> m_selectionStateBuffer_;

    // Double-buffered readback
    static constexpr uint32_t READBACK_BUFFER_COUNT = 2;
    std::shared_ptr<RHIBuffer> m_readbackBuffers_[READBACK_BUFFER_COUNT];
    uint32_t m_currentReadbackIndex_ = 0;

    // Binding group
    std::shared_ptr<RHIBindingGroup> m_bindingGroup_;

    // State
    bool m_initialized = false;
    bool m_descriptorsDirty = true;
    bool m_dataCopyPending = false;
    uint32_t m_visibleCount = 0;
    std::vector<uint32_t> m_visibleIndicesCPU;

    static constexpr uint32_t WORKGROUP_SIZE = 64;
    static constexpr uint32_t MAX_CLUSTERS = 65536;
};

} // namespace Nanite