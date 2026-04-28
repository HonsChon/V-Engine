#include "ComputePassBase.h"
#include "VulkanDevice.h"
#include "RHIDevice.h"
#include "RHIPipeline.h"
#include "RHIDescriptor.h"
#include <iostream>

ComputePassBase::ComputePassBase(std::shared_ptr<VulkanDevice> device, RHIDevice* rhiDevice, const std::string& name)
    : device(device), rhiDevice_(rhiDevice), name(name) {
}

ComputePassBase::~ComputePassBase() {
    cleanup();
}

void ComputePassBase::cleanup() {
    if (rhiDevice_) rhiDevice_->waitIdle();

    descriptorSet_ = VK_NULL_HANDLE;
    pipeline_.reset();
    bindingLayout_.reset();
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

    vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0,
                         1, &memoryBarrier, 0, nullptr, 0, nullptr);
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

    vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0,
                         0, nullptr, 1, &bufferBarrier, 0, nullptr);
}