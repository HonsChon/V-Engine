#pragma once

#include "RHITypes.h"
#include <cstdint>
#include <memory>
#include <vector>

// Forward declarations
class RHITexture;

// =============================================================================
// RenderPass Attachment Description
// =============================================================================

struct RHIAttachmentDesc {
    RHIFormat      format       = RHIFormat::Undefined;
    RHISampleCount samples      = RHISampleCount::Count1;
    RHILoadOp      loadOp       = RHILoadOp::Clear;
    RHIStoreOp     storeOp      = RHIStoreOp::Store;
    RHILoadOp      stencilLoadOp  = RHILoadOp::DontCare;
    RHIStoreOp     stencilStoreOp = RHIStoreOp::DontCare;
    RHIImageLayout initialLayout  = RHIImageLayout::Undefined;
    RHIImageLayout finalLayout    = RHIImageLayout::ShaderReadOnly;
};

// =============================================================================
// RenderPass Description
// =============================================================================

struct RHIRenderPassDesc {
    std::vector<RHIAttachmentDesc> colorAttachments;
    bool hasDepthAttachment = false;
    RHIAttachmentDesc depthAttachment;

    RHIRenderPassDesc& addColorAttachment(
        RHIFormat format,
        RHILoadOp loadOp = RHILoadOp::Clear,
        RHIStoreOp storeOp = RHIStoreOp::Store,
        RHIImageLayout initialLayout = RHIImageLayout::Undefined,
        RHIImageLayout finalLayout = RHIImageLayout::ShaderReadOnly)
    {
        RHIAttachmentDesc desc;
        desc.format = format;
        desc.loadOp = loadOp;
        desc.storeOp = storeOp;
        desc.initialLayout = initialLayout;
        desc.finalLayout = finalLayout;
        colorAttachments.push_back(desc);
        return *this;
    }

    RHIRenderPassDesc& setDepthAttachment(
        RHIFormat format,
        RHILoadOp loadOp = RHILoadOp::Clear,
        RHIStoreOp storeOp = RHIStoreOp::Store,
        RHIImageLayout initialLayout = RHIImageLayout::Undefined,
        RHIImageLayout finalLayout = RHIImageLayout::DepthStencilReadOnly)
    {
        hasDepthAttachment = true;
        depthAttachment.format = format;
        depthAttachment.loadOp = loadOp;
        depthAttachment.storeOp = storeOp;
        depthAttachment.initialLayout = initialLayout;
        depthAttachment.finalLayout = finalLayout;
        return *this;
    }
};

// =============================================================================
// RHI RenderPass — Abstract Interface
// =============================================================================

class RHIRenderPass {
public:
    virtual ~RHIRenderPass() = default;

    virtual uint32_t getColorAttachmentCount() const = 0;

    RHIRenderPass(const RHIRenderPass&) = delete;
    RHIRenderPass& operator=(const RHIRenderPass&) = delete;

protected:
    RHIRenderPass() = default;
};

// =============================================================================
// Framebuffer Description
// =============================================================================

struct RHIFramebufferDesc {
    RHIRenderPass*            renderPass = nullptr;
    std::vector<RHITexture*>  attachments;   // color + depth, in order
    uint32_t                  width  = 0;
    uint32_t                  height = 0;
};

// =============================================================================
// RHI Framebuffer — Abstract Interface
// =============================================================================

class RHIFramebuffer {
public:
    virtual ~RHIFramebuffer() = default;

    virtual uint32_t getWidth() const = 0;
    virtual uint32_t getHeight() const = 0;

    RHIFramebuffer(const RHIFramebuffer&) = delete;
    RHIFramebuffer& operator=(const RHIFramebuffer&) = delete;

protected:
    RHIFramebuffer() = default;
};
