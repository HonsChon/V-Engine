#pragma once

/**
 * NaniteManager.h - Nanite 系统管理器
 * 
 * 负责：
 * - 网格的Cluster 化处理
 * - Cluster 数据向GPU 上传
 * - 运行的Cluster 剔除
 * - 统计信息收集
 */

#include "Nanite.h"
#include "ClusterCullingPass.h"
#include <memory>
#include <unordered_map>

// 前向声明
class RHIDevice;
class RHICommandBuffer;
class RHIBuffer;

namespace Nanite {

/**
 * NaniteManager - Nanite 系统的核心管理器
 */
class NaniteManager {
public:
    NaniteManager(RHIDevice* rhiDevice);
    ~NaniteManager();
    
    /**
     * 初始化Nanite 系统
     */
    void initialize();
    
    /**
     * 清理资源
     */
    void cleanup();
    
    /**
     * 处理网格，生成Cluster 化数量
     * @param mesh 输入网格
     * @param meshName 网格名称（用作缓存键值
     * @return Cluster 化后的网格（共享所有权）
     */
    std::shared_ptr<ClusterizedMesh> processMesh(const InputMesh& mesh, 
                                                  const std::string& meshName);
    
    /**
     * 获取已处理的网格
     * @param meshName 网格名称
     * @return Cluster 化后的网格，如果不存在返回nullptr
     */
    std::shared_ptr<ClusterizedMesh> getMesh(const std::string& meshName) const;
    
    /**
     * 获取所有已处理的网格名称
     * @return 网格名称列表
     */
    std::vector<std::string> getAllMeshNames() const;
    
    /**
     * 获取所有 GPU cluster 数据（用于 CPU 端 LOD 选择）
     */
    const std::vector<GPUClusterData>& getAllGPUClusterData() const { return m_allGPUClusterData; }
    
    /**
     * 获取上一帧的相机位置
     */
    glm::vec3 getLastCameraPosition() const { return m_lastCameraPosition; }
    
    /**
     * 上传 Cluster 数据向GPU
     * 在所有网格处理完成后调用
     */
    void uploadToGPU();
    
    /**
     * 执行 Cluster 剔除（在 Compute Pass 中调用）
     * @param cmd RHI 命令缓冲区
     * @param viewMatrix 视图矩阵
     * @param projMatrix 投影矩阵
     * @param cameraPosition 相机世界坐标
     * @param frameIndex 当前帧索引（用于双缓冲同步）
     */
    void performCulling(RHICommandBuffer* cmd,
                       const glm::mat4& viewMatrix,
                       const glm::mat4& projMatrix,
                       const glm::vec3& cameraPosition,
                       uint32_t frameIndex = 0);
    
    /**
     * 读取可见 Cluster 列表（GPU -> CPU）
     * @return 可见 Cluster 的索引列表
     */
    const std::vector<uint32_t>& getVisibleClusters();
    
    /**
     * 获取统计信息
     */
    const NaniteStats& getStats() const { return m_stats; }
    
    /**
     * 配置访问
     */
    void setConfig(const NaniteConfig& config) { m_config = config; }
    const NaniteConfig& getConfig() const { return m_config; }
    
    /**
     * 获取总Cluster 数量
     */
    uint32_t getTotalClusterCount() const { return m_totalClusterCount; }
    
    /**
     * 获取 Cluster 数据缓冲区（用于渲染）
     */
    RHIBuffer* getClusterDataBuffer() const;
    RHIBuffer* getVisibleIndicesBuffer() const;

private:
    // GPU 缓冲区
    void createGPUBuffers();
    void updateUniformBuffer(const glm::mat4& viewMatrix,
                            const glm::mat4& projMatrix,
                            const glm::vec3& cameraPosition);
    
    // 从视图投影矩阵提取视锥平面
    static void extractFrustumPlanes(const glm::mat4& viewProj, glm::vec4 planes[6]);
    
    RHIDevice* m_rhiDevice = nullptr;
    
    // Cluster 化处理器
    std::unique_ptr<MeshClusterizer> m_clusterizer;
    
    // 已处理的网格缓存
    std::unordered_map<std::string, std::shared_ptr<ClusterizedMesh>> m_meshCache;
    
    // GPU 缓冲区 (Pure RHI)
    std::unique_ptr<RHIBuffer> m_transformBuffer;
    std::unique_ptr<RHIBuffer> m_uniformBuffer;
    std::unique_ptr<RHIBuffer> m_visibleIndicesBuffer;
    std::unique_ptr<RHIBuffer> m_counterBuffer;
    std::unique_ptr<RHIBuffer> m_readbackBuffer;
    
    // RHI buffer for cluster data (used by ClusterCullingPass)
    std::unique_ptr<RHIBuffer> m_clusterDataBufferRHI;
    
    // GPU Cluster Culling Pass
    std::unique_ptr<ClusterCullingPass> m_cullingPass;
    
    // 统计和状态
    NaniteConfig m_config;
    NaniteStats m_stats;
    uint32_t m_totalClusterCount = 0;
    std::vector<uint32_t> m_visibleClustersCPU;
    
    // 屏幕参数（用于LOD 选择）
    float m_screenWidth = 1920.0f;
    float m_screenHeight = 1080.0f;
    
    // 缓存的矩阵（用于 uniform 更新）
    glm::mat4 m_lastViewMatrix{1.0f};
    glm::mat4 m_lastProjMatrix{1.0f};
    
    bool m_gpuDataDirty = true;
    bool m_initialized = false;
    
    // 缓存的 GPU 数据和相机位置（用于 CPU 端 LOD 选择）
    std::vector<GPUClusterData> m_allGPUClusterData;
    glm::vec3 m_lastCameraPosition{0.0f};
    
public:
    // 设置屏幕尺寸（用于LOD 计算）
    void setScreenSize(float width, float height) {
        m_screenWidth = width;
        m_screenHeight = height;
    }
    
    // 设置屏幕参数（宽高）- 别名
    void setScreenParams(uint32_t width, uint32_t height) {
        m_screenWidth = static_cast<float>(width);
        m_screenHeight = static_cast<float>(height);
    }
    
    // 更新视图/投影矩阵
    void setViewProjection(const glm::mat4& view, const glm::mat4& proj) {
        m_lastViewMatrix = view;
        m_lastProjMatrix = proj;
    }
    
    // 获取 GPU 可见 Cluster 数量
    uint32_t getVisibleClusterCount() const;
    
    // 获取 GPU 可见 Cluster 索引列表（同步读取，谨慎使用）
    const std::vector<uint32_t>& getVisibleClusterIndices();
    
    // 获取 ClusterCullingPass（用于访问GPU 选择结果）
    ClusterCullingPass* getCullingPass() const { return m_cullingPass.get(); }
    
    // 帧结束后读取 GPU 数据（在 vkWaitForFences 之后、命令录制之前调用）
    // @param frameIndex 当前帧索引（与渲染器的currentFrame 对应）
    void readbackCullingResults(uint32_t frameIndex) {
        if (m_cullingPass) {
            m_cullingPass->readbackData(frameIndex);
        }
    }
};

} // namespace Nanite
