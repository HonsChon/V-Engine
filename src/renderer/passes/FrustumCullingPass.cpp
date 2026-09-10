#include "FrustumCullingPass.h"
#include "RHIDevice.h"
#include "RHIBuffer.h"
#include "RHIPipeline.h"
#include "RHIDescriptor.h"
#include "RHICommandBuffer.h"
#include <cstring>
#include <stdexcept>
#include <iostream>

FrustumCullingPass::FrustumCullingPass(RHIDevice* rhiDevice)
    : ComputePassBase(rhiDevice, "FrustumCulling") {
}

FrustumCullingPass::~FrustumCullingPass() { cleanup(); }

void FrustumCullingPass::init() {
    createBuffers(10000);
    createComputePipeline();
    createDescriptorSet();
    std::cout << "[FrustumCullingPass] Initialized (Pure RHI)" << std::endl;
}

// ============================================
// Buffer creation (Pure RHI)
// ============================================

void FrustumCullingPass::createBuffers(uint32_t maxInstCount) {
    maxInstances_ = maxInstCount;

    // Instance buffer (Storage, GPU-only, upload via staging)
    { RHIBufferDesc d{}; d.size = sizeof(GPUInstanceData) * maxInstances_;
      d.usage = RHIBufferUsage::Storage; d.memoryUsage = RHIMemoryUsage::GPUOnly;
      instanceBuffer_ = rhiDevice_->createBuffer(d); }

    // Uniform buffer (CPU→GPU)
    { RHIBufferDesc d{}; d.size = sizeof(CullingUniforms);
      d.usage = RHIBufferUsage::Uniform; d.memoryUsage = RHIMemoryUsage::CPUToGPU;
      uniformBuffer_ = rhiDevice_->createBuffer(d); }

    // Visible indices buffer (Storage + Vertex + TransferSrc)
    { RHIBufferDesc d{}; d.size = sizeof(uint32_t) * maxInstances_;
      d.usage = RHIBufferUsage::Storage | RHIBufferUsage::Vertex | RHIBufferUsage::TransferSrc;
      d.memoryUsage = RHIMemoryUsage::GPUOnly;
      visibleIndicesBuffer_ = rhiDevice_->createBuffer(d); }

    // Indirect draw buffer (Storage + Indirect + TransferDst)
    { RHIBufferDesc d{}; d.size = sizeof(RHIDrawIndexedIndirectCommand) * maxInstances_;
      d.usage = RHIBufferUsage::Storage | RHIBufferUsage::Indirect | RHIBufferUsage::TransferDst;
      d.memoryUsage = RHIMemoryUsage::GPUOnly;
      indirectDrawBuffer_ = rhiDevice_->createBuffer(d); }

    // Counter buffer (Storage + TransferDst + TransferSrc)
    { RHIBufferDesc d{}; d.size = sizeof(uint32_t);
      d.usage = RHIBufferUsage::Storage | RHIBufferUsage::TransferDst | RHIBufferUsage::TransferSrc;
      d.memoryUsage = RHIMemoryUsage::GPUOnly;
      counterBuffer_ = rhiDevice_->createBuffer(d); }

    // Counter readback buffer (CPU-readable)
    { RHIBufferDesc d{}; d.size = sizeof(uint32_t);
      d.usage = RHIBufferUsage::TransferDst; d.memoryUsage = RHIMemoryUsage::GPUToCPU;
      counterReadbackBuffer_ = rhiDevice_->createBuffer(d); }

    // Visible indices readback buffer
    { RHIBufferDesc d{}; d.size = sizeof(uint32_t) * maxInstances_;
      d.usage = RHIBufferUsage::TransferDst; d.memoryUsage = RHIMemoryUsage::GPUToCPU;
      visibleIndicesReadbackBuffer_ = rhiDevice_->createBuffer(d); }
}

// ============================================
// Compute Pipeline (Pure RHI)
// ============================================

