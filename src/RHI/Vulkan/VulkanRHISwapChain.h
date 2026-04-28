#pragma once

#include "RHISwapChain.h"
#include "VulkanRHIRenderPass.h"
#include "VulkanTypeConversions.h"

#include <vulkan/vulkan.h>
#include <vector>

class VulkanRHIDevice;

// =============================================================================
// VulkanRHISwapChain — wraps VkSwapchainKHR + depth + framebuffers
// =============================================================================

class VulkanRHISwapChain : public RHISwapChain {
public:
    VulkanRHISwapChain(VulkanRHIDevice* device, uint32_t width, uint32_t height);
    ~VulkanRHISwapChain() override;

    RHIFormat    getFormat() const override { return format_; }
    RHIExtent2D  getExtent() const override { return extent_; }
    uint32_t     getImageCount() const override { return static_cast<uint32_t>(imageViews_.size()); }

    // Acquire / present (returns VkResult for caller to handle OUT_OF_DATE)
    VkResult acquireNextImage(VkSemaphore semaphore, uint32_t* imageIndex);
    VkResult present(VkSemaphore waitSemaphore, uint32_t imageIndex);

    void recreate(uint32_t width, uint32_t height);

    // Native handles
    VkSwapchainKHR        getVkSwapChain() const { return swapChain_; }
    VkRenderPass          getVkRenderPass() const { return renderPass_; }
    VkFramebuffer         getVkFramebuffer(uint32_t index) const;
    const VkImageView&    getVkImageView(uint32_t index) const { return imageViews_[index]; }
    VkFormat              getVkFormat() const { return vkFormat_; }
    VkExtent2D            getVkExtent() const { return vkExtent_; }

private:
    void createSwapChain();
    void createImageViews();
    void createRenderPass();
    void createDepthResources();
    void createFramebuffers();
    void cleanup();

    VkSurfaceFormatKHR chooseSwapSurfaceFormat();
    VkPresentModeKHR   chooseSwapPresentMode();
    VkExtent2D         chooseSwapExtent(const VkSurfaceCapabilitiesKHR& caps);

    VulkanRHIDevice* device_;
    uint32_t requestedWidth_  = 0;
    uint32_t requestedHeight_ = 0;

    // RHI-level state
    RHIFormat   format_ = RHIFormat::Undefined;
    RHIExtent2D extent_ = { 0, 0 };

    // Vulkan handles
    VkSwapchainKHR         swapChain_ = VK_NULL_HANDLE;
    VkFormat               vkFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D             vkExtent_ = { 0, 0 };
    std::vector<VkImage>       images_;
    std::vector<VkImageView>   imageViews_;
    std::vector<VkFramebuffer> framebuffers_;

    // Render pass for the final presentation pass
    VkRenderPass renderPass_ = VK_NULL_HANDLE;

    // Depth resources
    VkImage        depthImage_  = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView    depthView_   = VK_NULL_HANDLE;
};
