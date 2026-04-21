#include "ComputePassBase.h"
#include "VulkanDevice.h"
#include "ComputePipeline.h"
#include <iostream>

ComputePassBase::ComputePassBase(std::shared_ptr<VulkanDevice> device, const std::string& name)
    : device(device), name(name) {
}

ComputePassBase::~ComputePassBase() {
    cleanup();
}

void ComputePassBase::cleanup() {
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device->getDevice(), descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    
    pipeline.reset();
}

void ComputePassBase::createDescriptorPool(const std::vector<VkDescriptorPoolSize>& poolSizes, 
                                           uint32_t maxSets) {
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxSets;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    if (vkCreateDescriptorPool(device->getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool for " + name);
    }
}

void ComputePassBase::insertMemoryBarrier(VkCommandBuffer commandBuffer,
                                          VkPipelineStageFlags srcStage,
                                          VkPipelineStageFlags dstStage,
                                          VkAccessFlags srcAccess,
                                          VkAccessFlags dstAccess) {
    VkMemoryBarrier memoryBarrier{};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = srcAccess;
    memoryBarrier.dstAccessMask = dstAccess;

    vkCmdPipelineBarrier(
        commandBuffer,
        srcStage,
        dstStage,
        0,
        1, &memoryBarrier,
        0, nullptr,
        0, nullptr
    );
}

void ComputePassBase::insertBufferBarrier(VkCommandBuffer commandBuffer,
                                          VkBuffer buffer,
                                          VkDeviceSize size,
                                          VkPipelineStageFlags srcStage,
                                          VkPipelineStageFlags dstStage,
                                          VkAccessFlags srcAccess,
                                          VkAccessFlags dstAccess) {
    VkBufferMemoryBarrier bufferBarrier{};
    bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bufferBarrier.srcAccessMask = srcAccess;
    bufferBarrier.dstAccessMask = dstAccess;
    bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bufferBarrier.buffer = buffer;
    bufferBarrier.offset = 0;
    bufferBarrier.size = size;

    vkCmdPipelineBarrier(
        commandBuffer,
        srcStage,
        dstStage,
        0,
        0, nullptr,
        1, &bufferBarrier,
        0, nullptr
    );
}
