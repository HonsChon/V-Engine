#pragma once

#include "Mesh.h"
#include "AssetPath.h"
#include "RHIBuffer.h"
#include "RayPicker.h"  // for AABB
#include <memory>
#include <string>
#include <unordered_map>
#include <iostream>

// Forward declaration
class RHIDevice;

namespace VEngine {

class ModelImporter;  // loadMesh 的 glTF 分支依赖（实现在 MeshManager.cpp）

/**
 * @brief GPU Mesh 数据结构
 * 持有 Mesh 几何数据及 RHI 顶点/索引缓冲区
 * 所有 buffer 通过 RHIDevice 创建 — 无 Vulkan 依赖
 */
struct GPUMesh {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<RHIBuffer> vertexBuffer;
    std::shared_ptr<RHIBuffer> indexBuffer;
    
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
     * @param meshId 网格标识符：
     *   - 预设名 "sphere"/"cube"/"plane"
     *   - OBJ 文件路径
     *   - glTF meshId："<路径>#<meshIdx>_<primIdx>"（惰性解析整个文件并注册全部 primitive）
     * @return 指向 GPUMesh 的共享指针，失败返回 nullptr
     */
    std::shared_ptr<GPUMesh> getMesh(const std::string& meshId) {
        // 路径归一化：序列化场景重开时工作目录可能变化（见 AssetPath.h）
        std::string key = resolveAssetPath(meshId);

        // 检查缓存
        auto it = m_meshCache.find(key);
        if (it != m_meshCache.end()) {
            return it->second;
        }

        // 加载网格
        auto gpuMesh = loadMesh(key);
        if (gpuMesh) {
            m_meshCache[key] = gpuMesh;
        }
        return gpuMesh;
    }

    /**
     * @brief 注册外部构建的网格（ModelImporter 解析 glTF 后逐 primitive 注册）
     * @param meshId 缓存键（glTF: "<路径>#<meshIdx>_<primIdx>"）
     * @param mesh 已构建好的 CPU 几何
     * @return GPUMesh（创建 GPU 缓冲区并缓存），失败返回 nullptr；已注册时直接返回缓存
     */
    std::shared_ptr<GPUMesh> registerMesh(const std::string& meshId, std::shared_ptr<Mesh> mesh);
    
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

    // 实现位于 MeshManager.cpp（glTF 分支依赖 ModelImporter）
    std::shared_ptr<GPUMesh> loadMesh(const std::string& meshId);
    
    bool createGPUBuffers(std::shared_ptr<GPUMesh> gpuMesh);
    
    RHIDevice* m_rhiDevice = nullptr;
    std::unordered_map<std::string, std::shared_ptr<GPUMesh>> m_meshCache;
};

} // namespace VEngine