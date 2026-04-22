/**
 * ClusterCullingPass.cpp - Nanite Cluster 剔除和LOD 选择实现
 */

#include "ClusterCullingPass.h"
#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include "ComputePipeline.h"
#include <iostream>
#include <array>

namespace Nanite {

ClusterCullingPass::ClusterCullingPass(std::shared_ptr<VulkanDevice> dev)
    : ComputePassBase(std::move(dev), "ClusterCulling")
{
}

ClusterCullingPass::~ClusterCullingPass() {
    cleanup();
}

void ClusterCullingPass::init() {
    if (m_initialized) return;
    
    std::cout << "[ClusterCulling] Initializing..." << std::endl;
    
    // 创建 buffer（需要在 pipeline 之前创建，用了descriptor set：
    createBuffers();
    
    // 创建 Compute Pipeline
    ComputePipeline::Config config;
    config.shaderPath = "shaders/nanite/cluster_culling.comp.spv";
    
    // 6 bindings:
    // 0: Uniform buffer (CullingUniforms)
    // 1: Storage buffer (ClusterData, readonly)
    // 2: Storage buffer (Transforms, readonly)
    // 3: Storage buffer (VisibleIndices, writeonly)
    // 4: Storage buffer (Counter)
    // 5: Storage buffer (SelectionState)
    config.bindings = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
        {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT}
    };
    
    pipeline = std::make_unique<ComputePipeline>(device, config);
    
    // 获取 pipeline 创建的descriptor set layout
    m_descriptorSetLayout = pipeline->getDescriptorSetLayout();
    
    // 创建 descriptor set
    createDescriptorSet();
    
    m_initialized = true;
    std::cout << "[ClusterCulling] Initialization complete" << std::endl;
}

void ClusterCullingPass::cleanup() {
    if (!m_initialized) return;
    
    if (device) {
        vkDeviceWaitIdle(device->getDevice());
    }
    
    // 清理 descriptor (注意: m_descriptorSetLayout 是由 ComputePipeline 创建和管理的)
    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device->getDevice(), m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    m_descriptorSetLayout = VK_NULL_HANDLE; // 不要销毁，用pipeline 管理
    
    // 清理 buffer
    m_uniformBuffer.reset();
    m_dummyStorageBuffer.reset();
    m_visibleIndicesBuffer.reset();
    m_counterBuffer.reset();
    m_selectionStateBuffer.reset();
    
    // 清理双缓冲readback buffers
    for (uint32_t i = 0; i < READBACK_BUFFER_COUNT; ++i) {
        m_readbackBuffers[i].reset();
    }
    
    ComputePassBase::cleanup();
    m_initialized = false;
}

// createDescriptorSetLayout 不再需要，pipeline 会自动创建

