#pragma once

/**
 * NaniteDebugPass.h - Nanite Cluster 调试可视化渲染通道 (GPU-driven, Phase 5)
 *
 * 功能：
 * - GPU 展开可见 cluster 几何(build_visible_geometry.comp)后,
 *   单次 drawIndirect 绘制全部可见 cluster —— 无 CPU 回读绘制循环
 * - 支持多种调试模式：Cluster 颜色、法线、LOD、哈希色
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
 * Cluster 调试 Push Constants(GPU-driven 版:per-cluster 数据已移入 SSBO)
 */
struct ClusterDebugPushConstants {
    uint32_t totalClusters;
    uint32_t debugMode;
    uint32_t pad0;
    uint32_t pad1;
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
 * NaniteDebugPass - Nanite 调试渲染通道 (GPU-driven indirect)
 */
class NaniteDebugPass : public RenderPassBase {
public:
    NaniteDebugPass(RHIDevice* rhiDevice,
                    RHISwapChain* rhiSwapChain,
                    std::shared_ptr<Nanite::NaniteManager> naniteManager);
    
    ~NaniteDebugPass() override;
    
    /**
     * 初始化渲染通道
     * @param renderPass 外部渲染通道 (external, NOT owned)
     */
    void initialize(RHIRenderPass* externalRenderPass);
    
    void cleanup();
    
    /**
     * GPU-driven 间接绘制准备(render pass 之外调用):
     * drawArgs 重置 → 几何展开 compute dispatch → expVB/drawArgs 屏障。
     * 必须在 cluster culling dispatch 之后、绘制之前调用。
     */
    void prepareIndirectDraw(RHICommandBuffer* cmd);
    
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
    
    // 诊断: 强制只绘制指定 LOD（-1 = 关闭, 0..7 = 层级;GPU 侧过滤）
    void setForceLOD(int level) { m_forceLOD = level; }

    // 每帧实际绘制的统计(基于上一帧 readback 的可见列表;绘制本身完全 GPU 驱动)
    struct DrawnStats {
        uint32_t totalClusters = 0;
        uint32_t visibleClusters = 0;
        uint32_t drawnClusters = 0;
        uint32_t drawnTriangles = 0;
        uint32_t drawnVertices = 0;
        uint32_t lodClusterCounts[8] = { 0 };
    };
    const DrawnStats& getDrawnStats() const { return m_drawnStats; }
    
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
    void createCompactionPipeline();
    void createPipeline();
    void createUniformBuffers();
    void createDescriptorSets();
    void buildRenderData();
    void updateCompactionBindings(RHIBuffer* transformBuffer);

    // RHI device & swap chain
    RHIDevice* rhiDevice_ = nullptr;
    RHISwapChain* rhiSwapChain_ = nullptr;
    RHIRenderPass* externalRenderPass_ = nullptr;  // NOT owned

    // Nanite manager
    std::shared_ptr<Nanite::NaniteManager> m_naniteManager;
    
    // ---- 绘制管线(单 drawIndirect) ----
    std::shared_ptr<RHIPipeline>       m_pipeline_;
    std::shared_ptr<RHIBindingLayout>  m_bindingLayout_;
    
    // ---- 几何展开 compute 管线 ----
    std::shared_ptr<RHIPipeline>       m_compactionPipeline_;
    std::shared_ptr<RHIBindingLayout>  m_compactionLayout_;
    std::shared_ptr<RHIBindingGroup>   m_compactionGroup_;
    bool m_compactionBindingsDirty = true;
    RHIBuffer* m_boundTransformBuffer_ = nullptr;      // tracked for dirty
    std::shared_ptr<RHIBuffer> m_dummyTransformBuffer_; // mesh 变换未上传时兜底
    
    // Per-frame UBOs (RHI)
    std::vector<std::shared_ptr<RHIBuffer>> m_uniformBuffers_;

    // Binding groups (Pure RHI)
    std::vector<std::shared_ptr<RHIBindingGroup>> m_bindingGroups_;
    
    // 渲染数据结构(stats / 几何表构建)
    struct ClusterRenderData {
        uint32_t vertexOffset;
        uint32_t indexOffset;
        uint32_t indexCount;
        uint32_t clusterIndex;
        uint32_t lodLevel;
        uint32_t vertexCount;
    };
    
    struct MeshRenderInfo {
        std::string meshName;
        std::vector<ClusterRenderData> clusters;
        glm::mat4 modelMatrix;
    };
    
    std::vector<ClusterRenderData> m_clusterRenderData;
    std::vector<MeshRenderInfo> m_meshRenderInfos;
    
    // 源几何(合并大缓冲,SSBO 只读;GPU 展开的输入)
    std::shared_ptr<RHIBuffer> m_vertexBuffer_;   // 44B interleaved, Storage
    std::shared_ptr<RHIBuffer> m_indexBuffer_;    // uint32 全局重定位, Storage
    std::shared_ptr<RHIBuffer> m_geomTableBuffer_;// 每 cluster 32B 静态表
    
    // GPU-driven 输出
    std::shared_ptr<RHIBuffer> m_expVertexBuffer_; // 48B/corner, Storage|Vertex
    std::shared_ptr<RHIBuffer> m_drawArgsBuffer_;  // 16B VkDrawIndirectCommand, Storage|Indirect
    
    uint32_t m_totalVertexCount = 0;
    uint32_t m_totalIndexCount = 0;
    uint32_t m_totalClusterCount = 0;
    uint32_t m_lod0ClusterCount = 0;
    
    NaniteDebugMode m_debugMode = NaniteDebugMode::ClusterColor;
    int m_forceLOD = -1;
    DrawnStats m_drawnStats;
    
    std::string m_targetMeshName;
    bool m_renderAllMeshes = false;
    
    bool m_initialized = false;
    bool m_renderDataBuilt = false;
    
    Nanite::ClusterCullingPass* m_clusterCullingPass = nullptr;
};
