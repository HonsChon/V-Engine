#include "DX12RHIFramebuffer.h"

DX12RHIFramebuffer::DX12RHIFramebuffer(DX12RHIRenderPass* /*renderPass*/,
                                       const std::vector<DX12RHITexture*>& /*attachments*/,
                                       uint32_t width, uint32_t height,
                                       D3D12_CPU_DESCRIPTOR_HANDLE* rtvHandles,
                                       D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
                                       bool hasDepth)
    : width_(width)
    , height_(height)
    , dsvHandle_(dsvHandle)
    , hasDepth_(hasDepth)
{
    if (rtvHandles) {
        colorAttachmentCount_ = hasDepth ? 1 : 1; // minimal: infer from hasDepth+rtvHandles
        for (uint32_t i = 0; i < colorAttachmentCount_; ++i) {
            rtvHandles_.push_back(rtvHandles[i]);
        }
    }
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12RHIFramebuffer::getRTVHandle(uint32_t index) const {
    if (index < rtvHandles_.size()) {
        return rtvHandles_[index];
    }
    return {};
}
