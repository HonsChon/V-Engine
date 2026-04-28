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
    VulkanRHIRenderPass(VulkanRHIDevice* device, const RHIRenderPassDesc& desc);
    ~VulkanRHIRenderPass() override;

    uint32_t getColorAttachmentCount() const override {
        return static_cast<uint32_t>(desc_.colorAttachments.size());
    }

    VkRenderPass getVkRenderPass() const { return renderPass_; }
    const RHIRenderPassDesc& getDesc() const { return desc_; }

private:
    VulkanRHIDevice*  device_;
    RHIRenderPassDesc desc_;
    VkRenderPass      renderPass_ = VK_NULL_HANDLE;
};

// =============================================================================
// VulkanRHIFramebuffer — wraps VkFramebuffer
// =============================================================================

class VulkanRHIFramebuffer : public RHIFramebuffer {
public:
    VulkanRHIFramebuffer(VulkanRHIDevice* device, const RHIFramebufferDesc& desc);
    ~VulkanRHIFramebuffer() override;

    uint32_t getWidth() const override { return width_; }
    uint32_t getHeight() const override { return height_; }

    VkFramebuffer getVkFramebuffer() const { return framebuffer_; }

private:
    VulkanRHIDevice* device_;
    VkFramebuffer    framebuffer_ = VK_NULL_HANDLE;
    uint32_t         width_  = 0;
    uint32_t         height_ = 0;
};
