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
    VulkanRHISwapChain(VulkanRHIDevice* device, const RHISwapChainDesc& desc);
    ~VulkanRHISwapChain() override;

    RHIFormat    getFormat() const override { return format_; }
    RHIExtent2D  getExtent() const override { return extent_; }
    uint32_t     getImageCount() const override { return static_cast<uint32_t>(imageViews_.size()); }

    // ---- RHISwapChain interface ----
    RHISwapChainResult acquireNextImage(void* signalSemaphore, uint32_t* outImageIndex) override;
    RHISwapChainResult present(void* waitSemaphore, uint32_t imageIndex) override;
    void recreate(uint32_t width, uint32_t height) override;

    RHIRenderPass*  getRHIRenderPass() const override;
    RHIFramebuffer* getRHIFramebuffer(uint32_t imageIndex) const override;

    void* getNativeRenderPass() const override { return (void*)renderPass_; }
    void* getNativeFramebuffer(uint32_t imageIndex) const override;

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
    RHISwapChainDesc desc_;

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

    // RHI wrappers (non-owning, lazily created)
    mutable std::unique_ptr<VulkanRHIRenderPass> rhiRenderPass_;
    mutable std::vector<std::unique_ptr<VulkanRHIFramebuffer>> rhiFramebuffers_;
    void createRHIWrappers() const;

    // Depth resources
    VkImage        depthImage_  = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView    depthView_   = VK_NULL_HANDLE;
};