void FrustumCullingPass::createComputePipeline() {
    // Binding layout: 5 buffers
    RHIBindingLayoutDesc layoutDesc;
    layoutDesc.entries.push_back({0, RHIDescriptorType::UniformBuffer,  RHIShaderStage::Compute, 1});
    layoutDesc.entries.push_back({1, RHIDescriptorType::StorageBuffer,  RHIShaderStage::Compute, 1});
    layoutDesc.entries.push_back({2, RHIDescriptorType::StorageBuffer,  RHIShaderStage::Compute, 1});
    layoutDesc.entries.push_back({3, RHIDescriptorType::StorageBuffer,  RHIShaderStage::Compute, 1});
    layoutDesc.entries.push_back({4, RHIDescriptorType::StorageBuffer,  RHIShaderStage::Compute, 1});
    bindingLayout_ = rhiDevice_->createBindingLayout(layoutDesc);

    auto builder = rhiDevice_->createComputePipelineBuilder();
    builder->setComputeShader("shaders/culling/frustum_culling.comp.spv")
        .addBindingLayout(bindingLayout_.get());
    pipeline_ = builder->build();
}

void FrustumCullingPass::createDescriptorSet() {
    bindingGroup_ = rhiDevice_->allocateBindingGroup(bindingLayout_.get());
    updateDescriptorSet();
}

void FrustumCullingPass::updateDescriptorSet() {
    if (!bindingGroup_) return;
    bindingGroup_->updateBuffer(0, uniformBuffer_.get(),        0, sizeof(CullingUniforms));
    bindingGroup_->updateBuffer(1, instanceBuffer_.get(),       0, sizeof(GPUInstanceData) * maxInstances_);
    bindingGroup_->updateBuffer(2, visibleIndicesBuffer_.get(), 0, sizeof(uint32_t) * maxInstances_);
    bindingGroup_->updateBuffer(3, counterBuffer_.get(),        0, sizeof(uint32_t));
    bindingGroup_->updateBuffer(4, indirectDrawBuffer_.get(),   0, sizeof(RHIDrawIndexedIndirectCommand) * maxInstances_);
}

// ============================================
// Record (Pure RHI)
// ============================================

