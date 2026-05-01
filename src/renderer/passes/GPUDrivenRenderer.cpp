#include "GPUDrivenRenderer.h"
#include "RHIDevice.h"
#include "RHICommandBuffer.h"
#include "RHIBuffer.h"
#include <iostream>

GPUDrivenRenderer::GPUDrivenRenderer(RHIDevice* rhiDevice, const Config& config)
    : rhiDevice_(rhiDevice), config(config) {
}

GPUDrivenRenderer::~GPUDrivenRenderer() {
    frustumCullingPass.reset();
}

void GPUDrivenRenderer::init() {
    std::cout << "[GPUDrivenRenderer] Initializing (Pure RHI)..." << std::endl;
    if (config.enableFrustumCulling) {
        frustumCullingPass = std::make_unique<FrustumCullingPass>(rhiDevice_);
        frustumCullingPass->init();
        std::cout << "[GPUDrivenRenderer] Frustum Culling Pass initialized" << std::endl;
    }
    std::cout << "[GPUDrivenRenderer] GPU-Driven Rendering ready!" << std::endl;
}

void GPUDrivenRenderer::prepare(const std::vector<GPUInstanceData>& instances,
                                 const glm::mat4& viewMatrix,
                                 const glm::mat4& projMatrix,
                                 const glm::vec3& cameraPos) {
    stats.totalInstances = static_cast<uint32_t>(instances.size());
    if (!config.enableFrustumCulling || !frustumCullingPass) return;

    frustumCullingPass->updateInstances(instances);

    CullingUniforms uniforms{};
    uniforms.viewMatrix = viewMatrix;
    uniforms.projMatrix = projMatrix;
    uniforms.viewProjMatrix = projMatrix * viewMatrix;
    uniforms.cameraPosition = glm::vec4(cameraPos, 1.0f);
    uniforms.instanceCountPacked.x = static_cast<uint32_t>(instances.size());
    frustumCullingPass->updateUniforms(uniforms);
}

void GPUDrivenRenderer::executeCulling(RHICommandBuffer* cmd) {
    if (!config.enableFrustumCulling || !frustumCullingPass) return;
    frustumCullingPass->resetCounters(cmd);
    frustumCullingPass->record(cmd);
    stats.visibleInstances = frustumCullingPass->getVisibleCount();
    stats.culledInstances = stats.totalInstances - stats.visibleInstances;
}

RHIBuffer* GPUDrivenRenderer::getIndirectDrawBuffer() const {
    return frustumCullingPass ? frustumCullingPass->getIndirectDrawBuffer() : nullptr;
}

RHIBuffer* GPUDrivenRenderer::getVisibleIndicesBuffer() const {
    return frustumCullingPass ? frustumCullingPass->getVisibleIndicesBuffer() : nullptr;
}

uint32_t GPUDrivenRenderer::getVisibleCount() const {
    return frustumCullingPass ? frustumCullingPass->getVisibleCount() : 0;
}

const std::vector<uint32_t>& GPUDrivenRenderer::getVisibleIndices() {
    static std::vector<uint32_t> emptyVector;
    return frustumCullingPass ? frustumCullingPass->getVisibleIndices() : emptyVector;
}