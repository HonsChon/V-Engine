#pragma once

#include "RHIRenderPass.h"

#include <vector>
#include <directx/d3d12.h>

class DX12RHIRenderPass : public RHIRenderPass
{
public:
    DX12RHIRenderPass(const RHIRenderPassDesc& desc);
    ~DX12RHIRenderPass() override = default;

    uint32_t getColorAttachmentCount() const override { return static_cast<uint32_t>(colorAttachments_.size()); }

    RHIFormat getColorFormat(uint32_t index) const {
        return (index < colorAttachments_.size()) ? colorAttachments_[index].format : RHIFormat::Undefined;
    }
    const RHIAttachmentDesc& getColorAttachment(uint32_t index) const { return colorAttachments_[index]; }

    const RHIAttachmentDesc* getDepthAttachment() const { return hasDepth_ ? &depthAttachment_ : nullptr; }
    RHIFormat getDepthFormat() const { return depthAttachment_.format; }
    bool      hasDepthAttachment() const { return hasDepth_; }

private:
    std::vector<RHIAttachmentDesc> colorAttachments_;
    RHIAttachmentDesc              depthAttachment_;
    bool                           hasDepth_ = false;
};
