#pragma once

#include "Mesh.h"
#include "RHIBuffer.h"
#include "RayPicker.h"  // for AABB
#include <memory>
#include <string>
#include <unordered_map>
#include <iostream>

// Forward declaration
class RHIDevice;

namespace VulkanEngine {

/**
 * @brief GPU Mesh 数据结构
 * 持有 Mesh 几何数据及 RHI 顶点/索引缓冲区
 * 所有 buffer 通过 RHIDevice 创建 — 无 Vulkan 依赖
 */
struct GPUMesh {
    std::shared_ptr<Mesh> mesh;
    std::unique_ptr<RHIBuffer> vertexBuffer;
    std::unique_ptr<RHIBuffer> indexBuffer;
    
    bool isValid() const {
        return mesh && vertexBuffer && indexBuffer;
    }
    
    uint32_t getIndexCount() const {
        return mesh ? static_cast<uint32_t>(mesh->getIndices().size()) : 0;
    }
    
    uint32_t getVertexCount() const {
        return mesh ? static_cast<uint32_t>(mesh->getVertices().size()) : 0;
    }
    
    /// Direct RHI buffer access — no wrapping needed
    RHIBuffer* getVertexBuffer() const { return vertexBuffer.get(); }
    RHIBuffer* getIndexBuffer() const { return indexBuffer.get(); }
    
    /**
     * @brief 计算网格的局部空间AABB
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
 * 
 * 通过 RHIDevice 创建 GPU buffer — 不依赖具体后端
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
     * @param rhiDevice RHI 设备指针（用于创建 buffer）
     */
    void init(RHIDevice* rhiDevice) {
        m_rhiDevice = rhiDevice;
        std::cout << "[MeshManager] Initialized (RHI)" << std::endl;
    }
    
    /**
     * @brief 加载或获取网格
     * 如果网格已缓存，直接返回；否则加载并缓存
     * @param meshId 网格标识符（路径或预设名称如 "sphere", "cube", "plane"：
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
    
    void preloadMesh(const std::string& meshId) {
        getMesh(meshId);
    }
    
    bool hasMesh(const std::string& meshId) const {
        return m_meshCache.find(meshId) != m_meshCache.end();
    }
    
    void unloadMesh(const std::string& meshId) {
        auto it = m_meshCache.find(meshId);
        if (it != m_meshCache.end()) {
            std::cout << "[MeshManager] Unloading mesh: " << meshId << std::endl;
            m_meshCache.erase(it);
        }
    }
    
    void cleanup() {
        std::cout << "[MeshManager] Cleaning up " << m_meshCache.size() << " meshes..." << std::endl;
        m_meshCache.clear();
        m_rhiDevice = nullptr;
    }
    
    size_t getMeshCount() const {
        return m_meshCache.size();
    }
    
    AABB getMeshAABB(const std::string& meshId) {
        auto gpuMesh = getMesh(meshId);
        if (gpuMesh) {
            return gpuMesh->calculateAABB();
        }
        AABB defaultAABB;
        defaultAABB.min = glm::vec3(-1.0f);
        defaultAABB.max = glm::vec3(1.0f);
        return defaultAABB;
    }

private:
    MeshManager() = default;
    ~MeshManager() { cleanup(); }
    
    std::shared_ptr<GPUMesh> loadMesh(const std::string& meshId) {
        if (!m_rhiDevice) {
            std::cerr << "[MeshManager] Error: RHI Device not initialized!" << std::endl;
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
        
        // 通过 RHI 创建 GPU 缓冲区
        if (!createGPUBuffers(gpuMesh)) {
            return nullptr;
        }
        
        std::cout << "[MeshManager] Loaded mesh: " << meshId 
                  << " (vertices: " << gpuMesh->mesh->getVertices().size()
                  << ", indices: " << gpuMesh->mesh->getIndices().size() << ")" << std::endl;
        
        return gpuMesh;
    }
    
    bool createGPUBuffers(std::shared_ptr<GPUMesh> gpuMesh);
    
    RHIDevice* m_rhiDevice = nullptr;
    std::unordered_map<std::string, std::shared_ptr<GPUMesh>> m_meshCache;
};

} // namespace VulkanEngine