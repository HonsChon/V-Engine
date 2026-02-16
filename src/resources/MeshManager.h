#pragma once

#include "Mesh.h"
#include "VulkanBuffer.h"
#include "VulkanDevice.h"
#include "../scene/RayPicker.h"  // for AABB
#include <memory>
#include <string>
#include <unordered_map>
#include <iostream>

namespace VulkanEngine {

/**
 * @brief GPU Mesh 数据结构
 * 包含 Mesh 几何数据以及 Vulkan 顶点/索引缓冲区
 */
struct GPUMesh {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<VulkanBuffer> vertexBuffer;
    std::shared_ptr<VulkanBuffer> indexBuffer;
    
    bool isValid() const {
        return mesh && vertexBuffer && indexBuffer;
    }
    
    uint32_t getIndexCount() const {
        return mesh ? static_cast<uint32_t>(mesh->getIndices().size()) : 0;
    }
    
    uint32_t getVertexCount() const {
        return mesh ? static_cast<uint32_t>(mesh->getVertices().size()) : 0;
    }
    
    VkBuffer getVertexBufferHandle() const {
        return vertexBuffer ? vertexBuffer->getBuffer() : VK_NULL_HANDLE;
    }
    
    VkBuffer getIndexBufferHandle() const {
        return indexBuffer ? indexBuffer->getBuffer() : VK_NULL_HANDLE;
    }
    
    /**
     * @brief 计算网格的局部空间 AABB
     */
    AABB calculateAABB() const {
        AABB aabb;
        if (!mesh) return aabb;
        
        const auto& vertices = mesh->getVertices();
        for (const auto& vertex : vertices) {
            aabb.expand(vertex.pos);
        }
        return aabb;
    }
};

/**
 * @brief 网格资源管理器
 * 负责加载、缓存和管理所有网格资源
 * 单例模式，全局访问
 */
class MeshManager {
public:
    static MeshManager& getInstance() {
        static MeshManager instance;
        return instance;
    }
    
    // 禁止拷贝和移动
    MeshManager(const MeshManager&) = delete;
    MeshManager& operator=(const MeshManager&) = delete;
    
    /**
     * @brief 初始化 MeshManager
     * @param device Vulkan 设备指针
     */
    void init(std::shared_ptr<VulkanDevice> device) {
        m_device = device;
        std::cout << "[MeshManager] Initialized" << std::endl;
    }
    
    /**
     * @brief 加载或获取网格
     * 如果网格已缓存，直接返回；否则加载并缓存
     * @param meshId 网格标识符（路径或预设名称如 "sphere", "cube", "plane"）
     * @return 指向 GPUMesh 的共享指针，失败返回 nullptr
     */
    std::shared_ptr<GPUMesh> getMesh(const std::string& meshId) {
        // 检查缓存
        auto it = m_meshCache.find(meshId);
        if (it != m_meshCache.end()) {
            return it->second;
        }
        
        // 加载网格
        auto gpuMesh = loadMesh(meshId);
        if (gpuMesh) {
            m_meshCache[meshId] = gpuMesh;
        }
        return gpuMesh;
    }
    
    /**
     * @brief 预加载网格（不返回，仅缓存）
     */
    void preloadMesh(const std::string& meshId) {
        getMesh(meshId);
    }
    
    /**
     * @brief 检查网格是否已缓存
     */
    bool hasMesh(const std::string& meshId) const {
        return m_meshCache.find(meshId) != m_meshCache.end();
    }
    
    /**
     * @brief 卸载指定网格
     */
    void unloadMesh(const std::string& meshId) {
        auto it = m_meshCache.find(meshId);
        if (it != m_meshCache.end()) {
            std::cout << "[MeshManager] Unloading mesh: " << meshId << std::endl;
            m_meshCache.erase(it);
        }
    }
    
    /**
     * @brief 卸载所有网格资源
     */
    void cleanup() {
        std::cout << "[MeshManager] Cleaning up " << m_meshCache.size() << " meshes..." << std::endl;
        m_meshCache.clear();
        m_device.reset();
    }
    
