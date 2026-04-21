/**
 * NaniteManager.cpp - Nanite 系统管理器实�?
 */

#include "NaniteManager.h"
#include "VulkanDevice.h"
#include "VulkanBuffer.h"

#include <algorithm>
#include <iostream>
#include <chrono>

namespace Nanite {

// ============================================================================
// 构�?析构
// ============================================================================

NaniteManager::NaniteManager(std::shared_ptr<VulkanDevice> device)
    : m_device(std::move(device))
{
    m_clusterizer = std::make_unique<MeshClusterizer>();
}

NaniteManager::~NaniteManager() {
    cleanup();
}

void NaniteManager::initialize() {
    if (m_initialized) return;
    
    std::cout << "[Nanite] Initializing Nanite system v" 
              << VERSION_MAJOR << "." << VERSION_MINOR << std::endl;
    
    // 创建 GPU Cluster Culling Pass
    m_cullingPass = std::make_unique<ClusterCullingPass>(m_device);
    m_cullingPass->init();
    
    // 创建 GPU 缓冲区将�?uploadToGPU 时进�?
    m_initialized = true;
}

void NaniteManager::cleanup() {
    if (!m_initialized) return;
    
    // 等待 GPU 完成
    if (m_device) {
        vkDeviceWaitIdle(m_device->getDevice());
    }
    
    // 清理 culling pass
    if (m_cullingPass) {
        m_cullingPass->cleanup();
        m_cullingPass.reset();
    }
    
    // 清理缓冲�?
    m_clusterDataBuffer.reset();
    m_transformBuffer.reset();
    m_uniformBuffer.reset();
    m_visibleIndicesBuffer.reset();
    m_counterBuffer.reset();
    m_readbackBuffer.reset();
    
    // 清理缓存
    m_meshCache.clear();
    
    m_initialized = false;
    std::cout << "[Nanite] Cleanup complete" << std::endl;
}

// ============================================================================
// 网格处理
// ============================================================================

std::shared_ptr<ClusterizedMesh> NaniteManager::processMesh(
    const InputMesh& mesh, 
    const std::string& meshName) 
{
    // 检查缓�?
    auto it = m_meshCache.find(meshName);
    if (it != m_meshCache.end()) {
        std::cout << "[Nanite] Using cached mesh: " << meshName << std::endl;
        return it->second;
    }
    
    std::cout << "[Nanite] Processing mesh: " << meshName 
              << " (" << mesh.triangleCount() << " triangles)" << std::endl;
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Cluster 化处�?
    auto uniqueMesh = m_clusterizer->clusterize(mesh);
    auto clusterizedMesh = std::shared_ptr<ClusterizedMesh>(uniqueMesh.release());
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    std::cout << "[Nanite] Generated " << clusterizedMesh->clusters.size() 
              << " clusters in " << duration.count() << " ms" << std::endl;
    
    // 输出 LOD 层级信息
    if (clusterizedMesh->lodLevels.size() > 1) {
        std::cout << "[Nanite] LOD Hierarchy: " << clusterizedMesh->lodLevels.size() 
                  << " levels" << std::endl;
        for (size_t i = 0; i < clusterizedMesh->lodLevels.size(); ++i) {
            const auto& lod = clusterizedMesh->lodLevels[i];
            std::cout << "  LOD " << i << ": " << lod.clusterCount 
                      << " clusters (start: " << lod.clusterStartIndex 
                      << ", error: " << lod.maxError << ")" << std::endl;
        }
    }
    
    // 缓存并标记需要更�?GPU
    m_meshCache[meshName] = clusterizedMesh;
    m_gpuDataDirty = true;
    
    return clusterizedMesh;
}

std::shared_ptr<ClusterizedMesh> NaniteManager::getMesh(const std::string& meshName) const {
    auto it = m_meshCache.find(meshName);
    return (it != m_meshCache.end()) ? it->second : nullptr;
}

std::vector<std::string> NaniteManager::getAllMeshNames() const {
    std::vector<std::string> names;
    names.reserve(m_meshCache.size());
    for (const auto& [name, mesh] : m_meshCache) {
        names.push_back(name);
    }
    // 必须排序！确保与 uploadToGPU() �?buildRenderData() 中的遍历顺序一�?
    // 否则 GPU culling 返回�?clusterIndex �?CPU 渲染端不匹配
    std::sort(names.begin(), names.end());
    return names;
}

// ============================================================================
// GPU 操作
// ============================================================================

void NaniteManager::uploadToGPU() {
    if (!m_gpuDataDirty) return;
    
    // 收集所�?Cluster 数据
    // 重要：必须按照确定性顺序（字典序）遍历 mesh
    // 这与 NaniteDebugPass::buildRenderData() 中的顺序必须一�?
    // 否则 GPU culling 返回�?clusterIndex �?CPU 端的不匹�?
    std::vector<GPUClusterData> allClusterData;
    
    // 先收集所�?mesh 名称并排�?
    std::vector<std::string> sortedMeshNames;
    sortedMeshNames.reserve(m_meshCache.size());
    for (const auto& [name, mesh] : m_meshCache) {
        sortedMeshNames.push_back(name);
    }
    std::sort(sortedMeshNames.begin(), sortedMeshNames.end());
    
    // 按排序后的顺序遍历，同时修正 parentGroupIndex 为全局索引
    for (const auto& name : sortedMeshNames) {
        const auto& mesh = m_meshCache.at(name);
        uint32_t meshBaseOffset = static_cast<uint32_t>(allClusterData.size());
        
        for (const auto& cluster : mesh->clusters) {
            GPUClusterData data = cluster.gpuData;
            
            // 将 mesh 内部的本地 parentGroupIndex 转换为全局索引
            if (data.parentGroupIndex != 0xFFFFFFFF) {
                data.parentGroupIndex += meshBaseOffset;
            }
            // childStartIndex 也需要偏移（如果有子节点）
            if (data.childCount > 0) {
                data.childStartIndex += meshBaseOffset;
            }
            
            allClusterData.push_back(data);
        }
    }
    
    m_totalClusterCount = static_cast<uint32_t>(allClusterData.size());
    
    if (m_totalClusterCount == 0) {
        std::cout << "[Nanite] No clusters to upload" << std::endl;
        return;
    }
    
    std::cout << "[Nanite] Uploading " << m_totalClusterCount 
              << " clusters to GPU" << std::endl;
    
    // 调试：验证上传的 lodLevel 分布
    std::unordered_map<uint32_t, uint32_t> lodCounts;
    uint32_t validParentCount = 0;
    for (size_t i = 0; i < allClusterData.size(); ++i) {
        const auto& data = allClusterData[i];
        lodCounts[data.lodLevel]++;
        if (data.parentGroupIndex != 0xFFFFFFFF && data.parentGroupIndex < allClusterData.size()) {
            validParentCount++;
        }
    }
    
    std::cout << "[Nanite] GPU Data LOD distribution:" << std::endl;
    for (const auto& [lod, count] : lodCounts) {
        std::cout << "  LOD " << lod << ": " << count << " clusters" << std::endl;
    }
    std::cout << "  Clusters with valid parent: " << validParentCount << "/" << allClusterData.size() << std::endl;
    
    // 验证结构体布局
    {
        GPUClusterData testData{};
        std::cout << "[Nanite] GPUClusterData layout verification:" << std::endl;
        std::cout << "  sizeof(GPUClusterData) = " << sizeof(GPUClusterData) << std::endl;
        std::cout << "  offset boundingSphere = " << offsetof(GPUClusterData, boundingSphere) << std::endl;
        std::cout << "  offset aabbMin = " << offsetof(GPUClusterData, aabbMin) << std::endl;
        std::cout << "  offset aabbMax = " << offsetof(GPUClusterData, aabbMax) << std::endl;
        std::cout << "  offset normalCone = " << offsetof(GPUClusterData, normalCone) << std::endl;
        std::cout << "  offset vertexOffset = " << offsetof(GPUClusterData, vertexOffset) << std::endl;
        std::cout << "  offset indexOffset = " << offsetof(GPUClusterData, indexOffset) << std::endl;
        std::cout << "  offset triangleCount = " << offsetof(GPUClusterData, triangleCount) << std::endl;
        std::cout << "  offset lodLevel = " << offsetof(GPUClusterData, lodLevel) << std::endl;
        std::cout << "  offset parentGroupIndex = " << offsetof(GPUClusterData, parentGroupIndex) << std::endl;
        std::cout << "  offset flags = " << offsetof(GPUClusterData, flags) << std::endl;
        std::cout << "  offset childStartIndex = " << offsetof(GPUClusterData, childStartIndex) << std::endl;
        std::cout << "  offset childCount = " << offsetof(GPUClusterData, childCount) << std::endl;
        
        // 验证实际数据：打印第一个非 LOD0 cluster 的 raw bytes
        for (size_t i = 0; i < allClusterData.size(); ++i) {
            if (allClusterData[i].lodLevel > 0) {
                const auto& d = allClusterData[i];
                std::cout << "  First non-LOD0 cluster [" << i << "]:" << std::endl;
                std::cout << "    lodLevel = " << d.lodLevel << std::endl;
                std::cout << "    parentGroupIndex = " << d.parentGroupIndex << std::endl;
                std::cout << "    flags = " << d.flags << std::endl;
                std::cout << "    triangleCount = " << d.triangleCount << std::endl;
                
                // 打印 offset 76-79 处的原始 bytes (lodLevel 的位置)
                const uint8_t* raw = reinterpret_cast<const uint8_t*>(&d);
                std::cout << "    raw bytes at offset 76-79: ";
                for (int b = 76; b < 80; ++b) {
                    printf("%02x ", raw[b]);
                }
                std::cout << std::endl;
                break;
            }
        }
    }
    
    // 打印 lodError 范围用于调试
    float maxLodError = 0.0f;
    float minNonZeroLodError = FLT_MAX;
    for (const auto& data : allClusterData) {
        if (data.aabbMin.w > 0.0f) {
            maxLodError = std::max(maxLodError, data.aabbMin.w);
            minNonZeroLodError = std::min(minNonZeroLodError, data.aabbMin.w);
        }
    }
    std::cout << "[Nanite] lodError range: [" << minNonZeroLodError << ", " << maxLodError 
              << "], lodErrorScale=" << m_config.lodErrorScale 
              << ", threshold=" << m_config.screenSpaceErrorThreshold << std::endl;
    
    // 打印样本数据（包括 lodError）
    std::cout << "[Nanite] Sample GPU data:" << std::endl;
    for (size_t i = 0; i < std::min(allClusterData.size(), size_t(5)); ++i) {
        const auto& d = allClusterData[i];
        std::cout << "  [" << i << "] LOD=" << d.lodLevel 
                  << ", parent=" << (d.parentGroupIndex == 0xFFFFFFFF ? -1 : (int)d.parentGroupIndex)
                  << ", flags=" << d.flags 
                  << ", lodError=" << d.aabbMin.w
                  << ", childCount=" << d.childCount
                  << std::endl;
    }
    // 打印每个 LOD 级别的第一个 cluster
    for (const auto& [lod, count] : lodCounts) {
        if (lod == 0) continue;
        for (size_t i = 0; i < allClusterData.size(); ++i) {
            if (allClusterData[i].lodLevel == lod) {
                const auto& d = allClusterData[i];
                std::cout << "  LOD" << lod << " first [" << i << "] parent=" 
                          << (d.parentGroupIndex == 0xFFFFFFFF ? -1 : (int)d.parentGroupIndex)
                          << ", lodError=" << d.aabbMin.w
                          << ", childCount=" << d.childCount
                          << ", childStart=" << d.childStartIndex
                          << std::endl;
                break;
            }
        }
    }
    
    // ===== CPU 模拟 Shader LOD 选择（调试） =====
    {
        float projScaleY = 2.414f; // cot(22.5°) for FOV=45°
        float screenH = static_cast<float>(m_screenHeight);
        float threshold = m_config.screenSpaceErrorThreshold;
        float errorScale = m_config.lodErrorScale;
        glm::vec3 fakeCameraPos(0, 2, 5); // 假设的相机位置
        
        std::cout << "[Nanite] CPU LOD simulation (cameraPos=" << fakeCameraPos.x << "," << fakeCameraPos.y << "," << fakeCameraPos.z 
                  << ", threshold=" << threshold << ", errorScale=" << errorScale << "):" << std::endl;
        
        // 对每个 LOD 的第一个 cluster 模拟判断
        for (const auto& [lod, count] : lodCounts) {
            for (size_t i = 0; i < allClusterData.size(); ++i) {
                if (allClusterData[i].lodLevel == lod) {
                    const auto& d = allClusterData[i];
                    glm::vec3 center(d.boundingSphere.x, d.boundingSphere.y, d.boundingSphere.z);
                    float radius = d.boundingSphere.w;
                    float dist = glm::length(center - fakeCameraPos);
                    float lodError = d.aabbMin.w;
                    
                    float screenError = (lodError * projScaleY * screenH * 0.5f) / std::max(dist, 0.001f) * errorScale;
                    
                    bool selfOK = (screenError <= threshold);
                    
                    bool parentNotOK = true;
                    bool isRoot = (d.parentGroupIndex == 0xFFFFFFFF);
                    if (!isRoot && d.parentGroupIndex < allClusterData.size()) {
                        const auto& p = allClusterData[d.parentGroupIndex];
                        glm::vec3 pCenter(p.boundingSphere.x, p.boundingSphere.y, p.boundingSphere.z);
                        float pRadius = p.boundingSphere.w;
                        float pDist = glm::length(pCenter - fakeCameraPos);
                        float pError = p.aabbMin.w;
                        float pScreenError = (pError * projScaleY * screenH * 0.5f) / std::max(pDist, 0.001f) * errorScale;
                        parentNotOK = (pScreenError > threshold);
                        
                        std::cout << "  LOD" << lod << " [" << i << "]: dist=" << dist 
                                  << ", lodError=" << lodError 
                                  << ", screenErr=" << screenError 
                                  << ", selfOK=" << selfOK
                                  << " | parent[" << d.parentGroupIndex << "]: pErr=" << pError 
                                  << ", pScreenErr=" << pScreenError
                                  << ", parentNotOK=" << parentNotOK
                                  << " → " << ((selfOK && parentNotOK) ? "RENDER" : "SKIP")
                                  << std::endl;
                    } else {
                        std::cout << "  LOD" << lod << " [" << i << "]: dist=" << dist 
                                  << ", lodError=" << lodError 
                                  << ", screenErr=" << screenError 
                                  << ", selfOK=" << selfOK
                                  << " | ROOT"
                                  << " → " << (selfOK ? "RENDER" : "SKIP")
                                  << std::endl;
                    }
                    break;
                }
            }
        }
    }
    
    // 创建或更�?Cluster 数据缓冲�?
    VkDeviceSize clusterBufferSize = sizeof(GPUClusterData) * m_totalClusterCount;
    
    m_clusterDataBuffer = std::make_unique<VulkanBuffer>(
        m_device,
        clusterBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    // 通过 staging buffer 上传
    m_clusterDataBuffer->uploadData(allClusterData.data(), clusterBufferSize);
    
    // 创建可见索引缓冲区（最多和�?Cluster 数量一样大�?
    VkDeviceSize visibleBufferSize = sizeof(uint32_t) * m_totalClusterCount;
    
    m_visibleIndicesBuffer = std::make_unique<VulkanBuffer>(
        m_device,
        visibleBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    // 创建计数器缓冲区（单�?uint32_t�?
    m_counterBuffer = std::make_unique<VulkanBuffer>(
        m_device,
        sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    // 创建 Uniform 缓冲�?
    // ClusterCullingUniforms 结构大小（参�?cluster_culling.comp�?
    constexpr VkDeviceSize uniformSize = 368; // 6 planes * 16 + viewProj + 其他
    
    m_uniformBuffer = std::make_unique<VulkanBuffer>(
        m_device,
        uniformSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    
    // 创建 readback 缓冲�?
    m_readbackBuffer = std::make_unique<VulkanBuffer>(
        m_device,
        sizeof(uint32_t) * (m_totalClusterCount + 1), // +1 for counter
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    
    // �?cluster buffer 绑定�?culling pass
    if (m_cullingPass) {
        m_cullingPass->setClusterBuffer(m_clusterDataBuffer->getBuffer(), m_totalClusterCount);
        if (m_transformBuffer) {
            m_cullingPass->setTransformBuffer(m_transformBuffer->getBuffer());
        }
    }
    
    m_gpuDataDirty = false;
    
    // 缓存 GPU 数据用于 CPU 端 LOD 选择
    m_allGPUClusterData = allClusterData;
    
    std::cout << "[Nanite] GPU upload complete" << std::endl;
}

void NaniteManager::createGPUBuffers() {
    // �?uploadToGPU() 处理
}

void NaniteManager::performCulling(
    VkCommandBuffer commandBuffer,
    const glm::mat4& viewMatrix,
    const glm::mat4& projMatrix,
    const glm::vec3& cameraPosition,
    uint32_t frameIndex) 
{
    if (m_totalClusterCount == 0 || m_gpuDataDirty) return;
    if (!m_cullingPass || !m_cullingPass->isReady()) return;
    
    // 计算 viewProj
    glm::mat4 viewProj = projMatrix * viewMatrix;
    
    // 提取视锥平面
    glm::vec4 frustumPlanes[6];
    extractFrustumPlanes(viewProj, frustumPlanes);
    
    // 构建 uniform 数据
    ClusterCullingUniforms uniforms{};
    uniforms.viewMatrix = viewMatrix;
    uniforms.projMatrix = projMatrix;
    uniforms.viewProjMatrix = viewProj;
    for (int i = 0; i < 6; i++) {
        uniforms.frustumPlanes[i] = frustumPlanes[i];
    }
    uniforms.cameraPosition = glm::vec4(cameraPosition, 1.0f);
    m_lastCameraPosition = cameraPosition;  // 缓存用于 CPU 端 LOD 选择
    uniforms.clusterCountPacked = glm::uvec4(
        m_totalClusterCount,  // x: cluster count
        1,                    // y: enable frustum cull
        1,                    // z: enable cone cull
        0                     // w: disable GPU LOD selection (使用 CPU 端 LOD)
    );
    uniforms.screenParams = glm::vec4(
        m_screenWidth,                          // x: screen width
        m_screenHeight,                         // y: screen height
        m_config.lodErrorScale,                 // z: LOD error scale
        m_config.screenSpaceErrorThreshold      // w: pixel threshold
    );
    
    // 更新 culling pass �?uniform
    m_cullingPass->updateUniforms(uniforms);
    
    // 重置计数器和选择状�?
    m_cullingPass->resetCounters(commandBuffer);
    
    // 执行 compute pass，传递帧索引以实现双缓冲同步
    m_cullingPass->record(commandBuffer, frameIndex);
}

void NaniteManager::updateUniformBuffer(
    const glm::mat4& viewMatrix,
    const glm::mat4& projMatrix,
    const glm::vec3& cameraPosition) 
{
    // 计算 viewProj
    glm::mat4 viewProj = projMatrix * viewMatrix;
    
    // 提取视锥平面
    glm::vec4 frustumPlanes[6];
    extractFrustumPlanes(viewProj, frustumPlanes);
    
    // 构建 uniform 数据（需要与 shader 匹配�?
    // 必须�?cluster_culling.comp 中的 CullingUniforms 完全一�?
    struct ClusterCullingUniforms {
        glm::mat4 viewMatrix;        // 64 bytes
        glm::mat4 projMatrix;        // 64 bytes
        glm::mat4 viewProjMatrix;    // 64 bytes
        glm::vec4 frustumPlanes[6];  // 96 bytes
        glm::vec4 cameraPosition;    // 16 bytes (xyz: position, w: unused)
        glm::uvec4 clusterCountPacked; // 16 bytes (x=count, y=frustumCull, z=coneCull, w=lodSelect)
        glm::vec4 screenParams;      // 16 bytes (x=width, y=height, z=errorScale, w=threshold)
        // Total: 336 bytes
    };
    
    ClusterCullingUniforms uniforms{};
    uniforms.viewMatrix = m_lastViewMatrix;
    uniforms.projMatrix = m_lastProjMatrix;
    uniforms.viewProjMatrix = viewProj;
    for (int i = 0; i < 6; i++) {
        uniforms.frustumPlanes[i] = frustumPlanes[i];
    }
    uniforms.cameraPosition = glm::vec4(cameraPosition, 1.0f);
    uniforms.clusterCountPacked = glm::uvec4(
        m_totalClusterCount,  // x: cluster count
        1,                    // y: enable frustum cull
        1,                    // z: enable cone cull
        1                     // w: enable LOD selection
    );
    uniforms.screenParams = glm::vec4(
        m_screenWidth,                          // x: screen width
        m_screenHeight,                         // y: screen height
        m_config.lodErrorScale,                 // z: LOD error scale
        m_config.screenSpaceErrorThreshold      // w: pixel threshold
    );
    
    // 使用 copyFrom 直接写入（对�?HOST_VISIBLE 缓冲区）
    m_uniformBuffer->copyFrom(&uniforms, sizeof(uniforms));
}

void NaniteManager::extractFrustumPlanes(const glm::mat4& viewProj, glm::vec4 planes[6]) {
    // �?VP 矩阵提取视锥平面（标准算法）
    // Left
    planes[0] = glm::vec4(
        viewProj[0][3] + viewProj[0][0],
        viewProj[1][3] + viewProj[1][0],
        viewProj[2][3] + viewProj[2][0],
        viewProj[3][3] + viewProj[3][0]
    );
    // Right
    planes[1] = glm::vec4(
        viewProj[0][3] - viewProj[0][0],
        viewProj[1][3] - viewProj[1][0],
        viewProj[2][3] - viewProj[2][0],
        viewProj[3][3] - viewProj[3][0]
    );
    // Bottom
    planes[2] = glm::vec4(
        viewProj[0][3] + viewProj[0][1],
        viewProj[1][3] + viewProj[1][1],
        viewProj[2][3] + viewProj[2][1],
        viewProj[3][3] + viewProj[3][1]
    );
    // Top
    planes[3] = glm::vec4(
        viewProj[0][3] - viewProj[0][1],
        viewProj[1][3] - viewProj[1][1],
        viewProj[2][3] - viewProj[2][1],
        viewProj[3][3] - viewProj[3][1]
    );
    // Near
    planes[4] = glm::vec4(
        viewProj[0][3] + viewProj[0][2],
        viewProj[1][3] + viewProj[1][2],
        viewProj[2][3] + viewProj[2][2],
        viewProj[3][3] + viewProj[3][2]
    );
    // Far
    planes[5] = glm::vec4(
        viewProj[0][3] - viewProj[0][2],
        viewProj[1][3] - viewProj[1][2],
        viewProj[2][3] - viewProj[2][2],
        viewProj[3][3] - viewProj[3][2]
    );
    
    // 归一化平�?
    for (int i = 0; i < 6; i++) {
        float length = glm::length(glm::vec3(planes[i]));
        if (length > 0.0f) {
            planes[i] /= length;
        }
    }
}

const std::vector<uint32_t>& NaniteManager::getVisibleClusters() {
    // �?GPU 读取可见 Cluster 列表
    // 这是一个同步操作，通常只在需�?CPU 端访问时使用
    
    // TODO: 实际实现需要复�?counterBuffer �?visibleIndicesBuffer �?readbackBuffer
    // 然后映射读取
    
    m_visibleClustersCPU.clear();
    
    // 暂时返回空列�?
    return m_visibleClustersCPU;
}

VkBuffer NaniteManager::getClusterDataBuffer() const {
    return m_clusterDataBuffer ? m_clusterDataBuffer->getBuffer() : VK_NULL_HANDLE;
}

VkBuffer NaniteManager::getVisibleIndicesBuffer() const {
    return m_visibleIndicesBuffer ? m_visibleIndicesBuffer->getBuffer() : VK_NULL_HANDLE;
}

uint32_t NaniteManager::getVisibleClusterCount() const {
    if (m_cullingPass) {
        // 注意：这需要上一帧的剔除结果已经完成
        return m_cullingPass->getVisibleCount();
    }
    return m_totalClusterCount; // 没有剔除时返回全�?
}

const std::vector<uint32_t>& NaniteManager::getVisibleClusterIndices() {
    if (m_cullingPass) {
        return m_cullingPass->getVisibleIndices();
    }
    
    // 如果没有剔除，返回全部索�?
    static std::vector<uint32_t> allIndices;
    if (allIndices.size() != m_totalClusterCount) {
        allIndices.resize(m_totalClusterCount);
        for (uint32_t i = 0; i < m_totalClusterCount; ++i) {
            allIndices[i] = i;
        }
    }
    return allIndices;
}

} // namespace Nanite
