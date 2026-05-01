#pragma once

#include "RHIRenderPass.h"
#include <vulkan/vulkan.h>
#include <vector>

class VulkanRHIDevice;

// =============================================================================
// VulkanRHIRenderPass — wraps VkRenderPass
// =============================================================================

class VulkanRHIRenderPass : public RHIRenderPass {
public:
    /// Create an owning render pass from description
    VulkanRHIRenderPass(VulkanRHIDevice* device, const RHIRenderPassDesc& desc);

    /// Wrap an existing VkRenderPass (non-owning — won't destroy on delete)
    VulkanRHIRenderPass(VkRenderPass externalRenderPass);

    ~VulkanRHIRenderPass() override;

    uint32_t getColorAttachmentCount() const override {
        return static_cast<uint32_t>(desc_.colorAttachments.size());
    }

    VkRenderPass getVkRenderPass() const { return renderPass_; }
    const RHIRenderPassDesc& getDesc() const { return desc_; }

private:
    VulkanRHIDevice*  device_ = nullptr;
    RHIRenderPassDesc desc_;
    VkRenderPass      renderPass_ = VK_NULL_HANDLE;
    bool              ownsRenderPass_ = true;
};

// =============================================================================
// VulkanRHIFramebuffer — wraps VkFramebuffer
// =============================================================================

class VulkanRHIFramebuffer : public RHIFramebuffer {
public:
    /// Create an owning framebuffer from description
    VulkanRHIFramebuffer(VulkanRHIDevice* device, const RHIFramebufferDesc& desc);

    /// Wrap an existing VkFramebuffer (non-owning — won't destroy on delete)
    VulkanRHIFramebuffer(VkFramebuffer externalFramebuffer, uint32_t width, uint32_t height);

    ~VulkanRHIFramebuffer() override;

    uint32_t getWidth() const override { return width_; }
    uint32_t getHeight() const override { return height_; }

    VkFramebuffer getVkFramebuffer() const { return framebuffer_; }

private:
    VulkanRHIDevice* device_ = nullptr;
    VkFramebuffer    framebuffer_ = VK_NULL_HANDLE;
    uint32_t         width_  = 0;
    uint32_t         height_ = 0;
    bool             ownsFramebuffer_ = true;
};
