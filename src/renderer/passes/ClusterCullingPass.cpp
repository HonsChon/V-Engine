/**
 * ClusterCullingPass.cpp - Nanite Cluster 剔除和LOD 选择 (Pure RHI)
 */

#include "ClusterCullingPass.h"
#include "RHIDevice.h"
#include "RHIBuffer.h"
#include "RHIPipeline.h"
#include "RHIDescriptor.h"
#include "RHICommandBuffer.h"
#include <iostream>
#include <cstring>

namespace Nanite {

ClusterCullingPass::ClusterCullingPass(RHIDevice* rhiDevice)
    : ComputePassBase(rhiDevice, "ClusterCulling") {
}

ClusterCullingPass::~ClusterCullingPass() { cleanup(); }

void ClusterCullingPass::init() {
    if (m_initialized) return;
    std::cout << "[ClusterCulling] Initializing (Pure RHI)..." << std::endl;
    createBuffers();
    createComputePipeline();
    createDescriptorSet();
    m_initialized = true;
    std::cout << "[ClusterCulling] Initialization complete" << std::endl;
}

void ClusterCullingPass::cleanup() {
    if (!m_initialized) return;
    if (rhiDevice_) rhiDevice_->waitIdle();
    m_uniformBuffer_.reset(); m_dummyStorageBuffer_.reset();
    m_visibleIndicesBuffer_.reset(); m_counterBuffer_.reset();
    m_selectionStateBuffer_.reset(); m_bindingGroup_.reset();
    for (uint32_t i = 0; i < READBACK_BUFFER_COUNT; ++i) m_readbackBuffers_[i].reset();
    ComputePassBase::cleanup();
    m_initialized = false;
}

// ============================================
// Buffer creation
// ============================================

void ClusterCullingPass::createBuffers() {
    // Uniform buffer
    { RHIBufferDesc d{}; d.size = sizeof(ClusterCullingUniforms);
      d.usage = RHIBufferUsage::Uniform; d.memoryUsage = RHIMemoryUsage::CPUToGPU;
      m_uniformBuffer_ = rhiDevice_->createBuffer(d); }

    // Dummy storage buffer (placeholder when transform buffer not set)
    { RHIBufferDesc d{}; d.size = sizeof(glm::mat4);
      d.usage = RHIBufferUsage::Storage; d.memoryUsage = RHIMemoryUsage::GPUOnly;
      m_dummyStorageBuffer_ = rhiDevice_->createBuffer(d); }

    // Visible indices buffer
    { RHIBufferDesc d{}; d.size = sizeof(uint32_t) * MAX_CLUSTERS;
      d.usage = RHIBufferUsage::Storage | RHIBufferUsage::TransferSrc;
      d.memoryUsage = RHIMemoryUsage::GPUOnly;
      m_visibleIndicesBuffer_ = rhiDevice_->createBuffer(d); }

    // Counter buffer
    { RHIBufferDesc d{}; d.size = sizeof(uint32_t);
      d.usage = RHIBufferUsage::Storage | RHIBufferUsage::TransferDst | RHIBufferUsage::TransferSrc;
      d.memoryUsage = RHIMemoryUsage::GPUOnly;
      m_counterBuffer_ = rhiDevice_->createBuffer(d); }

    // Selection state buffer
    { RHIBufferDesc d{}; d.size = sizeof(uint32_t) * MAX_CLUSTERS;
      d.usage = RHIBufferUsage::Storage | RHIBufferUsage::TransferDst;
      d.memoryUsage = RHIMemoryUsage::GPUOnly;
      m_selectionStateBuffer_ = rhiDevice_->createBuffer(d); }

    // Double-buffered readback
    for (uint32_t i = 0; i < READBACK_BUFFER_COUNT; ++i) {
        RHIBufferDesc d{}; d.size = sizeof(uint32_t) * (MAX_CLUSTERS + 1);
        d.usage = RHIBufferUsage::TransferDst; d.memoryUsage = RHIMemoryUsage::GPUToCPU;
        m_readbackBuffers_[i] = rhiDevice_->createBuffer(d);
    }
    std::cout << "[ClusterCulling] Created double-buffered readback (count=" << READBACK_BUFFER_COUNT << ")" << std::endl;
}

// ============================================
// Compute Pipeline
// ============================================

void ClusterCullingPass::createComputePipeline() {
    RHIBindingLayoutDesc layoutDesc;
    layoutDesc.entries.push_back({0, RHIDescriptorType::UniformBuffer, RHIShaderStage::Compute, 1});
    layoutDesc.entries.push_back({1, RHIDescriptorType::StorageBuffer, RHIShaderStage::Compute, 1});
    layoutDesc.entries.push_back({2, RHIDescriptorType::StorageBuffer, RHIShaderStage::Compute, 1});
    layoutDesc.entries.push_back({3, RHIDescriptorType::StorageBuffer, RHIShaderStage::Compute, 1});
    layoutDesc.entries.push_back({4, RHIDescriptorType::StorageBuffer, RHIShaderStage::Compute, 1});
    layoutDesc.entries.push_back({5, RHIDescriptorType::StorageBuffer, RHIShaderStage::Compute, 1});
    bindingLayout_ = rhiDevice_->createBindingLayout(layoutDesc);

    auto builder = rhiDevice_->createComputePipelineBuilder();
    builder->setComputeShader("shaders/nanite/cluster_culling.comp.spv")
        .addBindingLayout(bindingLayout_.get());
    pipeline_ = builder->build();
}

void ClusterCullingPass::createDescriptorSet() {
    m_bindingGroup_ = rhiDevice_->allocateBindingGroup(bindingLayout_.get());
    m_descriptorsDirty = true;
}

void ClusterCullingPass::updateDescriptorSet() {
    if (!m_descriptorsDirty) return;
    if (!m_clusterBuffer_) return;

    m_bindingGroup_->updateBuffer(0, m_uniformBuffer_.get(), 0, sizeof(ClusterCullingUniforms));
    m_bindingGroup_->updateBuffer(1, m_clusterBuffer_, 0, 0);  // 0 = VK_WHOLE_SIZE equivalent
    RHIBuffer* transformBuf = m_transformBuffer_ ? m_transformBuffer_ : m_dummyStorageBuffer_.get();
    m_bindingGroup_->updateBuffer(2, transformBuf, 0, 0);
    m_bindingGroup_->updateBuffer(3, m_visibleIndicesBuffer_.get(), 0, sizeof(uint32_t) * MAX_CLUSTERS);
    m_bindingGroup_->updateBuffer(4, m_counterBuffer_.get(), 0, sizeof(uint32_t));
    m_bindingGroup_->updateBuffer(5, m_selectionStateBuffer_.get(), 0, sizeof(uint32_t) * MAX_CLUSTERS);

    m_descriptorsDirty = false;
}

// ============================================
// External buffer setters
// ============================================

void ClusterCullingPass::setClusterBuffer(RHIBuffer* buffer, uint32_t clusterCount) {
    if (m_clusterBuffer_ != buffer || m_clusterCount_ != clusterCount) {
        m_clusterBuffer_ = buffer;
        m_clusterCount_ = clusterCount;
        m_descriptorsDirty = true;
    }
}

void ClusterCullingPass::setTransformBuffer(RHIBuffer* buffer) {
    if (m_transformBuffer_ != buffer) {
        m_transformBuffer_ = buffer;
        m_descriptorsDirty = true;
    }
}

void ClusterCullingPass::updateUniforms(const ClusterCullingUniforms& uniforms) {
    if (!m_uniformBuffer_) return;
    void* ptr = m_uniformBuffer_->map();
    memcpy(ptr, &uniforms, sizeof(uniforms));
    m_uniformBuffer_->unmap();
}

// ============================================
// Record (Pure RHI)
// ============================================

void ClusterCullingPass::resetCounters(RHICommandBuffer* cmd) {
    cmd->fillBuffer(m_counterBuffer_.get(), 0, sizeof(uint32_t), 0);
    cmd->fillBuffer(m_selectionStateBuffer_.get(), 0, sizeof(uint32_t) * m_clusterCount_, 0);
    insertBufferBarrier(cmd, m_counterBuffer_.get());
    insertBufferBarrier(cmd, m_selectionStateBuffer_.get());
}

void ClusterCullingPass::record(RHICommandBuffer* cmd) {
    record(cmd, m_currentReadbackIndex_);
}

void ClusterCullingPass::record(RHICommandBuffer* cmd, uint32_t frameIndex) {
    if (!isReady()) return;
    updateDescriptorSet();

    cmd->bindComputePipeline(pipeline_.get());
    cmd->setBindingGroup(0, m_bindingGroup_.get());

    uint32_t groupCountX = (m_clusterCount_ + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    cmd->dispatch(groupCountX, 1, 1);

    // Barriers
    insertBufferBarrier(cmd, m_visibleIndicesBuffer_.get());
    insertBufferBarrier(cmd, m_counterBuffer_.get());

    // Double-buffered readback copy
    uint32_t writeIndex = frameIndex % READBACK_BUFFER_COUNT;
    m_currentReadbackIndex_ = writeIndex;

    cmd->copyBuffer(m_counterBuffer_.get(), m_readbackBuffers_[writeIndex].get(), sizeof(uint32_t), 0, 0);
    cmd->copyBuffer(m_visibleIndicesBuffer_.get(), m_readbackBuffers_[writeIndex].get(),
                    sizeof(uint32_t) * m_clusterCount_, 0, sizeof(uint32_t));

    insertBufferBarrier(cmd, m_readbackBuffers_[writeIndex].get());
    m_dataCopyPending = true;
}

// ============================================
// Frustum plane extraction
// ============================================

void ClusterCullingPass::extractFrustumPlanes(const glm::mat4& viewProj, glm::vec4 planes[6]) {
    planes[0] = glm::vec4(viewProj[0][3]+viewProj[0][0], viewProj[1][3]+viewProj[1][0], viewProj[2][3]+viewProj[2][0], viewProj[3][3]+viewProj[3][0]);
    planes[1] = glm::vec4(viewProj[0][3]-viewProj[0][0], viewProj[1][3]-viewProj[1][0], viewProj[2][3]-viewProj[2][0], viewProj[3][3]-viewProj[3][0]);
    planes[2] = glm::vec4(viewProj[0][3]+viewProj[0][1], viewProj[1][3]+viewProj[1][1], viewProj[2][3]+viewProj[2][1], viewProj[3][3]+viewProj[3][1]);
    planes[3] = glm::vec4(viewProj[0][3]-viewProj[0][1], viewProj[1][3]-viewProj[1][1], viewProj[2][3]-viewProj[2][1], viewProj[3][3]-viewProj[3][1]);
    planes[4] = glm::vec4(viewProj[0][3]+viewProj[0][2], viewProj[1][3]+viewProj[1][2], viewProj[2][3]+viewProj[2][2], viewProj[3][3]+viewProj[3][2]);
    planes[5] = glm::vec4(viewProj[0][3]-viewProj[0][2], viewProj[1][3]-viewProj[1][2], viewProj[2][3]-viewProj[2][2], viewProj[3][3]-viewProj[3][2]);
    for (int i = 0; i < 6; i++) {
        float length = glm::length(glm::vec3(planes[i]));
        if (length > 0.0f) planes[i] /= length;
    }
}

// ============================================
// Readback
// ============================================

uint32_t ClusterCullingPass::getVisibleCount() { return m_visibleCount; }

const std::vector<uint32_t>& ClusterCullingPass::getVisibleIndices() { return m_visibleIndicesCPU; }

void ClusterCullingPass::readbackData(uint32_t frameIndex) {
    uint32_t readIndex = frameIndex % READBACK_BUFFER_COUNT;
    if (!m_readbackBuffers_[readIndex]) {
        std::cerr << "[ClusterCulling] Readback buffer[" << readIndex << "] is null" << std::endl;
        m_dataCopyPending = false; return;
    }

    void* mappedData = m_readbackBuffers_[readIndex]->map();
    if (!mappedData) {
        std::cerr << "[ClusterCulling] Failed to map readback buffer[" << readIndex << "]" << std::endl;
        m_dataCopyPending = false; return;
    }

    uint32_t* data = static_cast<uint32_t*>(mappedData);
    m_visibleCount = data[0];
    if (m_visibleCount > m_clusterCount_) m_visibleCount = m_clusterCount_;
    m_visibleIndicesCPU.resize(m_visibleCount);
    for (uint32_t i = 0; i < m_visibleCount; ++i) m_visibleIndicesCPU[i] = data[1 + i];
    m_readbackBuffers_[readIndex]->unmap();
    m_dataCopyPending = false;

    static uint32_t debugCounter = 0;
    if (++debugCounter % 300 == 0) {
        std::cout << "[ClusterCulling] Visible: " << m_visibleCount << "/" << m_clusterCount_
                  << " (readBuffer=" << readIndex << ")" << std::endl;
    }
}

} // namespace Nanite
