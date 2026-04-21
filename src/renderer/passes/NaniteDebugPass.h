#pragma once

/**
 * NaniteDebugPass.h - Nanite Cluster 调试可视化渲染通道
 * 
 * 功能�?
 * - 使用不同颜色渲染每个 Cluster，便于可视化分割结果
 * - 支持多种调试模式：Cluster 颜色、法线、LOD �?
 */

#include "RenderPassBase.h"
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <glm/glm.hpp>

// 前向声明
namespace Nanite {
    class NaniteManager;
    class ClusterizedMesh;
    struct Cluster;
    class ClusterCullingPass;
}

class VulkanDevice;
class VulkanBuffer;
class VulkanSwapChain;
class Scene;

/**
 * Cluster 调试信息 Push Constants
 */
struct ClusterDebugPushConstants {
    glm::mat4 model;
    glm::mat4 normalMatrix;
    uint32_t clusterIndex;
    uint32_t totalClusters;
    uint32_t debugMode;
    float padding;
};

/**
 * 调试模式枚举
 */
enum class NaniteDebugMode : uint32_t {
    ClusterColor = 0,   // 每个 Cluster 不同颜色
    Normal = 1,         // 法线可视�?
    LOD = 2,            // LOD 级别可视�?
    HashColor = 3       // 哈希随机颜色（高对比度）
};

/**
 * NaniteDebugPass - Nanite 调试渲染通道
 */
class NaniteDebugPass : public RenderPassBase {
public:
    NaniteDebugPass(std::shared_ptr<VulkanDevice> device,
                    std::shared_ptr<VulkanSwapChain> swapChain,
                    std::shared_ptr<Nanite::NaniteManager> naniteManager);
    
    ~NaniteDebugPass() override;
    
    /**
     * 初始化渲染通道
     * @param renderPass Vulkan 渲染通道句柄
     */
    void initialize(VkRenderPass renderPass);
    
    /**
     * 清理资源
     */
    void cleanup();
    
    /**
     * 录制渲染命令（单网格模式�?
     * @param commandBuffer 命令缓冲�?
     * @param frameIndex 帧索�?
     * @param modelMatrix 模型变换矩阵
     */
    void recordCommands(VkCommandBuffer commandBuffer, 
                       uint32_t frameIndex,
                       const glm::mat4& modelMatrix);
    
    /**
     * 录制渲染命令（多网格模式�?
     * 使用网格名称到模型矩阵的映射来渲染所有网�?
     * @param commandBuffer 命令缓冲�?
     * @param frameIndex 帧索�?
     * @param meshMatrices 网格名称到模型矩阵的映射
     */
    void recordCommandsMultiMesh(VkCommandBuffer commandBuffer,
                                 uint32_t frameIndex,
                                 const std::unordered_map<std::string, glm::mat4>& meshMatrices);
    
    /**
     * 录制渲染命令（使�?GPU LOD 选择结果�?
     * 只渲�?GPU 剔除后选择�?Cluster
     * @param commandBuffer 命令缓冲�?
     * @param frameIndex 帧索�?
     * @param meshMatrices 网格名称到模型矩阵的映射
     * @param naniteManager Nanite 管理器（用于获取可见 cluster 列表�?
     */
    void recordCommandsWithLOD(VkCommandBuffer commandBuffer,
                               uint32_t frameIndex,
                               const std::unordered_map<std::string, glm::mat4>& meshMatrices,
                               Nanite::NaniteManager* naniteManager);
    
    /**
     * 更新 Uniform Buffer
     * @param frameIndex 帧索�?
     * @param viewMatrix 视图矩阵
     * @param projMatrix 投影矩阵
     * @param viewPos 相机位置
     * @param lightPos 光源位置
     * @param lightColor 光源颜色
     */
    void updateUniforms(uint32_t frameIndex,
                       const glm::mat4& viewMatrix,
                       const glm::mat4& projMatrix,
                       const glm::vec3& viewPos,
                       const glm::vec3& lightPos,
                       const glm::vec3& lightColor);
    
    /**
     * 窗口大小改变时重建资�?
     */
    void resize(uint32_t width, uint32_t height) override;
    