    /**
     * @brief 获取已加载的网格数量
     */
    size_t getMeshCount() const {
        return m_meshCache.size();
    }
    
    /**
     * @brief 获取指定网格的 AABB 包围盒
     * @param meshId 网格标识符
     * @return 网格的局部空间 AABB，如果网格不存在则返回默认 AABB
     */
    AABB getMeshAABB(const std::string& meshId) {
        auto gpuMesh = getMesh(meshId);
        if (gpuMesh) {
            return gpuMesh->calculateAABB();
        }
        // 返回默认的单位立方体 AABB
        AABB defaultAABB;
        defaultAABB.min = glm::vec3(-1.0f);
        defaultAABB.max = glm::vec3(1.0f);
        return defaultAABB;
    }

private:
    MeshManager() = default;
    ~MeshManager() { cleanup(); }
    
    /**
     * @brief 实际加载网格的内部方法
     */
    std::shared_ptr<GPUMesh> loadMesh(const std::string& meshId) {
        if (!m_device) {
            std::cerr << "[MeshManager] Error: Device not initialized!" << std::endl;
            return nullptr;
        }
        
        auto gpuMesh = std::make_shared<GPUMesh>();
        gpuMesh->mesh = std::make_shared<Mesh>();
        
        bool loadSuccess = false;
        
        // 处理预设网格
        if (meshId == "sphere") {
            gpuMesh->mesh->createSphere(64);
            loadSuccess = true;
        }
        else if (meshId == "cube") {
            gpuMesh->mesh->createCube();
            loadSuccess = true;
        }
        else if (meshId == "plane") {
            gpuMesh->mesh->createPlane(10.0f, 10);
            loadSuccess = true;
        }
        // 处理 OBJ 文件路径
        else if (meshId.find(".obj") != std::string::npos || 
                 meshId.find(".OBJ") != std::string::npos) {
            if (gpuMesh->mesh->loadFromOBJ(meshId)) {
                gpuMesh->mesh->centerAndNormalize();
                loadSuccess = true;
            } else {
                std::cerr << "[MeshManager] Failed to load OBJ: " << meshId << std::endl;
            }
        }
        else {
            std::cerr << "[MeshManager] Unknown mesh type: " << meshId << std::endl;
        }
        
        if (!loadSuccess) {
            return nullptr;
        }
        
        // 创建 GPU 缓冲区
        if (!createGPUBuffers(gpuMesh)) {
            return nullptr;
        }
        
        std::cout << "[MeshManager] Loaded mesh: " << meshId 
                  << " (vertices: " << gpuMesh->mesh->getVertices().size()
                  << ", indices: " << gpuMesh->mesh->getIndices().size() << ")" << std::endl;
        
        return gpuMesh;
    }
    
    /**
     * @brief 为网格创建 GPU 缓冲区
     */
    bool createGPUBuffers(std::shared_ptr<GPUMesh> gpuMesh) {
        if (!gpuMesh || !gpuMesh->mesh) return false;
        
        const auto& vertices = gpuMesh->mesh->getVertices();
        const auto& indices = gpuMesh->mesh->getIndices();
        
        if (vertices.empty() || indices.empty()) {
            std::cerr << "[MeshManager] Error: Empty mesh data!" << std::endl;
            return false;
        }
        
        // 创建顶点缓冲区
        VkDeviceSize vertexBufferSize = sizeof(vertices[0]) * vertices.size();
        gpuMesh->vertexBuffer = std::make_shared<VulkanBuffer>(
            m_device,
            vertexBufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        gpuMesh->vertexBuffer->copyFrom(vertices.data(), vertexBufferSize);
        
        // 创建索引缓冲区
        VkDeviceSize indexBufferSize = sizeof(indices[0]) * indices.size();
        gpuMesh->indexBuffer = std::make_shared<VulkanBuffer>(
            m_device,
            indexBufferSize,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        gpuMesh->indexBuffer->copyFrom(indices.data(), indexBufferSize);
        
        return true;
    }
    
    std::shared_ptr<VulkanDevice> m_device;
    std::unordered_map<std::string, std::shared_ptr<GPUMesh>> m_meshCache;
};

} // namespace VulkanEngine
