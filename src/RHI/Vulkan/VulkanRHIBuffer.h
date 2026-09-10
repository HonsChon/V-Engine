#pragma once

#include "RHIBuffer.h"
#include "IVulkanNative.h"

class VulkanRHIDevice;

class VulkanRHIBuffer : public RHIBuffer, public IVulkanNativeBuffer {
public:
    VulkanRHIBuffer(VulkanRHIDevice* device, const RHIBufferDesc& desc);
    ~VulkanRHIBuffer() override;

    void* map() override;
    void  unmap() override;
    void  uploadData(const void* data, uint64_t size, uint64_t offset = 0) override;

    uint64_t       getSize() const override { return desc_.size; }
    RHIBufferUsage getUsage() const override { return desc_.usage; }
    RHIMemoryUsage getMemoryUsage() const override { return desc_.memoryUsage; }

    // IVulkanNativeBuffer implementation
    VkBuffer       getVkBuffer() const override { return buffer_; }
    VkDeviceMemory getVkMemory() const { return memory_; }

private:
    VulkanRHIDevice* device_;
    RHIBufferDesc    desc_;

    VkBuffer       buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    VkMemoryPropertyFlags memProps_ = 0;
    void* mapped_ = nullptr;

    bool isHostVisible() const {
        return (memProps_ & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
    }
};
