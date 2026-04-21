#include "GPUDrivenRenderer.h"
#include "VulkanDevice.h"
#include <iostream>

GPUDrivenRenderer::GPUDrivenRenderer(std::shared_ptr<VulkanDevice> device, const Config& config)
    : device(device), config(config) {
}

GPUDrivenRenderer::~GPUDrivenRenderer() {
    frustumCullingPass.reset();
}

void GPUDrivenRenderer::init() {
    std::cout << "[GPUDrivenRenderer] Initializing GPU-Driven Rendering..." << std::endl;
    
    if (config.enableFrustumCulling) {
        frustumCullingPass = std::make_unique<FrustumCullingPass>(device);
        frustumCullingPass->init();
        std::cout << "[GPUDrivenRenderer] Frustum Culling Pass initialized" << std::endl;
    }
    
    // 后续初始化其�?Pass
    // if (config.enableOcclusionCulling) { ... }
    // if (config.enableLODSelection) { ... }
    
    std::cout << "[GPUDrivenRenderer] GPU-Driven Rendering ready!" << std::endl;
}

void GPUDrivenRenderer::prepare(const std::vector<GPUInstanceData>& instances,
                                 const glm::mat4& viewMatrix,
                                 const glm::mat4& projMatrix,
                                 const glm::vec3& cameraPos) {
    stats.totalInstances = static_cast<uint32_t>(instances.size());
    
    if (!config.enableFrustumCulling || !frustumCullingPass) {
        return;
    }
    
    // 更新实例数据
    frustumCullingPass->updateInstances(instances);
    
    // 更新 Uniform 数据
    CullingUniforms uniforms{};
    uniforms.viewMatrix = viewMatrix;
    uniforms.projMatrix = projMatrix;
    uniforms.viewProjMatrix = projMatrix * viewMatrix;
    uniforms.cameraPosition = glm::vec4(cameraPos, 1.0f);
    uniforms.instanceCountPacked.x = static_cast<uint32_t>(instances.size());
    
    frustumCullingPass->updateUniforms(uniforms);
}

void GPUDrivenRenderer::executeCulling(VkCommandBuffer commandBuffer) {
    if (!config.enableFrustumCulling || !frustumCullingPass) {
        return;
    }
    
    // 重置计数�?
    frustumCullingPass->resetCounters(commandBuffer);
    
    // 执行剔除
    frustumCullingPass->record(commandBuffer);
    
    // 更新统计
    stats.visibleInstances = frustumCullingPass->getVisibleCount();
    stats.culledInstances = stats.totalInstances - stats.visibleInstances;
}

VkBuffer GPUDrivenRenderer::getIndirectDrawBuffer() const {
    if (frustumCullingPass) {
        return frustumCullingPass->getIndirectDrawBuffer();
    }
    return VK_NULL_HANDLE;
}

VkBuffer GPUDrivenRenderer::getVisibleIndicesBuffer() const {
    if (frustumCullingPass) {
        return frustumCullingPass->getVisibleIndicesBuffer();
    }
    return VK_NULL_HANDLE;
}

uint32_t GPUDrivenRenderer::getVisibleCount() const {
    if (frustumCullingPass) {
        return frustumCullingPass->getVisibleCount();
    }
    return 0;
}

const std::vector<uint32_t>& GPUDrivenRenderer::getVisibleIndices() {
    static std::vector<uint32_t> emptyVector;
    if (frustumCullingPass) {
        return frustumCullingPass->getVisibleIndices();
    }
    return emptyVector;
}
