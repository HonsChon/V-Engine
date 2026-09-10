#include "DX12RHIRenderPass.h"

// D3D12 has no render-pass object: the desc is kept alive so that
// DX12RHICommandBuffer::beginRenderPass can honor per-attachment loadOp /
// final layout semantics at record time.
DX12RHIRenderPass::DX12RHIRenderPass(const RHIRenderPassDesc& desc)
    : colorAttachments_(desc.colorAttachments)
    , hasDepth_(desc.hasDepthAttachment)
{
    if (hasDepth_) {
        depthAttachment_ = desc.depthAttachment;
    }
}
