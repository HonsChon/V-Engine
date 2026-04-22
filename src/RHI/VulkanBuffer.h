#pragma once

#include <vulkan/vulkan.h>
#include <memory>

class VulkanDevice;

class VulkanBuffer {
public:
    VulkanBuffer(std::shared_ptr<VulkanDevice> device, 
                VkDeviceSize size, 
                VkBufferUsageFlags usage, 
                VkMemoryPropertyFlags properties);
    ~VulkanBuffer();

    void map(void** data, VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
    void unmap();
    void copyFrom(const void* src, VkDeviceSize size);
    
    /**
     * 上传数据到设备本地缓冲区（使用 staging buffer）
     * 对于 HOST_VISIBLE 缓冲区直接映射内存
     * 对于 DEVICE_LOCAL 缓冲区使用命令缓冲区复制
     */
    void uploadData(const void* data, VkDeviceSize dataSize);

    VkBuffer getBuffer() const { return buffer; }
    VkDeviceMemory getMemory() const { return memory; }
    VkDeviceSize getSize() const { return size; }

private:
    std::shared_ptr<VulkanDevice> device;
    
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    VkMemoryPropertyFlags memoryProperties = 0;  // 保存创建时的内存属性
    void* mapped = nullptr;
    
    bool isHostVisible() const { 
        return (memoryProperties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0; 
    }
};
