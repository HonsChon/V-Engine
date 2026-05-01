#pragma once

/**
 * NaniteDebugPass.h - Nanite Cluster 调试可视化渲染通道 (RHI)
 * 
 * 功能：
 * - 使用不同颜色渲染每个 Cluster，便于可视化分割结果
 * - 支持多种调试模式：Cluster 颜色、法线、LOD 等
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

class Scene;
class RHIDevice;
class RHISwapChain;
class RHIBuffer;
class RHIPipeline;
class RHIBindingLayout;
class RHIBindingGroup;
class RHICommandBuffer;
class RHIRenderPass;

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
    Normal = 1,         // 法线可视区
    LOD = 2,            // LOD 级别可视区
    HashColor = 3       // 哈希随机颜色（高对比度）
};

// UBO 结构体
struct NaniteDebugUBO {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 viewPos;
    glm::vec4 lightPos;
    glm::vec4 lightColor;
};

/**
 * NaniteDebugPass - Nanite 调试渲染通道 (RHI)
 */
class NaniteDebugPass : public RenderPassBase {
public:
    NaniteDebugPass(RHIDevice* rhiDevice,
                    RHISwapChain* rhiSwapChain,
                    std::shared_ptr<Nanite::NaniteManager> naniteManager);
    
    ~NaniteDebugPass() override;
    
    /**
     * 初始化渲染通道
     * @param renderPass Vulkan 渲染通道句柄 (external, NOT owned)
     */
    void initialize(RHIRenderPass* externalRenderPass);
    
    void cleanup();
    
    void recordCommands(RHICommandBuffer* cmd, 
                       uint32_t frameIndex,
                       const glm::mat4& modelMatrix);
    
    void recordCommandsMultiMesh(RHICommandBuffer* cmd,
                                 uint32_t frameIndex,
                                 const std::unordered_map<std::string, glm::mat4>& meshMatrices);
    
    void recordCommandsWithLOD(RHICommandBuffer* cmd,
                               uint32_t frameIndex,
                               const std::unordered_map<std::string, glm::mat4>& meshMatrices,
                               Nanite::NaniteManager* naniteManager);
    
    void updateUniforms(uint32_t frameIndex,
                       const glm::mat4& viewMatrix,
                       const glm::mat4& projMatrix,
                       const glm::vec3& viewPos,
                       const glm::vec3& lightPos,
                       const glm::vec3& lightColor);
    
    void resize(uint32_t width, uint32_t height) override;
    
    void setDebugMode(NaniteDebugMode mode) { m_debugMode = mode; }
    NaniteDebugMode getDebugMode() const { return m_debugMode; }
    void cycleDebugMode();
    const char* getDebugModeName() const;
    
    void setTargetMesh(const std::string& meshName) { 
        if (m_targetMeshName != meshName) {
            m_targetMeshName = meshName; 
            m_renderDataBuilt = false;
        }
    }
    
    void setRenderAllMeshes() {
        if (!m_renderAllMeshes) {
            m_renderAllMeshes = true;
            m_renderDataBuilt = false;
        }
    }
    
    bool hasClusterData() const;
    void ensureRenderDataBuilt();
    
    void setClusterCullingPass(Nanite::ClusterCullingPass* cullingPass) {
        m_clusterCullingPass = cullingPass;
    }

private:
    void createBindingLayout();
    void createPipeline();
    void createUniformBuffers();
    void createDescriptorSets();
    void buildRenderData();

    // RHI device & swap chain
    RHIDevice* rhiDevice_ = nullptr;
    RHISwapChain* rhiSwapChain_ = nullptr;
    RHIRenderPass* externalRenderPass_ = nullptr;  // NOT owned

    // Nanite manager
    std::shared_ptr<Nanite::NaniteManager> m_naniteManager;
    
    // RHI resources
    std::unique_ptr<RHIPipeline>       m_pipeline_;
    std::unique_ptr<RHIBindingLayout>  m_bindingLayout_;

    // Per-frame UBOs (RHI)
    std::vector<std::unique_ptr<RHIBuffer>> m_uniformBuffers_;

    // Binding groups (Pure RHI)
    std::vector<std::unique_ptr<RHIBindingGroup>> m_bindingGroups_;
    
    // 渲染数据结构
    struct ClusterRenderData {
        uint32_t vertexOffset;
        uint32_t indexOffset;
        uint32_t indexCount;
        uint32_t clusterIndex;
    };
    
    struct MeshRenderInfo {
        std::string meshName;
        std::vector<ClusterRenderData> clusters;
        glm::mat4 modelMatrix;
    };
    
    std::vector<ClusterRenderData> m_clusterRenderData;
    std::vector<MeshRenderInfo> m_meshRenderInfos;
    
    // Vertex/Index buffers (RHI)
    std::unique_ptr<RHIBuffer> m_vertexBuffer_;
    std::unique_ptr<RHIBuffer> m_indexBuffer_;
    
    uint32_t m_totalVertexCount = 0;
    uint32_t m_totalIndexCount = 0;
    uint32_t m_totalClusterCount = 0;
    uint32_t m_lod0ClusterCount = 0;
    
    NaniteDebugMode m_debugMode = NaniteDebugMode::ClusterColor;
    
    std::string m_targetMeshName;
    bool m_renderAllMeshes = false;
    
    bool m_initialized = false;
    bool m_renderDataBuilt = false;
    
    Nanite::ClusterCullingPass* m_clusterCullingPass = nullptr;
};