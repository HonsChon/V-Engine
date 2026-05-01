#pragma once

#include "RHITexture.h"
#include "IVulkanNative.h"

class VulkanRHIDevice;

class VulkanRHITexture : public RHITexture, public IVulkanNativeTexture {
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

    void uploadPixels(const void* data, uint64_t dataSize) override;
    std::unique_ptr<RHITexture> createLayerView(uint32_t layer) override;

    // IVulkanNativeTexture implementation
    VkImage        getVkImage() const override { return image_; }
    VkImageView    getVkImageView() const override { return imageView_; }
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

/// Non-owning single-layer view of a VulkanRHITexture array.
/// Does NOT destroy the underlying image; only destroys its own VkImageView.
class VulkanRHITextureLayerView : public RHITexture, public IVulkanNativeTexture {
public:
    VulkanRHITextureLayerView(VulkanRHIDevice* device, VulkanRHITexture* parent,
                              uint32_t layer);
    ~VulkanRHITextureLayerView() override;

    uint32_t        getWidth() const override { return parent_->getWidth(); }
    uint32_t        getHeight() const override { return parent_->getHeight(); }
    uint32_t        getDepth() const override { return 1; }
    uint32_t        getMipLevels() const override { return 1; }
    uint32_t        getArrayLayers() const override { return 1; }
    RHIFormat       getFormat() const override { return parent_->getFormat(); }
    RHITextureUsage getUsage() const override { return parent_->getUsage(); }

    // IVulkanNativeTexture implementation
    VkImage     getVkImage() const override { return parent_->getVkImage(); }
    VkImageView getVkImageView() const override { return layerView_; }

private:
    VulkanRHIDevice*  device_;
    VulkanRHITexture* parent_;
    uint32_t          layer_;
    VkImageView       layerView_ = VK_NULL_HANDLE;
};
