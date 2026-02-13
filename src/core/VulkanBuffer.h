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

    VkBuffer getBuffer() const { return buffer; }
    VkDeviceMemory getMemory() const { return memory; }
    VkDeviceSize getSize() const { return size; }

private:
    std::shared_ptr<VulkanDevice> device;
    
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void* mapped = nullptr;
};