void ClusterCullingPass::createBuffers() {
    // Uniform buffer
    m_uniformBuffer = std::make_unique<VulkanBuffer>(
        device,
        sizeof(ClusterCullingUniforms),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    
    // Dummy storage buffer (作为 transform buffer 未设置时的占位符)
    // 必须使用 STORAGE_BUFFER_BIT 以满足描述符类型要求
    m_dummyStorageBuffer = std::make_unique<VulkanBuffer>(
        device,
        sizeof(glm::mat4),  // 至少能存放一个矩阵
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    // Visible indices buffer
    m_visibleIndicesBuffer = std::make_unique<VulkanBuffer>(
        device,
        sizeof(uint32_t) * MAX_CLUSTERS,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    // Counter buffer
    m_counterBuffer = std::make_unique<VulkanBuffer>(
        device,
        sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | 
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    // Selection state buffer
    m_selectionStateBuffer = std::make_unique<VulkanBuffer>(
        device,
        sizeof(uint32_t) * MAX_CLUSTERS,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    // 双缓冲Readback buffers
    // 用于稳定的GPU->CPU 数据传输：帧 N 写入 buffer[N%2]，读取buffer[(N+1)%2]
    for (uint32_t i = 0; i < READBACK_BUFFER_COUNT; ++i) {
        m_readbackBuffers[i] = std::make_unique<VulkanBuffer>(
            device,
            sizeof(uint32_t) * (MAX_CLUSTERS + 1), // +1 for counter
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
    }
    
    std::cout << "[ClusterCulling] Created double-buffered readback (count=" 
              << READBACK_BUFFER_COUNT << ")" << std::endl;
}

void ClusterCullingPass::createDescriptorSet() {
    // Create descriptor pool
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = 5;
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;
    
    if (vkCreateDescriptorPool(device->getDevice(), &poolInfo, nullptr, 
                               &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create cluster culling descriptor pool");
    }
    
    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;
    
    if (vkAllocateDescriptorSets(device->getDevice(), &allocInfo, 
                                 &m_descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate cluster culling descriptor set");
    }
    
    m_descriptorsDirty = true;
}

void ClusterCullingPass::updateDescriptorSet() {
    if (!m_descriptorsDirty) return;
    if (m_clusterBuffer == VK_NULL_HANDLE) return;
    
    std::array<VkWriteDescriptorSet, 6> descriptorWrites{};
    
    // Binding 0: Uniform buffer
    VkDescriptorBufferInfo uniformBufferInfo{};
    uniformBufferInfo.buffer = m_uniformBuffer->getBuffer();
    uniformBufferInfo.offset = 0;
    uniformBufferInfo.range = sizeof(ClusterCullingUniforms);
    
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = m_descriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &uniformBufferInfo;
    
    // Binding 1: Cluster data buffer
    VkDescriptorBufferInfo clusterBufferInfo{};
    clusterBufferInfo.buffer = m_clusterBuffer;
    clusterBufferInfo.offset = 0;
    clusterBufferInfo.range = VK_WHOLE_SIZE;
    
    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = m_descriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pBufferInfo = &clusterBufferInfo;
    
    // Binding 2: Transform buffer (use dummy storage buffer if not set)
    VkDescriptorBufferInfo transformBufferInfo{};
    transformBufferInfo.buffer = m_transformBuffer != VK_NULL_HANDLE ? 
        m_transformBuffer : m_dummyStorageBuffer->getBuffer();
    transformBufferInfo.offset = 0;
    transformBufferInfo.range = VK_WHOLE_SIZE;
    
    descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[2].dstSet = m_descriptorSet;
    descriptorWrites[2].dstBinding = 2;
    descriptorWrites[2].dstArrayElement = 0;
    descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].pBufferInfo = &transformBufferInfo;
    
    // Binding 3: Visible indices buffer
    VkDescriptorBufferInfo visibleBufferInfo{};
    visibleBufferInfo.buffer = m_visibleIndicesBuffer->getBuffer();
    visibleBufferInfo.offset = 0;
    visibleBufferInfo.range = VK_WHOLE_SIZE;
    
    descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[3].dstSet = m_descriptorSet;
    descriptorWrites[3].dstBinding = 3;
    descriptorWrites[3].dstArrayElement = 0;
    descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[3].descriptorCount = 1;
    descriptorWrites[3].pBufferInfo = &visibleBufferInfo;
    
    // Binding 4: Counter buffer
    VkDescriptorBufferInfo counterBufferInfo{};
    counterBufferInfo.buffer = m_counterBuffer->getBuffer();
    counterBufferInfo.offset = 0;
    counterBufferInfo.range = sizeof(uint32_t);
    
    descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[4].dstSet = m_descriptorSet;
    descriptorWrites[4].dstBinding = 4;
    descriptorWrites[4].dstArrayElement = 0;
    descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[4].descriptorCount = 1;
    descriptorWrites[4].pBufferInfo = &counterBufferInfo;
    
    // Binding 5: Selection state buffer
    VkDescriptorBufferInfo stateBufferInfo{};
    stateBufferInfo.buffer = m_selectionStateBuffer->getBuffer();
    stateBufferInfo.offset = 0;
    stateBufferInfo.range = VK_WHOLE_SIZE;
    
    descriptorWrites[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[5].dstSet = m_descriptorSet;
    descriptorWrites[5].dstBinding = 5;
    descriptorWrites[5].dstArrayElement = 0;
    descriptorWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorWrites[5].descriptorCount = 1;
    descriptorWrites[5].pBufferInfo = &stateBufferInfo;
    
    vkUpdateDescriptorSets(device->getDevice(), 
                           static_cast<uint32_t>(descriptorWrites.size()),
                           descriptorWrites.data(), 0, nullptr);
    
    m_descriptorsDirty = false;
}

void ClusterCullingPass::setClusterBuffer(VkBuffer buffer, uint32_t clusterCount) {
    if (m_clusterBuffer != buffer || m_clusterCount != clusterCount) {
        m_clusterBuffer = buffer;
        m_clusterCount = clusterCount;
        m_descriptorsDirty = true;
    }
}

void ClusterCullingPass::setTransformBuffer(VkBuffer buffer) {
    if (m_transformBuffer != buffer) {
        m_transformBuffer = buffer;
        m_descriptorsDirty = true;
    }
}

void ClusterCullingPass::updateUniforms(const ClusterCullingUniforms& uniforms) {
    if (m_uniformBuffer) {
        m_uniformBuffer->copyFrom(&uniforms, sizeof(uniforms));
    }
}

void ClusterCullingPass::resetCounters(VkCommandBuffer commandBuffer) {
    // 重置可见计数器为 0
    vkCmdFillBuffer(commandBuffer, m_counterBuffer->getBuffer(), 0, sizeof(uint32_t), 0);
    
    // 重置选择状态
    vkCmdFillBuffer(commandBuffer, m_selectionStateBuffer->getBuffer(), 0,
                    sizeof(uint32_t) * m_clusterCount, 0);
    
    // 内存屏障
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        1, &barrier,
        0, nullptr,
        0, nullptr
    );
}

void ClusterCullingPass::record(VkCommandBuffer commandBuffer) {
    // 无帧索引版本：使用内部计数器（不推荐，可能有同步问题：
    record(commandBuffer, m_currentReadbackIndex);
}

void ClusterCullingPass::record(VkCommandBuffer commandBuffer, uint32_t frameIndex) {
    if (!isReady()) return;
    
    // 确保 descriptors 更新
    updateDescriptorSet();
    
    // 绑定 pipeline
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, 
                      pipeline->getPipeline());
    
    // 绑定 descriptor set
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipeline->getPipelineLayout(),
                            0, 1, &m_descriptorSet, 0, nullptr);
    
    // Dispatch
    uint32_t groupCountX = (m_clusterCount + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    vkCmdDispatch(commandBuffer, groupCountX, 1, 1);
    
    // 内存屏障：compute -> host transfer (for readback)
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        1, &barrier,
        0, nullptr,
        0, nullptr
    );
    
    // =====================================================
    // 基于帧索引的双缓冲策略：
    // - 帧N 写入 buffer[N % READBACK_BUFFER_COUNT]
    // - 读取时（在vkWaitForFences 之后）读取buffer[currentFrame]
    //   此时 GPU 已完成对试buffer 的写入（因为 fence 保证了上一次使用该帧槽的命令已完成：
    // =====================================================
    
    uint32_t writeIndex = frameIndex % READBACK_BUFFER_COUNT;
    m_currentReadbackIndex = writeIndex;  // 记录当前写入的索引
    VkBuffer writeBuffer = m_readbackBuffers[writeIndex]->getBuffer();
    
    // 复制计数器到当前写入的readback buffer
    VkBufferCopy countCopyRegion{};
    countCopyRegion.size = sizeof(uint32_t);
    vkCmdCopyBuffer(commandBuffer, m_counterBuffer->getBuffer(), 
                    writeBuffer, 1, &countCopyRegion);
    
    // 复制可见索引到当前写入的 readback buffer
    VkBufferCopy indicesCopyRegion{};
    indicesCopyRegion.srcOffset = 0;
    indicesCopyRegion.dstOffset = sizeof(uint32_t);
    indicesCopyRegion.size = sizeof(uint32_t) * m_clusterCount;
    vkCmdCopyBuffer(commandBuffer, m_visibleIndicesBuffer->getBuffer(),
                    writeBuffer, 1, &indicesCopyRegion);
    
    // 屏障：transfer -> host read
    VkMemoryBarrier transferBarrier{};
    transferBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    transferBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    transferBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0,
        1, &transferBarrier,
        0, nullptr,
        0, nullptr
    );
    
    // 标记需要在帧结束后读取数据
    m_dataCopyPending = true;
}

void ClusterCullingPass::extractFrustumPlanes(const glm::mat4& viewProj, glm::vec4 planes[6]) {
    // 从VP 矩阵提取视锥平面（Gribb/Hartmann 方法：
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
    
    // 归一化平面
    for (int i = 0; i < 6; i++) {
        float length = glm::length(glm::vec3(planes[i]));
        if (length > 0.0f) {
            planes[i] /= length;
        }
    }
}

uint32_t ClusterCullingPass::getVisibleCount() {
    // 返回上一帧的缓存结果
    return m_visibleCount;
}

const std::vector<uint32_t>& ClusterCullingPass::getVisibleIndices() {
    // 返回上一帧的缓存结果
    return m_visibleIndicesCPU;
}

void ClusterCullingPass::readbackData(uint32_t frameIndex) {
    // =====================================================
    // 帧同步双缓冲读取策略：
    // 
    // 渲染循环时序：
    // 1. vkWaitForFences(currentFrame) -- 等待使用同一帧槽的命令完成
    // 2. readbackData(currentFrame)    -- 此函数调用点
    // 3. record(..., currentFrame)      -- 写入 buffer[currentFrame % 2]
    // 4. vkQueueSubmit                   -- 提交命令
    //
    // vkWaitForFences(currentFrame) 保证的是：
    //   上一次使用frameIndex == currentFrame 的帧已经完成
    //   半"两帧前 的工作已完成
    //
    // 所以我们应该读取buffer[currentFrame % 2]，这是上一与
    // 使用同一帧槽时写入的数据，此时GPU 一定已完成写入。
    // =====================================================
    
    uint32_t readIndex = frameIndex % READBACK_BUFFER_COUNT;
    
    // 确保 buffer 存在
    if (!m_readbackBuffers[readIndex]) {
        std::cerr << "[ClusterCulling] Readback buffer[" << readIndex << "] is null" << std::endl;
        m_dataCopyPending = false;
        return;
    }
    
    // 读取数据（readback buffer 是HOST_VISIBLE 的）
    void* mappedData = nullptr;
    m_readbackBuffers[readIndex]->map(&mappedData);
    
    if (mappedData == nullptr) {
        std::cerr << "[ClusterCulling] Failed to map readback buffer[" << readIndex << "]" << std::endl;
        m_dataCopyPending = false;
        return;
    }
    
    uint32_t* data = static_cast<uint32_t*>(mappedData);
    
    m_visibleCount = data[0];
    
    // 限制可见数量到有效范围
    if (m_visibleCount > m_clusterCount) {
        m_visibleCount = m_clusterCount;
    }
    
    m_visibleIndicesCPU.resize(m_visibleCount);
    
    for (uint32_t i = 0; i < m_visibleCount; ++i) {
        m_visibleIndicesCPU[i] = data[1 + i];
    }
    
    m_readbackBuffers[readIndex]->unmap();
    m_dataCopyPending = false;
    
    // 输出调试信息（每隔一段时间）
    static uint32_t debugCounter = 0;
    if (++debugCounter % 300 == 0) {
        // 统计每个 LOD 级别的可见cluster 数量
        std::cout << "[ClusterCulling] Visible: " << m_visibleCount 
                  << "/" << m_clusterCount << " (readBuffer=" << readIndex << ") | indices: ";
        
        // 输出前几个可见cluster 的索引（用于调试：
        size_t sampleCount = std::min(m_visibleIndicesCPU.size(), size_t(5));
        for (size_t i = 0; i < sampleCount; ++i) {
            std::cout << m_visibleIndicesCPU[i];
            if (i < sampleCount - 1) std::cout << ",";
        }
        if (m_visibleIndicesCPU.size() > 5) std::cout << "...";
        std::cout << std::endl;
    }
}

VkBuffer ClusterCullingPass::getVisibleIndicesBuffer() const {
    return m_visibleIndicesBuffer ? m_visibleIndicesBuffer->getBuffer() : VK_NULL_HANDLE;
}

VkBuffer ClusterCullingPass::getCounterBuffer() const {
    return m_counterBuffer ? m_counterBuffer->getBuffer() : VK_NULL_HANDLE;
}

} // namespace Nanite
