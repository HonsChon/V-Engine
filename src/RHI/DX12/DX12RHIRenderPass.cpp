#include "DX12RHIRenderPass.h"

DX12RHIRenderPass::DX12RHIRenderPass(const RHIRenderPassDesc& desc)
{
    colorAttachmentCount_ = static_cast<uint32_t>(desc.colorAttachments.size());
    for (const auto& att : desc.colorAttachments) {
        colorFormats_.push_back(att.format);
    }
    hasDepth_ = desc.hasDepthAttachment;
    depthFormat_ = desc.depthAttachment.format;
}

RHIFormat DX12RHIRenderPass::getColorFormat(uint32_t index) const {
    if (index < colorFormats_.size()) {
        return colorFormats_[index];
    }
    return RHIFormat::Undefined;
}
