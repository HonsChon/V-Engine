#pragma once

#include "ComputePassBase.h"
#include "ComputePipeline.h"
#include <glm/glm.hpp>
#include <vector>

class VulkanBuffer;

/**
 * GPU 实例数据
 * 存储每个物体的变换和包围盒信息
 */
struct GPUInstanceData {
    glm::mat4 modelMatrix;       // 模型矩阵
    glm::vec4 boundingSphere;    // 包围球(xyz: center, w: radius)
    glm::vec4 aabbMin;           // AABB 最小点
    glm::vec4 aabbMax;           // AABB 最大点
    uint32_t meshIndex;          // 网格索引
    uint32_t materialIndex;      // 材质索引
    uint32_t flags;              // 标志位（可见性等：
    uint32_t padding;
};

/**
 * GPU Culling 的Uniform 数据
 * 注意：必须与 shader 中的 std140 布局完全匹配：
 * std140 规则对齐，使用uvec4 代替 uint 数组避免对齐问题
 */
struct alignas(16) CullingUniforms {
    glm::mat4 viewMatrix;        // 64 bytes, offset 0
    glm::mat4 projMatrix;        // 64 bytes, offset 64
    glm::mat4 viewProjMatrix;    // 64 bytes, offset 128
    glm::vec4 frustumPlanes[6];  // 96 bytes, offset 192  (视锥体平面，世界空间)
    glm::vec4 cameraPosition;    // 16 bytes, offset 288
    glm::uvec4 instanceCountPacked; // 16 bytes, offset 304 (x=instanceCount, yzw=padding)
    // Total: 320 bytes
};

/**
 * FrustumCullingPass - GPU 视锥剔除
 * 
 * 使用 Compute Shader 在GPU 上执行视锥剔除。
 * 输入：所有物体的实例数据
 * 输出：可见物体的间接绘制命令
 */
class FrustumCullingPass : public ComputePassBase {
public:
    FrustumCullingPass(std::shared_ptr<VulkanDevice> device, RHIDevice* rhiDevice = nullptr);
    ~FrustumCullingPass() override;

    void init() override;
    void record(VkCommandBuffer commandBuffer) override;
    void cleanup() override;

    /**
     * 更新实例数据
     * @param instances 实例数据数组
     */
    void updateInstances(const std::vector<GPUInstanceData>& instances);

    /**
     * 更新剔除参数（相机矩阵等：
     */
    void updateUniforms(const CullingUniforms& uniforms);

    /**
     * 获取可见实例数量（需要从 GPU 回读：
     */
    uint32_t getVisibleCount();

    /**
     * 获取间接绘制缓冲区
     */
    VkBuffer getIndirectDrawBuffer() const;

    /**
     * 获取可见实例索引缓冲区
     */
    VkBuffer getVisibleIndicesBuffer() const;
    
    /**
     * 获取可见实例索引（从 GPU 回读：
     * @return 可见实例的原始索引列表
     */
    const std::vector<uint32_t>& getVisibleIndices();

    /**
     * 重置计数器（每帧开始时调用：
     */
    void resetCounters(VkCommandBuffer commandBuffer);

private:
    void createBuffers(uint32_t maxInstances);
    void createDescriptorSet();
    void updateDescriptorSet();

    // 从视图和投影矩阵提取视锥体平面
    static void extractFrustumPlanes(const glm::mat4& viewProj, glm::vec4 planes[6]);

    // 从GPU 回读计数器
    void readbackCounter();

    // Legacy compute pipeline (transitional — will be replaced by RHI Compute Pipeline Builder)
    std::unique_ptr<ComputePipeline> pipeline;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    // 缓冲区
    std::unique_ptr<VulkanBuffer> instanceBuffer;        // 输入：所有实例数据
    std::unique_ptr<VulkanBuffer> uniformBuffer;         // Uniform 数据
    std::unique_ptr<VulkanBuffer> visibleIndicesBuffer;  // 输出：可见实例索引
    std::unique_ptr<VulkanBuffer> indirectDrawBuffer;    // 输出：间接绘制命令
    std::unique_ptr<VulkanBuffer> counterBuffer;         // 原子计数器
    std::unique_ptr<VulkanBuffer> counterReadbackBuffer; // CPU 可读的计数器副本
    std::unique_ptr<VulkanBuffer> visibleIndicesReadbackBuffer; // CPU 可读的可见索引

    uint32_t maxInstances = 0;
    uint32_t currentInstanceCount = 0;
    uint32_t visibleCount = 0;
    std::vector<uint32_t> visibleIndicesCPU;  // CPU 端缓存的可见索引

    static constexpr uint32_t WORKGROUP_SIZE = 256;
};
