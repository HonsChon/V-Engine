#include "VulkanRHISwapChain.h"
#include "VulkanRHIDevice.h"
#include <stdexcept>
#include <algorithm>
#include <array>
#include <limits>

using namespace VulkanTypeConversions;

// =============================================================================
// Constructor / Destructor
// =============================================================================

VulkanRHISwapChain::VulkanRHISwapChain(VulkanRHIDevice* device,
                                       uint32_t width, uint32_t height)
    : device_(device), requestedWidth_(width), requestedHeight_(height)
{
    createSwapChain();
    createImageViews();
    createRenderPass();
    createDepthResources();
    createFramebuffers();
}

VulkanRHISwapChain::~VulkanRHISwapChain() {
    cleanup();
}

// =============================================================================
// Public API
// =============================================================================

VkResult VulkanRHISwapChain::acquireNextImage(VkSemaphore semaphore, uint32_t* imageIndex) {
    return vkAcquireNextImageKHR(device_->getVkDevice(), swapChain_,
                                 UINT64_MAX, semaphore, VK_NULL_HANDLE, imageIndex);
}

VkResult VulkanRHISwapChain::present(VkSemaphore waitSemaphore, uint32_t imageIndex) {
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &waitSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapChain_;
    presentInfo.pImageIndices = &imageIndex;
    return vkQueuePresentKHR(device_->getPresentQueue(), &presentInfo);
}

VkFramebuffer VulkanRHISwapChain::getVkFramebuffer(uint32_t index) const {
    return (index < framebuffers_.size()) ? framebuffers_[index] : VK_NULL_HANDLE;
}

void VulkanRHISwapChain::recreate(uint32_t width, uint32_t height) {
    requestedWidth_ = width;
    requestedHeight_ = height;
    device_->waitIdle();
    cleanup();
    createSwapChain();
    createImageViews();
    createRenderPass();
    createDepthResources();
    createFramebuffers();
}

// =============================================================================
// Private — SwapChain creation
// =============================================================================

void VulkanRHISwapChain::createSwapChain() {
    VkPhysicalDevice gpu = device_->getVkPhysicalDevice();
    VkSurfaceKHR surface = device_->getSurface();

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat();
    VkPresentModeKHR presentMode = chooseSwapPresentMode();
    VkExtent2D extent = chooseSwapExtent(caps);

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
        imageCount = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = caps.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device_->getVkDevice(), &createInfo, nullptr, &swapChain_) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanRHISwapChain] Failed to create swap chain!");
    }

    vkGetSwapchainImagesKHR(device_->getVkDevice(), swapChain_, &imageCount, nullptr);
    images_.resize(imageCount);
    vkGetSwapchainImagesKHR(device_->getVkDevice(), swapChain_, &imageCount, images_.data());

    vkFormat_ = surfaceFormat.format;
    vkExtent_ = extent;
    format_ = fromVkFormat(vkFormat_);
    extent_ = { vkExtent_.width, vkExtent_.height };
}

void VulkanRHISwapChain::createImageViews() {
    imageViews_.resize(images_.size());
    for (size_t i = 0; i < images_.size(); i++) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = images_[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = vkFormat_;
        viewInfo.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_->getVkDevice(), &viewInfo, nullptr, &imageViews_[i]) != VK_SUCCESS) {
            throw std::runtime_error("[VulkanRHISwapChain] Failed to create image view!");
        }
    }
}

void VulkanRHISwapChain::createRenderPass() {
    // Color attachment
    VkAttachmentDescription colorAtt{};
    colorAtt.format = vkFormat_;
    colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAtt.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Depth attachment — find best format
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT; // default
    VkFormatProperties props;
    for (VkFormat fmt : { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT }) {
        vkGetPhysicalDeviceFormatProperties(device_->getVkPhysicalDevice(), fmt, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            depthFormat = fmt;
            break;
        }
    }

    VkAttachmentDescription depthAtt{};
    depthAtt.format = depthFormat;
    depthAtt.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAtt.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                     | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                     | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                      | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = { colorAtt, depthAtt };
    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    rpInfo.pAttachments = attachments.data();
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dep;

    if (vkCreateRenderPass(device_->getVkDevice(), &rpInfo, nullptr, &renderPass_) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanRHISwapChain] Failed to create render pass!");
    }
}

