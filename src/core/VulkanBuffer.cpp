#include "VulkanBuffer.h"
#include "VulkanDevice.h"
#include <stdexcept>
#include <cstring>

VulkanBuffer::VulkanBuffer(std::shared_ptr<VulkanDevice> device, VkDeviceSize size, 
                          VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) 
    : device(device), size(size) {
    
    // 创建缓冲区
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device->getDevice(), &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer!");
    }

    // 获取内存需求
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device->getDevice(), buffer, &memRequirements);

    // 分配内存
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = device->findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device->getDevice(), &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate buffer memory!");
    }

    // 绑定缓冲区和内存
    vkBindBufferMemory(device->getDevice(), buffer, memory, 0);
}

VulkanBuffer::~VulkanBuffer() {
    if (mapped) {
        unmap();
    }
    
    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device->getDevice(), buffer, nullptr);
    }
    
    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(device->getDevice(), memory, nullptr);
    }
}

void VulkanBuffer::map(void** data, VkDeviceSize mapSize, VkDeviceSize offset) {
    if (mapSize == VK_WHOLE_SIZE) {
        mapSize = size;
    }
    
    if (vkMapMemory(device->getDevice(), memory, offset, mapSize, 0, &mapped) != VK_SUCCESS) {
        throw std::runtime_error("failed to map buffer memory!");
    }
    
    *data = mapped;
}

void VulkanBuffer::unmap() {
    if (mapped) {
        vkUnmapMemory(device->getDevice(), memory);
        mapped = nullptr;
    }
}

void VulkanBuffer::copyFrom(const void* src, VkDeviceSize copySize) {
    void* data;
    map(&data, copySize);
    memcpy(data, src, copySize);
    unmap();
}
