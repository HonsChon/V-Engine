#include "VulkanRHITexture.h"
#include "VulkanRHIDevice.h"
#include "VulkanTypeConversions.h"
#include <stdexcept>

using namespace VulkanTypeConversions;

VulkanRHITexture::VulkanRHITexture(VulkanRHIDevice* device, const RHITextureDesc& desc)
    : device_(device), desc_(desc)
{
    createImage();
    createImageView();
}

VulkanRHITexture::~VulkanRHITexture() {
    VkDevice vkDev = device_->getVkDevice();
    if (imageView_ != VK_NULL_HANDLE) vkDestroyImageView(vkDev, imageView_, nullptr);
    if (image_     != VK_NULL_HANDLE) vkDestroyImage(vkDev, image_, nullptr);
    if (memory_    != VK_NULL_HANDLE) vkFreeMemory(vkDev, memory_, nullptr);
}

void VulkanRHITexture::createImage() {
    VkDevice vkDev = device_->getVkDevice();

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = desc_.width;
    imageInfo.extent.height = desc_.height;
    imageInfo.extent.depth = desc_.depth;
    imageInfo.mipLevels = desc_.mipLevels;
    imageInfo.arrayLayers = desc_.arrayLayers;
    imageInfo.format = toVkFormat(desc_.format);
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = toVkImageUsage(desc_.usage);
    imageInfo.samples = toVkSampleCount(desc_.samples);
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(vkDev, &imageInfo, nullptr, &image_) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanRHITexture] Failed to create image!");
    }

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(vkDev, image_, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = device_->findMemoryType(
        memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(vkDev, &allocInfo, nullptr, &memory_) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanRHITexture] Failed to allocate image memory!");
    }

    vkBindImageMemory(vkDev, image_, memory_, 0);
}

void VulkanRHITexture::createImageView() {
    VkDevice vkDev = device_->getVkDevice();

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image_;
    viewInfo.viewType = (desc_.arrayLayers > 1) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = toVkFormat(desc_.format);
    viewInfo.subresourceRange.aspectMask = getAspectFlags(desc_.format);
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = desc_.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = desc_.arrayLayers;

    if (vkCreateImageView(vkDev, &viewInfo, nullptr, &imageView_) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanRHITexture] Failed to create image view!");
    }
}