void VulkanRHISwapChain::createDepthResources() {
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    VkFormatProperties props;
    for (VkFormat fmt : { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT }) {
        vkGetPhysicalDeviceFormatProperties(device_->getVkPhysicalDevice(), fmt, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            depthFormat = fmt;
            break;
        }
    }

    // Create depth image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { vkExtent_.width, vkExtent_.height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device_->getVkDevice(), &imageInfo, nullptr, &depthImage_) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanRHISwapChain] Failed to create depth image!");
    }

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device_->getVkDevice(), depthImage_, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = device_->findMemoryType(
        memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device_->getVkDevice(), &allocInfo, nullptr, &depthMemory_) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanRHISwapChain] Failed to allocate depth memory!");
    }
    vkBindImageMemory(device_->getVkDevice(), depthImage_, depthMemory_, 0);

    // Create depth image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device_->getVkDevice(), &viewInfo, nullptr, &depthView_) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanRHISwapChain] Failed to create depth image view!");
    }
}

void VulkanRHISwapChain::createFramebuffers() {
    framebuffers_.resize(imageViews_.size());
    for (size_t i = 0; i < imageViews_.size(); i++) {
        std::array<VkImageView, 2> attachments = { imageViews_[i], depthView_ };

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = renderPass_;
        fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        fbInfo.pAttachments = attachments.data();
        fbInfo.width = vkExtent_.width;
        fbInfo.height = vkExtent_.height;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(device_->getVkDevice(), &fbInfo, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
            throw std::runtime_error("[VulkanRHISwapChain] Failed to create framebuffer!");
        }
    }
}

void VulkanRHISwapChain::cleanup() {
    VkDevice vkDev = device_->getVkDevice();

    if (depthView_ != VK_NULL_HANDLE)  { vkDestroyImageView(vkDev, depthView_, nullptr); depthView_ = VK_NULL_HANDLE; }
    if (depthImage_ != VK_NULL_HANDLE) { vkDestroyImage(vkDev, depthImage_, nullptr); depthImage_ = VK_NULL_HANDLE; }
    if (depthMemory_ != VK_NULL_HANDLE){ vkFreeMemory(vkDev, depthMemory_, nullptr); depthMemory_ = VK_NULL_HANDLE; }

    for (auto fb : framebuffers_) vkDestroyFramebuffer(vkDev, fb, nullptr);
    framebuffers_.clear();

    if (renderPass_ != VK_NULL_HANDLE) { vkDestroyRenderPass(vkDev, renderPass_, nullptr); renderPass_ = VK_NULL_HANDLE; }

    for (auto iv : imageViews_) vkDestroyImageView(vkDev, iv, nullptr);
    imageViews_.clear();
    images_.clear();

    if (swapChain_ != VK_NULL_HANDLE) { vkDestroySwapchainKHR(vkDev, swapChain_, nullptr); swapChain_ = VK_NULL_HANDLE; }
}

// =============================================================================
// Helpers — choose best config
// =============================================================================

VkSurfaceFormatKHR VulkanRHISwapChain::chooseSwapSurfaceFormat() {
    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device_->getVkPhysicalDevice(), device_->getSurface(), &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device_->getVkPhysicalDevice(), device_->getSurface(), &fmtCount, formats.data());

    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return f;
        }
    }
    return formats[0];
}

VkPresentModeKHR VulkanRHISwapChain::chooseSwapPresentMode() {
    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device_->getVkPhysicalDevice(), device_->getSurface(), &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device_->getVkPhysicalDevice(), device_->getSurface(), &modeCount, modes.data());

    for (auto m : modes) {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanRHISwapChain::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& caps) {
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return caps.currentExtent;
    }
    VkExtent2D actual = { requestedWidth_, requestedHeight_ };
    actual.width  = std::clamp(actual.width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
    actual.height = std::clamp(actual.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return actual;
}
