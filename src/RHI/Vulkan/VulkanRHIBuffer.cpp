#include "VulkanRHIBuffer.h"
#include "VulkanRHIDevice.h"
#include "VulkanTypeConversions.h"

#include <stdexcept>
#include <cstring>

using namespace VulkanTypeConversions;

VulkanRHIBuffer::VulkanRHIBuffer(VulkanRHIDevice* device, const RHIBufferDesc& desc)
    : device_(device), desc_(desc)
{
    VkDevice vkDev = device_->getVkDevice();
    memProps_ = toVkMemoryProperties(desc_.memoryUsage);

    // Create buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = desc_.size;
    bufferInfo.usage = toVkBufferUsage(desc_.usage);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(vkDev, &bufferInfo, nullptr, &buffer_) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanRHIBuffer] Failed to create buffer!");
    }

    // Allocate memory
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(vkDev, buffer_, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = device_->findMemoryType(memReq.memoryTypeBits, memProps_);

    if (vkAllocateMemory(vkDev, &allocInfo, nullptr, &memory_) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanRHIBuffer] Failed to allocate buffer memory!");
    }

    vkBindBufferMemory(vkDev, buffer_, memory_, 0);
}

VulkanRHIBuffer::~VulkanRHIBuffer() {
    VkDevice vkDev = device_->getVkDevice();
    if (mapped_) unmap();
    if (buffer_ != VK_NULL_HANDLE) vkDestroyBuffer(vkDev, buffer_, nullptr);
    if (memory_ != VK_NULL_HANDLE) vkFreeMemory(vkDev, memory_, nullptr);
}

void* VulkanRHIBuffer::map() {
    if (mapped_) return mapped_;
    if (vkMapMemory(device_->getVkDevice(), memory_, 0, desc_.size, 0, &mapped_) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanRHIBuffer] Failed to map buffer memory!");
    }
    return mapped_;
}

void VulkanRHIBuffer::unmap() {
    if (mapped_) {
        vkUnmapMemory(device_->getVkDevice(), memory_);
        mapped_ = nullptr;
    }
}

void VulkanRHIBuffer::uploadData(const void* data, uint64_t size, uint64_t offset) {
    VkDevice vkDev = device_->getVkDevice();

    if (isHostVisible()) {
        void* ptr;
        vkMapMemory(vkDev, memory_, offset, size, 0, &ptr);
        memcpy(ptr, data, size);
        vkUnmapMemory(vkDev, memory_);
    } else {
        // Staging buffer path
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingMemory;

        VkBufferCreateInfo bufInfo{};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size = size;
        bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(vkDev, &bufInfo, nullptr, &stagingBuffer);

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(vkDev, stagingBuffer, &memReq);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = device_->findMemoryType(
            memReq.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(vkDev, &allocInfo, nullptr, &stagingMemory);
        vkBindBufferMemory(vkDev, stagingBuffer, stagingMemory, 0);

        void* ptr;
        vkMapMemory(vkDev, stagingMemory, 0, size, 0, &ptr);
        memcpy(ptr, data, size);
        vkUnmapMemory(vkDev, stagingMemory);

        VkCommandBuffer cmd = device_->beginSingleTimeCommands();
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = offset;
        copyRegion.size = size;
        vkCmdCopyBuffer(cmd, stagingBuffer, buffer_, 1, &copyRegion);
        device_->endSingleTimeCommands(cmd);

        vkDestroyBuffer(vkDev, stagingBuffer, nullptr);
        vkFreeMemory(vkDev, stagingMemory, nullptr);
    }
}
