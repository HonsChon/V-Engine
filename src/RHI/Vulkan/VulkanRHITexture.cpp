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

void VulkanRHITexture::uploadPixels(const void* data, uint64_t dataSize) {
    VkDevice vkDev = device_->getVkDevice();

    // 1. Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = dataSize;
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

    // 2. Copy pixel data to staging
    void* ptr;
    vkMapMemory(vkDev, stagingMemory, 0, dataSize, 0, &ptr);
    memcpy(ptr, data, dataSize);
    vkUnmapMemory(vkDev, stagingMemory);

    // 3. Transition image: UNDEFINED → TRANSFER_DST
    VkCommandBuffer cmd = device_->beginSingleTimeCommandsVk();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = desc_.mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = desc_.arrayLayers;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    // 4. Copy buffer to image
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = desc_.arrayLayers;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { desc_.width, desc_.height, desc_.depth };

    vkCmdCopyBufferToImage(cmd, stagingBuffer, image_,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    // 5. Transition image: TRANSFER_DST → SHADER_READ_ONLY
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    device_->endSingleTimeCommandsVk(cmd);

    // 6. Cleanup staging
    vkDestroyBuffer(vkDev, stagingBuffer, nullptr);
    vkFreeMemory(vkDev, stagingMemory, nullptr);
}

std::unique_ptr<RHITexture> VulkanRHITexture::createLayerView(uint32_t layer) {
    if (layer >= desc_.arrayLayers) return nullptr;
    return std::make_unique<VulkanRHITextureLayerView>(device_, this, layer);
}

// =============================================================================
// VulkanRHITextureLayerView
// =============================================================================

VulkanRHITextureLayerView::VulkanRHITextureLayerView(VulkanRHIDevice* device,
                                                     VulkanRHITexture* parent,
                                                     uint32_t layer)
    : device_(device), parent_(parent), layer_(layer)
{
    VkDevice vkDev = device_->getVkDevice();

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = parent_->getVkImage();
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = toVkFormat(parent_->getFormat());
    viewInfo.subresourceRange.aspectMask = getAspectFlags(parent_->getFormat());
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = layer;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(vkDev, &viewInfo, nullptr, &layerView_) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanRHITextureLayerView] Failed to create layer image view!");
    }
}

VulkanRHITextureLayerView::~VulkanRHITextureLayerView() {
    if (layerView_ != VK_NULL_HANDLE) {
        VkDevice vkDev = device_->getVkDevice();
        vkDestroyImageView(vkDev, layerView_, nullptr);
    }
}
