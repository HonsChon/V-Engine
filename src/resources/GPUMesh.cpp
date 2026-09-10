/**
 * @file GPUMesh.cpp
 * @brief Implementation of MeshManager::createGPUBuffers (Pure RHI)
 */

#include "MeshManager.h"
#include "RHIDevice.h"

namespace VulkanEngine {

bool MeshManager::createGPUBuffers(std::shared_ptr<GPUMesh> gpuMesh) {
    if (!gpuMesh || !gpuMesh->mesh || !m_rhiDevice) return false;

    const auto& vertices = gpuMesh->mesh->getVertices();
    const auto& indices = gpuMesh->mesh->getIndices();

    if (vertices.empty() || indices.empty()) {
        std::cerr << "[MeshManager] Error: Empty mesh data!" << std::endl;
        return false;
    }

    // Create vertex buffer via RHI
    uint64_t vertexBufferSize = sizeof(vertices[0]) * vertices.size();
    RHIBufferDesc vtxDesc{};
    vtxDesc.size = vertexBufferSize;
    vtxDesc.usage = RHIBufferUsage::Vertex;
    vtxDesc.memoryUsage = RHIMemoryUsage::CPUToGPU;  // HOST_VISIBLE for simple upload
    gpuMesh->vertexBuffer = m_rhiDevice->createBuffer(vtxDesc);
    gpuMesh->vertexBuffer->uploadData(vertices.data(), vertexBufferSize);

    // Create index buffer via RHI
    uint64_t indexBufferSize = sizeof(indices[0]) * indices.size();
    RHIBufferDesc idxDesc{};
    idxDesc.size = indexBufferSize;
    idxDesc.usage = RHIBufferUsage::Index;
    idxDesc.memoryUsage = RHIMemoryUsage::CPUToGPU;
    gpuMesh->indexBuffer = m_rhiDevice->createBuffer(idxDesc);
    gpuMesh->indexBuffer->uploadData(indices.data(), indexBufferSize);

    return true;
}

} // namespace VulkanEngine
