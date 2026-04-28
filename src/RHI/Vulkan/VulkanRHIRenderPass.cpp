#include "VulkanRHIRenderPass.h"
#include "VulkanRHIDevice.h"
#include "VulkanRHITexture.h"
#include "VulkanTypeConversions.h"
#include <stdexcept>

using namespace VulkanTypeConversions;

// =============================================================================
// VulkanRHIRenderPass
// =============================================================================

VulkanRHIRenderPass::VulkanRHIRenderPass(VulkanRHIDevice* device,
                                         const RHIRenderPassDesc& desc)
    : device_(device), desc_(desc)
{
    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkAttachmentReference>   colorRefs;

    // Color attachments
    for (uint32_t i = 0; i < desc_.colorAttachments.size(); ++i) {
        const auto& ca = desc_.colorAttachments[i];
        VkAttachmentDescription att{};
        att.format         = toVkFormat(ca.format);
        att.samples        = toVkSampleCount(ca.samples);
        att.loadOp         = toVkLoadOp(ca.loadOp);
        att.storeOp        = toVkStoreOp(ca.storeOp);
        att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout  = toVkImageLayout(ca.initialLayout);
        att.finalLayout    = toVkImageLayout(ca.finalLayout);
        attachments.push_back(att);

        VkAttachmentReference ref{};
        ref.attachment = i;
        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorRefs.push_back(ref);
    }

    // Depth attachment
    VkAttachmentReference depthRef{};
    if (desc_.hasDepthAttachment) {
        const auto& da = desc_.depthAttachment;
        VkAttachmentDescription att{};
        att.format         = toVkFormat(da.format);
        att.samples        = toVkSampleCount(da.samples);
        att.loadOp         = toVkLoadOp(da.loadOp);
        att.storeOp        = toVkStoreOp(da.storeOp);
        att.stencilLoadOp  = toVkLoadOp(da.stencilLoadOp);
        att.stencilStoreOp = toVkStoreOp(da.stencilStoreOp);
        att.initialLayout  = toVkImageLayout(da.initialLayout);
        att.finalLayout    = toVkImageLayout(da.finalLayout);
        attachments.push_back(att);

        depthRef.attachment = static_cast<uint32_t>(attachments.size() - 1);
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    // Single subpass
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
    subpass.pColorAttachments = colorRefs.data();
    subpass.pDepthStencilAttachment = desc_.hasDepthAttachment ? &depthRef : nullptr;

    // Default dependency: external → subpass 0
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                            | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
                            | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                             | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    rpInfo.pAttachments = attachments.data();
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device_->getVkDevice(), &rpInfo, nullptr, &renderPass_) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanRHIRenderPass] Failed to create render pass!");
    }
}

VulkanRHIRenderPass::~VulkanRHIRenderPass() {
    if (renderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_->getVkDevice(), renderPass_, nullptr);
    }
}

// =============================================================================
// VulkanRHIFramebuffer
// =============================================================================

VulkanRHIFramebuffer::VulkanRHIFramebuffer(VulkanRHIDevice* device,
                                           const RHIFramebufferDesc& desc)
    : device_(device), width_(desc.width), height_(desc.height)
{
    auto* vkRP = static_cast<VulkanRHIRenderPass*>(desc.renderPass);

    std::vector<VkImageView> views;
    views.reserve(desc.attachments.size());
    for (auto* att : desc.attachments) {
        auto* vkTex = static_cast<VulkanRHITexture*>(att);
        views.push_back(vkTex->getVkImageView());
    }

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = vkRP->getVkRenderPass();
    fbInfo.attachmentCount = static_cast<uint32_t>(views.size());
    fbInfo.pAttachments = views.data();
    fbInfo.width = width_;
    fbInfo.height = height_;
    fbInfo.layers = 1;

    if (vkCreateFramebuffer(device_->getVkDevice(), &fbInfo, nullptr, &framebuffer_) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanRHIFramebuffer] Failed to create framebuffer!");
    }
}

VulkanRHIFramebuffer::~VulkanRHIFramebuffer() {
    if (framebuffer_ != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device_->getVkDevice(), framebuffer_, nullptr);
    }
}
