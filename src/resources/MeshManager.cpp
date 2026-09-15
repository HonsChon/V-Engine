/**
 * @file MeshManager.cpp
 * @brief MeshManager::loadMesh / registerMesh 实现
 *
 * loadMesh 从 MeshManager.h 移出：glTF 分支依赖 ModelImporter，
 * 避免所有包含 MeshManager.h 的编译单元引入 tiny_gltf.h。
 */

#include "MeshManager.h"
#include "ModelImporter.h"
#include "RHIDevice.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fs = std::filesystem;

namespace VEngine {

std::shared_ptr<GPUMesh> MeshManager::registerMesh(const std::string& meshId, std::shared_ptr<Mesh> mesh) {
    if (!m_rhiDevice || !mesh) return nullptr;

    auto it = m_meshCache.find(meshId);
    if (it != m_meshCache.end()) {
        return it->second;   // 已注册
    }

    auto gpuMesh = std::make_shared<GPUMesh>();
    gpuMesh->mesh = mesh;
    if (!createGPUBuffers(gpuMesh)) {
        return nullptr;
    }
    m_meshCache[meshId] = gpuMesh;

    std::cout << "[MeshManager] Registered mesh: " << meshId
              << " (vertices: " << mesh->getVertices().size()
              << ", indices: " << mesh->getIndices().size() << ")" << std::endl;
    return gpuMesh;
}

std::shared_ptr<GPUMesh> MeshManager::loadMesh(const std::string& meshId) {
    if (!m_rhiDevice) {
        std::cerr << "[MeshManager] Error: RHI Device not initialized!" << std::endl;
        return nullptr;
    }

    // glTF meshId 形如 "<路径>#<meshIdx>_<primIdx>"：
    // 一次解析整个文件并注册全部 primitive，返回请求的那一个
    auto hash = meshId.rfind('#');
    if (hash != std::string::npos) {
        std::string base = meshId.substr(0, hash);
        std::string suffix = meshId.substr(hash);

        std::string err;
        auto meshes = ModelImporter::extractGLTFMeshes(base, err);
        if (meshes.empty()) {
            std::cerr << "[MeshManager] Failed to load glTF: " << base
                      << " (" << err << ")" << std::endl;
            return nullptr;
        }

        std::shared_ptr<GPUMesh> found;
        for (const auto& [suf, mesh] : meshes) {
            auto gm = registerMesh(base + suf, mesh);
            if (suf == suffix) found = gm;
        }
        return found;
    }

    // 预设 / OBJ：按扩展名分发
    std::string ext;
    fs::path p(meshId);
    if (p.has_extension()) {
        ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }

    auto gpuMesh = std::make_shared<GPUMesh>();
    gpuMesh->mesh = std::make_shared<Mesh>();

    bool loadSuccess = false;

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
    else if (ext == ".obj") {
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

} // namespace VEngine