void FrustumCullingPass::record(RHICommandBuffer* cmd) {
    if (!pipeline_ || currentInstanceCount_ == 0) return;

    cmd->bindComputePipeline(pipeline_.get());
    cmd->setBindingGroup(0, bindingGroup_.get());

    uint32_t workGroupCount = (currentInstanceCount_ + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    cmd->dispatch(workGroupCount, 1, 1);

    // Barriers: ensure writes are visible to draw indirect and vertex input
    insertBufferBarrier(cmd, indirectDrawBuffer_.get());
    insertBufferBarrier(cmd, visibleIndicesBuffer_.get());
}

void FrustumCullingPass::resetCounters(RHICommandBuffer* cmd) {
    cmd->fillBuffer(counterBuffer_.get(), 0, sizeof(uint32_t), 0);
    insertBufferBarrier(cmd, counterBuffer_.get());
}

// ============================================
// Data upload
// ============================================

void FrustumCullingPass::updateInstances(const std::vector<GPUInstanceData>& instances) {
    currentInstanceCount_ = static_cast<uint32_t>(instances.size());
    if (currentInstanceCount_ > maxInstances_) {
        cleanup();
        createBuffers(currentInstanceCount_ * 2);
        createComputePipeline();
        createDescriptorSet();
    }
    instanceBuffer_->uploadData(instances.data(), sizeof(GPUInstanceData) * currentInstanceCount_);
}

void FrustumCullingPass::updateUniforms(const CullingUniforms& uniforms) {
    CullingUniforms updated = uniforms;
    extractFrustumPlanes(uniforms.viewProjMatrix, updated.frustumPlanes);
    updated.instanceCountPacked = glm::uvec4(currentInstanceCount_, 0, 0, 0);
    void* ptr = uniformBuffer_->map();
    memcpy(ptr, &updated, sizeof(CullingUniforms));
    uniformBuffer_->unmap();
}

// ============================================
// Frustum plane extraction (pure math, no Vulkan)
// ============================================

void FrustumCullingPass::extractFrustumPlanes(const glm::mat4& viewProj, glm::vec4 planes[6]) {
    planes[0] = glm::vec4(viewProj[0][3]+viewProj[0][0], viewProj[1][3]+viewProj[1][0], viewProj[2][3]+viewProj[2][0], viewProj[3][3]+viewProj[3][0]);
    planes[1] = glm::vec4(viewProj[0][3]-viewProj[0][0], viewProj[1][3]-viewProj[1][0], viewProj[2][3]-viewProj[2][0], viewProj[3][3]-viewProj[3][0]);
    planes[2] = glm::vec4(viewProj[0][3]+viewProj[0][1], viewProj[1][3]+viewProj[1][1], viewProj[2][3]+viewProj[2][1], viewProj[3][3]+viewProj[3][1]);
    planes[3] = glm::vec4(viewProj[0][3]-viewProj[0][1], viewProj[1][3]-viewProj[1][1], viewProj[2][3]-viewProj[2][1], viewProj[3][3]-viewProj[3][1]);
    planes[4] = glm::vec4(viewProj[0][2], viewProj[1][2], viewProj[2][2], viewProj[3][2]);
    planes[5] = glm::vec4(viewProj[0][3]-viewProj[0][2], viewProj[1][3]-viewProj[1][2], viewProj[2][3]-viewProj[2][2], viewProj[3][3]-viewProj[3][2]);
    for (int i = 0; i < 6; ++i) {
        float len = glm::length(glm::vec3(planes[i]));
        planes[i] /= len;
    }
}

// ============================================
// Readback (uses RHI map/unmap + device single-time commands for copy)
// ============================================

uint32_t FrustumCullingPass::getVisibleCount() {
    readbackCounter();
    return visibleCount_;
}

void FrustumCullingPass::readbackCounter() {
    if (!counterBuffer_ || !counterReadbackBuffer_) return;
    // Use RHI single-time commands for the copy
    void* cmdRaw = rhiDevice_->beginSingleTimeCommands();
    auto rhiCmd = rhiDevice_->wrapCommandBuffer(cmdRaw);
    rhiCmd->copyBuffer(counterBuffer_.get(), counterReadbackBuffer_.get(), sizeof(uint32_t));
    rhiCmd.reset(); // release wrap before ending
    rhiDevice_->endSingleTimeCommands(cmdRaw);

    void* data = counterReadbackBuffer_->map();
    visibleCount_ = *reinterpret_cast<uint32_t*>(data);
    counterReadbackBuffer_->unmap();
}

const std::vector<uint32_t>& FrustumCullingPass::getVisibleIndices() {
    if (!visibleIndicesBuffer_ || !visibleIndicesReadbackBuffer_) {
        visibleIndicesCPU_.clear();
        return visibleIndicesCPU_;
    }
    readbackCounter();
    if (visibleCount_ == 0) { visibleIndicesCPU_.clear(); return visibleIndicesCPU_; }
    uint32_t readCount = std::min(visibleCount_, maxInstances_);

    void* cmdRaw = rhiDevice_->beginSingleTimeCommands();
    auto rhiCmd = rhiDevice_->wrapCommandBuffer(cmdRaw);
    rhiCmd->copyBuffer(visibleIndicesBuffer_.get(), visibleIndicesReadbackBuffer_.get(), sizeof(uint32_t) * readCount);
    rhiCmd.reset();
    rhiDevice_->endSingleTimeCommands(cmdRaw);

    void* data = visibleIndicesReadbackBuffer_->map();
    visibleIndicesCPU_.resize(readCount);
    memcpy(visibleIndicesCPU_.data(), data, sizeof(uint32_t) * readCount);
    visibleIndicesReadbackBuffer_->unmap();

    return visibleIndicesCPU_;
}

void FrustumCullingPass::cleanup() {
    instanceBuffer_.reset(); uniformBuffer_.reset();
    visibleIndicesBuffer_.reset(); visibleIndicesReadbackBuffer_.reset();
    indirectDrawBuffer_.reset(); counterBuffer_.reset();
    counterReadbackBuffer_.reset(); bindingGroup_.reset();
    ComputePassBase::cleanup();
}