    /**
     * 设置调试模式
     */
    void setDebugMode(NaniteDebugMode mode) { m_debugMode = mode; }
    NaniteDebugMode getDebugMode() const { return m_debugMode; }
    
    /**
     * 循环切换调试模式
     */
    void cycleDebugMode();
    
    /**
     * 获取当前模式名称
     */
    const char* getDebugModeName() const;
    
    /**
     * 设置要渲染的网格名称（支持多网格�?
     * 如果传入空字符串，则渲染所有已聚类的网�?
     */
    void setTargetMesh(const std::string& meshName) { 
        if (m_targetMeshName != meshName) {
            m_targetMeshName = meshName; 
            m_renderDataBuilt = false;  // 重置渲染数据，需要重新构�?
        }
    }
    
    /**
     * 设置渲染所有已聚类的网�?
     * 只有在模式切换时才重置渲染数�?
     */
    void setRenderAllMeshes() {
        if (!m_renderAllMeshes) {
            m_renderAllMeshes = true;
            m_renderDataBuilt = false;
        }
    }
    
    /**
     * 是否有可渲染�?Cluster 数据
     */
    bool hasClusterData() const;
    
    /**
     * 确保渲染数据已构建（�?RenderPass 之前调用�?
     * 这个方法会触发数据上传，必须�?RenderPass 之前调用
     */
    void ensureRenderDataBuilt();
    
    /**
     * 设置 ClusterCullingPass 引用（用于获�?GPU culling 结果�?
     */
    void setClusterCullingPass(Nanite::ClusterCullingPass* cullingPass) {
        m_clusterCullingPass = cullingPass;
    }

private:
    // 创建管线布局
    void createPipelineLayout();
    
    // 创建图形管线
    void createGraphicsPipeline(VkRenderPass renderPass);
    
    // 创建描述符资�?
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createDescriptorSets();
    
    // 创建 Uniform Buffer
    void createUniformBuffers();
    
    // 创建用于渲染 Cluster 的顶�?索引缓冲
    void createClusterBuffers();
    
    // �?ClusterizedMesh 构建渲染数据
    void buildRenderData();
    
    // 设备引用
    std::shared_ptr<VulkanDevice> m_device;
    std::shared_ptr<VulkanSwapChain> m_swapChain;
    std::shared_ptr<Nanite::NaniteManager> m_naniteManager;
    
    // Vulkan 资源
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descriptorSets;
    
    // Uniform Buffers
    std::vector<std::unique_ptr<VulkanBuffer>> m_uniformBuffers;
    
    // 渲染数据结构
    struct ClusterRenderData {
        uint32_t vertexOffset;      // 在全局顶点缓冲中的偏移
        uint32_t indexOffset;       // 在全局索引缓冲中的偏移
        uint32_t indexCount;        // 索引数量
        uint32_t clusterIndex;      // Cluster 索引（全局�?
    };
    
    // 每个网格的渲染信�?
    struct MeshRenderInfo {
        std::string meshName;                       // 网格名称
        std::vector<ClusterRenderData> clusters;    // 该网格的所�?Cluster 数据
        glm::mat4 modelMatrix;                      // 该网格的模型变换矩阵
    };
    
    std::vector<ClusterRenderData> m_clusterRenderData;     // 兼容单网格模�?
    std::vector<MeshRenderInfo> m_meshRenderInfos;          // 多网格模式的渲染数据
    
    // 顶点和索引缓�?
    std::unique_ptr<VulkanBuffer> m_vertexBuffer;
    std::unique_ptr<VulkanBuffer> m_indexBuffer;
    
    uint32_t m_totalVertexCount = 0;
    uint32_t m_totalIndexCount = 0;
    uint32_t m_totalClusterCount = 0;
    uint32_t m_lod0ClusterCount = 0;   // LOD0 �?cluster 数量（用于调试渲染）
    
    // 调试模式
    NaniteDebugMode m_debugMode = NaniteDebugMode::ClusterColor;
    
    // 目标网格
    std::string m_targetMeshName;
    bool m_renderAllMeshes = false;     // 是否渲染所有网�?
    
    // 状�?
    bool m_initialized = false;
    bool m_renderDataBuilt = false;
    
    // GPU Culling Pass 引用
    Nanite::ClusterCullingPass* m_clusterCullingPass = nullptr;
};
