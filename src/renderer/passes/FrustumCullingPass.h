#pragma once

#include "ComputePassBase.h"
#include <glm/glm.hpp>
#include <vector>
#include <memory>

class RHIBuffer;
class RHIBindingGroup;

/**
 * GPU 实例数据
 */
struct GPUInstanceData {
    glm::mat4 modelMatrix;
    glm::vec4 boundingSphere;
    glm::vec4 aabbMin;
    glm::vec4 aabbMax;
    uint32_t meshIndex;
    uint32_t materialIndex;
    uint32_t flags;
    uint32_t padding;
};

/**
 * GPU Culling 的Uniform 数据 (std140 对齐)
 */
struct alignas(16) CullingUniforms {
    glm::mat4 viewMatrix;
    glm::mat4 projMatrix;
    glm::mat4 viewProjMatrix;
    glm::vec4 frustumPlanes[6];
    glm::vec4 cameraPosition;
    glm::uvec4 instanceCountPacked;
};

/**
 * DrawIndexedIndirectCommand — RHI 等效结构
 */
struct RHIDrawIndexedIndirectCommand {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t  vertexOffset;
    uint32_t firstInstance;
};

/**
 * FrustumCullingPass - GPU 视锥剔除 (Pure RHI)
 */
class FrustumCullingPass : public ComputePassBase {
public:
    FrustumCullingPass(RHIDevice* rhiDevice);
    ~FrustumCullingPass() override;

    void init() override;
    void record(RHICommandBuffer* cmd) override;
    void cleanup() override;

    void updateInstances(const std::vector<GPUInstanceData>& instances);
    void updateUniforms(const CullingUniforms& uniforms);

    uint32_t getVisibleCount();
    RHIBuffer* getIndirectDrawBuffer() const { return indirectDrawBuffer_.get(); }
    RHIBuffer* getVisibleIndicesBuffer() const { return visibleIndicesBuffer_.get(); }
    const std::vector<uint32_t>& getVisibleIndices();

    void resetCounters(RHICommandBuffer* cmd);

private:
    void createBuffers(uint32_t maxInstances);
    void createComputePipeline();
    void createDescriptorSet();
    void updateDescriptorSet();

    static void extractFrustumPlanes(const glm::mat4& viewProj, glm::vec4 planes[6]);
    void readbackCounter();

    // RHI buffers
    std::unique_ptr<RHIBuffer> instanceBuffer_;
    std::unique_ptr<RHIBuffer> uniformBuffer_;
    std::unique_ptr<RHIBuffer> visibleIndicesBuffer_;
    std::unique_ptr<RHIBuffer> indirectDrawBuffer_;
    std::unique_ptr<RHIBuffer> counterBuffer_;
    std::unique_ptr<RHIBuffer> counterReadbackBuffer_;
    std::unique_ptr<RHIBuffer> visibleIndicesReadbackBuffer_;

    // Binding group (replaces VkDescriptorSet)
    std::unique_ptr<RHIBindingGroup> bindingGroup_;

    uint32_t maxInstances_ = 0;
    uint32_t currentInstanceCount_ = 0;
    uint32_t visibleCount_ = 0;
    std::vector<uint32_t> visibleIndicesCPU_;

    static constexpr uint32_t WORKGROUP_SIZE = 256;
};