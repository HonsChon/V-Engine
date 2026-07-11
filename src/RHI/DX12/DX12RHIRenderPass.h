#pragma once

#include "RHIRenderPass.h"

#include <vector>
#include <directx/d3d12.h>

class DX12RHIRenderPass : public RHIRenderPass
{
public:
    DX12RHIRenderPass(const RHIRenderPassDesc& desc);
    ~DX12RHIRenderPass() override = default;

    uint32_t getColorAttachmentCount() const override { return colorAttachmentCount_; }

    RHIFormat       getColorFormat(uint32_t index) const;
    RHIFormat       getDepthFormat() const { return depthFormat_; }
    bool            hasDepthAttachment() const { return hasDepth_; }

private:
    uint32_t                colorAttachmentCount_ = 0;
    std::vector<RHIFormat>  colorFormats_;
    RHIFormat               depthFormat_ = RHIFormat::Undefined;
    bool                    hasDepth_ = false;
};

