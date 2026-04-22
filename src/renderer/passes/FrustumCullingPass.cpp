#include "FrustumCullingPass.h"
#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include "ComputePipeline.h"
#include <cstring>
#include <stdexcept>
#include <array>

FrustumCullingPass::FrustumCullingPass(std::shared_ptr<VulkanDevice> device)
    : ComputePassBase(device, "FrustumCulling") {
}

FrustumCullingPass::~FrustumCullingPass() {
    cleanup();
}

void FrustumCullingPass::init() {
    // 默认支持 10000 个实例
    createBuffers(10000);
    
    // 创建 Compute Pipeline
    ComputePipeline::Config config;
    config.shaderPath = "shaders/culling/frustum_culling.comp.spv";
    
    // 绑定布局
    // binding 0: Uniform Buffer (相机数据)
    // binding 1: Instance Buffer (只读)
    // binding 2: Visible Indices Buffer (读写)
    // binding 3: Counter Buffer (原子计数器
    // binding 4: Indirect Draw Buffer (输出)
    config.bindings = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
        {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT}
    };
    
    pipeline = std::make_unique<ComputePipeline>(device, config);
    
    createDescriptorSet();
}

void FrustumCullingPass::createBuffers(uint32_t maxInstCount) {
    maxInstances = maxInstCount;
    
    // Instance Buffer: 存储所有实例的变换和包围盒
    VkDeviceSize instanceBufferSize = sizeof(GPUInstanceData) * maxInstances;
    instanceBuffer = std::make_unique<VulkanBuffer>(
        device,
        instanceBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    // Uniform Buffer: 相机矩阵和视锥体
    uniformBuffer = std::make_unique<VulkanBuffer>(
        device,
        sizeof(CullingUniforms),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    
    // Visible Indices Buffer: 存储通过剔除测试的实例索引
    VkDeviceSize visibleBufferSize = sizeof(uint32_t) * maxInstances;
    visibleIndicesBuffer = std::make_unique<VulkanBuffer>(
        device,
        visibleBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    // Indirect Draw Buffer: 存储间接绘制命令
    // 每个网格一与DrawIndexedIndirectCommand
    VkDeviceSize indirectBufferSize = sizeof(VkDrawIndexedIndirectCommand) * maxInstances;
    indirectDrawBuffer = std::make_unique<VulkanBuffer>(
        device,
        indirectBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    // Counter Buffer: 原子计数器
    counterBuffer = std::make_unique<VulkanBuffer>(
        device,
        sizeof(uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    // Counter Readback Buffer: CPU 可读
    counterReadbackBuffer = std::make_unique<VulkanBuffer>(
        device,
        sizeof(uint32_t),
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    
    // Visible Indices Readback Buffer: CPU 可读的可见索引
    visibleIndicesReadbackBuffer = std::make_unique<VulkanBuffer>(
        device,
        sizeof(uint32_t) * maxInstances,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
}

void FrustumCullingPass::createDescriptorSet() {
    if (!pipeline) return;
    
    // 分配 Descriptor Set
    descriptorSet = pipeline->allocateDescriptorSet();
    
    updateDescriptorSet();
}

void FrustumCullingPass::updateDescriptorSet() {
    if (descriptorSet == VK_NULL_HANDLE) return;
    
    std::array<VkWriteDescriptorSet, 5> writes{};
    
    VkDescriptorBufferInfo uniformInfo{};
    uniformInfo.buffer = uniformBuffer->getBuffer();
    uniformInfo.offset = 0;
    uniformInfo.range = sizeof(CullingUniforms);
    
    VkDescriptorBufferInfo instanceInfo{};
    instanceInfo.buffer = instanceBuffer->getBuffer();
    instanceInfo.offset = 0;
    instanceInfo.range = sizeof(GPUInstanceData) * maxInstances;
    
    VkDescriptorBufferInfo visibleInfo{};
    visibleInfo.buffer = visibleIndicesBuffer->getBuffer();
    visibleInfo.offset = 0;
    visibleInfo.range = sizeof(uint32_t) * maxInstances;
    
    VkDescriptorBufferInfo counterInfo{};
    counterInfo.buffer = counterBuffer->getBuffer();
    counterInfo.offset = 0;
    counterInfo.range = sizeof(uint32_t);
    
    VkDescriptorBufferInfo indirectInfo{};
    indirectInfo.buffer = indirectDrawBuffer->getBuffer();
    indirectInfo.offset = 0;
    indirectInfo.range = sizeof(VkDrawIndexedIndirectCommand) * maxInstances;
    
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &uniformInfo;
    
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo = &instanceInfo;
    
    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    writes[2].pBufferInfo = &visibleInfo;
    
    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = descriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorCount = 1;
    writes[3].pBufferInfo = &counterInfo;
    
    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = descriptorSet;
    writes[4].dstBinding = 4;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].descriptorCount = 1;
    writes[4].pBufferInfo = &indirectInfo;
    
    vkUpdateDescriptorSets(device->getDevice(), 
                           static_cast<uint32_t>(writes.size()), 
                           writes.data(), 0, nullptr);
}

void FrustumCullingPass::record(VkCommandBuffer commandBuffer) {
    if (!pipeline || currentInstanceCount == 0) return;
    
    // 绑定 Pipeline
    pipeline->bind(commandBuffer);
    
    // 绑定 Descriptor Set
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipeline->getPipelineLayout(),
                            0, 1, &descriptorSet, 0, nullptr);
    
    // 计算工作组数量
    uint32_t workGroupCount = (currentInstanceCount + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    
    // 派发 Compute Shader
    pipeline->dispatch(commandBuffer, workGroupCount, 1, 1);
    
    // 添加内存屏障，确保后续的 Draw 命令能正确读取
    insertBufferBarrier(commandBuffer,
                        indirectDrawBuffer->getBuffer(),
                        VK_WHOLE_SIZE,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                        VK_ACCESS_SHADER_WRITE_BIT,
                        VK_ACCESS_INDIRECT_COMMAND_READ_BIT);
    
    insertBufferBarrier(commandBuffer,
                        visibleIndicesBuffer->getBuffer(),
                        VK_WHOLE_SIZE,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                        VK_ACCESS_SHADER_WRITE_BIT,
                        VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);
}

void FrustumCullingPass::resetCounters(VkCommandBuffer commandBuffer) {
    // 将计数器重置与0
    vkCmdFillBuffer(commandBuffer, counterBuffer->getBuffer(), 0, sizeof(uint32_t), 0);
    
    // 确保重置完成后才开始剔除
    insertBufferBarrier(commandBuffer,
                        counterBuffer->getBuffer(),
                        sizeof(uint32_t),
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
}

void FrustumCullingPass::updateInstances(const std::vector<GPUInstanceData>& instances) {
    currentInstanceCount = static_cast<uint32_t>(instances.size());
    
    if (currentInstanceCount > maxInstances) {
        // 需要重新分配更大的缓冲区
        cleanup();
        createBuffers(currentInstanceCount * 2); // 预留一些空间
        createDescriptorSet();
    }
    
    // 上传实例数据（这里简化处理，实际应使用staging buffer：
    // TODO: 使用 staging buffer 进行异步上传
    instanceBuffer->uploadData(instances.data(), sizeof(GPUInstanceData) * currentInstanceCount);
}

void FrustumCullingPass::updateUniforms(const CullingUniforms& uniforms) {
    // 计算视锥体平面
    CullingUniforms updatedUniforms = uniforms;
    extractFrustumPlanes(uniforms.viewProjMatrix, updatedUniforms.frustumPlanes);
    // 使用 instanceCountPacked.x 存储实例数量
    updatedUniforms.instanceCountPacked = glm::uvec4(currentInstanceCount, 0, 0, 0);
    
    // 直接映射内存上传（因为是 HOST_VISIBLE：
    void* data;
    vkMapMemory(device->getDevice(), uniformBuffer->getMemory(), 0, sizeof(CullingUniforms), 0, &data);
    memcpy(data, &updatedUniforms, sizeof(CullingUniforms));
    vkUnmapMemory(device->getDevice(), uniformBuffer->getMemory());
}

void FrustumCullingPass::extractFrustumPlanes(const glm::mat4& viewProj, glm::vec4 planes[6]) {
    // 从ViewProjection 矩阵提取视锥体的 6 个平面
    // 平面方程: ax + by + cz + d = 0
    
    // Left plane
    planes[0] = glm::vec4(
        viewProj[0][3] + viewProj[0][0],
        viewProj[1][3] + viewProj[1][0],
        viewProj[2][3] + viewProj[2][0],
        viewProj[3][3] + viewProj[3][0]
    );
    
    // Right plane
    planes[1] = glm::vec4(
        viewProj[0][3] - viewProj[0][0],
        viewProj[1][3] - viewProj[1][0],
        viewProj[2][3] - viewProj[2][0],
        viewProj[3][3] - viewProj[3][0]
    );
    
    // Bottom plane
    planes[2] = glm::vec4(
        viewProj[0][3] + viewProj[0][1],
        viewProj[1][3] + viewProj[1][1],
        viewProj[2][3] + viewProj[2][1],
        viewProj[3][3] + viewProj[3][1]
    );
    
    // Top plane
    planes[3] = glm::vec4(
        viewProj[0][3] - viewProj[0][1],
        viewProj[1][3] - viewProj[1][1],
        viewProj[2][3] - viewProj[2][1],
        viewProj[3][3] - viewProj[3][1]
    );
    
    // Near plane
    planes[4] = glm::vec4(
        viewProj[0][2],
        viewProj[1][2],
        viewProj[2][2],
        viewProj[3][2]
    );
    
    // Far plane
    planes[5] = glm::vec4(
        viewProj[0][3] - viewProj[0][2],
        viewProj[1][3] - viewProj[1][2],
        viewProj[2][3] - viewProj[2][2],
        viewProj[3][3] - viewProj[3][2]
    );
    
    // 归一化平面
    for (int i = 0; i < 6; ++i) {
        float length = glm::length(glm::vec3(planes[i]));
        planes[i] /= length;
    }
}

VkBuffer FrustumCullingPass::getIndirectDrawBuffer() const {
    return indirectDrawBuffer ? indirectDrawBuffer->getBuffer() : VK_NULL_HANDLE;
}

VkBuffer FrustumCullingPass::getVisibleIndicesBuffer() const {
    return visibleIndicesBuffer ? visibleIndicesBuffer->getBuffer() : VK_NULL_HANDLE;
}

void FrustumCullingPass::cleanup() {
    instanceBuffer.reset();
    uniformBuffer.reset();
    visibleIndicesBuffer.reset();
    visibleIndicesReadbackBuffer.reset();  // 添加: 清理 readback buffer
    indirectDrawBuffer.reset();
    counterBuffer.reset();
    counterReadbackBuffer.reset();
    
    ComputePassBase::cleanup();
}

uint32_t FrustumCullingPass::getVisibleCount() {
    readbackCounter();
    return visibleCount;
}

void FrustumCullingPass::readbackCounter() {
    if (!counterBuffer || !counterReadbackBuffer) return;
    
    // 使用一次性命令缓冲区复制计数器
    VkCommandBuffer cmdBuffer = device->beginSingleTimeCommands();
    
    VkBufferCopy copyRegion{};
    copyRegion.size = sizeof(uint32_t);
    vkCmdCopyBuffer(cmdBuffer, counterBuffer->getBuffer(), 
                    counterReadbackBuffer->getBuffer(), 1, &copyRegion);
    
    device->endSingleTimeCommands(cmdBuffer);
    
    // 从readback buffer 读取值
    void* data;
    vkMapMemory(device->getDevice(), counterReadbackBuffer->getMemory(), 
                0, sizeof(uint32_t), 0, &data);
    visibleCount = *reinterpret_cast<uint32_t*>(data);
    vkUnmapMemory(device->getDevice(), counterReadbackBuffer->getMemory());
}

const std::vector<uint32_t>& FrustumCullingPass::getVisibleIndices() {
    if (!visibleIndicesBuffer || !visibleIndicesReadbackBuffer) {
        visibleIndicesCPU.clear();
        return visibleIndicesCPU;
    }
    
    // 先读取可见数量
    readbackCounter();
    
    if (visibleCount == 0) {
        visibleIndicesCPU.clear();
        return visibleIndicesCPU;
    }
    
    // 限制读取数量，防止越界
    uint32_t readCount = std::min(visibleCount, maxInstances);
    
    // 使用一次性命令缓冲区复制可见索引
    VkCommandBuffer cmdBuffer = device->beginSingleTimeCommands();
    
    VkBufferCopy copyRegion{};
    copyRegion.size = sizeof(uint32_t) * readCount;
    vkCmdCopyBuffer(cmdBuffer, visibleIndicesBuffer->getBuffer(), 
                    visibleIndicesReadbackBuffer->getBuffer(), 1, &copyRegion);
    
    device->endSingleTimeCommands(cmdBuffer);
    
    // 从readback buffer 读取索引
    void* data;
    vkMapMemory(device->getDevice(), visibleIndicesReadbackBuffer->getMemory(), 
                0, sizeof(uint32_t) * readCount, 0, &data);
    
    visibleIndicesCPU.resize(readCount);
    memcpy(visibleIndicesCPU.data(), data, sizeof(uint32_t) * readCount);
    
    vkUnmapMemory(device->getDevice(), visibleIndicesReadbackBuffer->getMemory());
    
    return visibleIndicesCPU;
}
