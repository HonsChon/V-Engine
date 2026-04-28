#pragma once

#include "RHITexture.h"
#include <vulkan/vulkan.h>

class VulkanRHIDevice;

class VulkanRHITexture : public RHITexture {
public:
    VulkanRHITexture(VulkanRHIDevice* device, const RHITextureDesc& desc);
    ~VulkanRHITexture() override;

    uint32_t        getWidth() const override { return desc_.width; }
    uint32_t        getHeight() const override { return desc_.height; }
    uint32_t        getDepth() const override { return desc_.depth; }
    uint32_t        getMipLevels() const override { return desc_.mipLevels; }
    uint32_t        getArrayLayers() const override { return desc_.arrayLayers; }
    RHIFormat       getFormat() const override { return desc_.format; }
    RHITextureUsage getUsage() const override { return desc_.usage; }

    // Native handles
    VkImage        getVkImage() const { return image_; }
    VkImageView    getVkImageView() const { return imageView_; }
    VkDeviceMemory getVkMemory() const { return memory_; }

private:
    void createImage();
    void createImageView();

    VulkanRHIDevice* device_;
    RHITextureDesc   desc_;

    VkImage        image_     = VK_NULL_HANDLE;
    VkDeviceMemory memory_    = VK_NULL_HANDLE;
    VkImageView    imageView_ = VK_NULL_HANDLE;
};
