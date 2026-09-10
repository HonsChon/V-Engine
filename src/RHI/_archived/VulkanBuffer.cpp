#include "VulkanBuffer.h"
#include "VulkanDevice.h"
#include <stdexcept>
#include <cstring>

VulkanBuffer::VulkanBuffer(std::shared_ptr<VulkanDevice> device, VkDeviceSize size, 
                          VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) 
    : device(device), size(size), memoryProperties(properties) {
    
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

void VulkanBuffer::uploadData(const void* data, VkDeviceSize dataSize) {
    // 根据内存属性决定上传策略
    if (isHostVisible()) {
        // HOST_VISIBLE: 直接映射并复制
        if (vkMapMemory(device->getDevice(), memory, 0, dataSize, 0, &mapped) != VK_SUCCESS) {
            throw std::runtime_error("Failed to map HOST_VISIBLE buffer memory!");
        }
        memcpy(mapped, data, dataSize);
        vkUnmapMemory(device->getDevice(), memory);
        mapped = nullptr;
    } else {
        // DEVICE_LOCAL: 使用 staging buffer (不尝试直接映射，避免 validation 警告)
        // 创建临时 staging buffer
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingMemory;
        
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = dataSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateBuffer(device->getDevice(), &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create staging buffer!");
        }
        
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device->getDevice(), stagingBuffer, &memRequirements);
        
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = device->findMemoryType(
            memRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        
        if (vkAllocateMemory(device->getDevice(), &allocInfo, nullptr, &stagingMemory) != VK_SUCCESS) {
            vkDestroyBuffer(device->getDevice(), stagingBuffer, nullptr);
            throw std::runtime_error("Failed to allocate staging buffer memory!");
        }
        
        vkBindBufferMemory(device->getDevice(), stagingBuffer, stagingMemory, 0);
        
        // 复制数据到 staging buffer
        void* mappedData;
        vkMapMemory(device->getDevice(), stagingMemory, 0, dataSize, 0, &mappedData);
        memcpy(mappedData, data, dataSize);
        vkUnmapMemory(device->getDevice(), stagingMemory);
        
        // 使用命令缓冲区复制到目标缓冲区
        VkCommandBuffer commandBuffer = device->beginSingleTimeCommands();
        
        VkBufferCopy copyRegion{};
        copyRegion.size = dataSize;
        vkCmdCopyBuffer(commandBuffer, stagingBuffer, buffer, 1, &copyRegion);
        
        device->endSingleTimeCommands(commandBuffer);
        
        // 清理 staging buffer
        vkDestroyBuffer(device->getDevice(), stagingBuffer, nullptr);
        vkFreeMemory(device->getDevice(), stagingMemory, nullptr);
    }
}
