#include "DX12RHICommandBuffer.h"
#include "DX12RHIDevice.h"
#include "DX12RHIBuffer.h"
#include "DX12RHITexture.h"
#include "DX12RHIPipeline.h"
#include "DX12RHIDescriptor.h"
#include "DX12RHIRenderPass.h"
#include "DX12RHIFramebuffer.h"
#include "DX12RHISampler.h"

#include <stdexcept>
#include <algorithm>
#include <vector>

using namespace DX12TypeConversions;

namespace {

D3D12_RESOURCE_STATES shaderReadStates() {
    return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
         | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
}

} // namespace

DX12RHICommandBuffer::DX12RHICommandBuffer(DX12RHIDevice* device, ID3D12GraphicsCommandList* cmdList)
    : device_(device), cmdList_(cmdList)
{
}

void DX12RHICommandBuffer::begin() {
    // Reset the per-frame allocator + list through the device pool (the list is
    // left in the recording state), then clear all cached session state.
    device_->resetCommandBuffer(cmdList_);
    currentPipeline_ = nullptr;
    isCompute_ = false;
    activeRenderPass_ = nullptr;
    activeFramebuffer_ = nullptr;
}

void DX12RHICommandBuffer::end() {
    if (FAILED(cmdList_->Close())) {
        throw std::runtime_error("[DX12RHICommandBuffer] failed to close command list");
    }
}

// =============================================================================
// State plumbing
// =============================================================================

void DX12RHICommandBuffer::setDescriptorHeaps() {
    ID3D12DescriptorHeap* heaps[] = {
        device_->getShaderVisibleResourceHeap(),
        device_->getShaderVisibleSamplerHeap(),
    };
    cmdList_->SetDescriptorHeaps(2, heaps);
}

void DX12RHICommandBuffer::transitionTo(ID3D12Resource* resource,
                                        D3D12_RESOURCE_STATES from,
                                        D3D12_RESOURCE_STATES to) {
    if (from == to) {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = from;
    barrier.Transition.StateAfter = to;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList_->ResourceBarrier(1, &barrier);
}

D3D12_RESOURCE_STATES DX12RHICommandBuffer::textureStateFor(DX12RHITexture* texture,
                                                            RHIImageLayout layout) {
    D3D12_RESOURCE_STATES state = toD3D12ResourceStates(layout);
    if (layout == RHIImageLayout::General && texture) {
        // UAV writes from compute need the real UNORDERED_ACCESS state, not COMMON.
        if (hasFlag(texture->getUsage(), RHITextureUsage::Storage)) {
            state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
    }
    return state;
}

void DX12RHICommandBuffer::ensureState(DX12RHITexture* texture, D3D12_RESOURCE_STATES state) {
    if (!texture) {
        return;
    }
    transitionTo(texture->getD3D12Resource(), texture->getCurrentState(), state);
    texture->setCurrentState(state);
}

// =============================================================================
// Pipeline binding
// =============================================================================

void DX12RHICommandBuffer::bindPipelineInternal(DX12RHIPipeline* pipeline, bool isCompute) {
    setDescriptorHeaps();
    if (isCompute) {
        cmdList_->SetPipelineState(pipeline->getD3D12PipelineState());
        cmdList_->SetComputeRootSignature(pipeline->getD3D12RootSignature());
    } else {
        cmdList_->SetPipelineState(pipeline->getD3D12PipelineState());
        cmdList_->SetGraphicsRootSignature(pipeline->getD3D12RootSignature());
        cmdList_->IASetPrimitiveTopology(toD3DPrimitiveTopology(pipeline->getPrimitiveTopology()));
    }
    currentPipeline_ = pipeline;
    isCompute_ = isCompute;
}

void DX12RHICommandBuffer::bindGraphicsPipeline(RHIPipeline* pipeline) {
    bindPipelineInternal(static_cast<DX12RHIPipeline*>(pipeline), false);
}

void DX12RHICommandBuffer::bindComputePipeline(RHIPipeline* pipeline) {
    bindPipelineInternal(static_cast<DX12RHIPipeline*>(pipeline), true);
}

// ---- Render pass ----

void DX12RHICommandBuffer::beginRenderPass(RHIRenderPass* renderPass,
                                           RHIFramebuffer* framebuffer,
                                           const std::vector<RHIClearValue>& clearValues) {
    auto* dxRP = static_cast<DX12RHIRenderPass*>(renderPass);
    auto* dxFB = static_cast<DX12RHIFramebuffer*>(framebuffer);

    const UINT numRTVs = dxFB->getColorAttachmentCount();
    if (numRTVs > 8) {
        throw std::runtime_error("[DX12RHICommandBuffer] more than 8 render targets");
    }

    // Vulkan render passes transition attachments implicitly; do it explicitly here.
    for (UINT i = 0; i < numRTVs; ++i) {
        DX12RHITexture* tex = dxFB->getColorTexture(i);
        ensureState(tex, D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    if (dxFB->hasDepthAttachment()) {
        ensureState(dxFB->getDepthTexture(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[8] = {};
    for (UINT i = 0; i < numRTVs; ++i) {
        rtvHandles[i] = dxFB->getRTVHandle(i);
    }
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};
    D3D12_CPU_DESCRIPTOR_HANDLE* pDSV = nullptr;
    if (dxFB->hasDepthAttachment()) {
        dsvHandle = dxFB->getDSVHandle();
        pDSV = &dsvHandle;
    }
    cmdList_->OMSetRenderTargets(numRTVs, rtvHandles, FALSE, pDSV);

    // Honor per-attachment loadOp: clear only attachments whose loadOp == Clear,
    // using the matching clear value (fixes the old "clear every RTV for every
    // clear value" N^2 bug).
    const RHIAttachmentDesc* depthAtt = dxRP->getDepthAttachment();

    UINT colorClearIndex = 0;
    bool depthClear = false;
    for (const auto& cv : clearValues) {
        if (cv.type == RHIClearValue::Type::Color) {
            const UINT attach = colorClearIndex++;
            if (attach < numRTVs &&
                dxRP->getColorAttachment(attach).loadOp == RHILoadOp::Clear) {
                const float color[4] = { cv.color.r, cv.color.g, cv.color.b, cv.color.a };
                cmdList_->ClearRenderTargetView(rtvHandles[attach], color, 0, nullptr);
            }
        } else if (cv.type == RHIClearValue::Type::DepthStencil && depthAtt && !depthClear) {
            depthClear = true;
            if (depthAtt->loadOp == RHILoadOp::Clear && dxFB->hasDepthAttachment()) {
                D3D12_CLEAR_FLAGS flags = D3D12_CLEAR_FLAG_DEPTH;
                if (depthAtt->stencilLoadOp == RHILoadOp::Clear && hasStencil(depthAtt->format)) {
                    flags |= D3D12_CLEAR_FLAG_STENCIL;
                }
                cmdList_->ClearDepthStencilView(dsvHandle, flags,
                                                cv.depthStencil.depth, cv.depthStencil.stencil,
                                                0, nullptr);
            }
        }
    }

    activeRenderPass_ = dxRP;
    activeFramebuffer_ = dxFB;
}

void DX12RHICommandBuffer::endRenderPass() {
    // D3D12 has no render-pass object, so this method itself is a no-op; the
    // Vulkan-equivalent "final layout" transitions are emitted here instead so
    // tracked resource states stay accurate for the next pass / present.
    if (!activeRenderPass_ || !activeFramebuffer_) {
        return;
    }
    auto* dxRP = activeRenderPass_;
    auto* dxFB = activeFramebuffer_;

    for (UINT i = 0; i < dxFB->getColorAttachmentCount(); ++i) {
        const RHIAttachmentDesc& att = dxRP->getColorAttachment(i);
        if (att.finalLayout == RHIImageLayout::Undefined) {
            continue;
        }
        DX12RHITexture* tex = dxFB->getColorTexture(i);
        D3D12_RESOURCE_STATES target = textureStateFor(tex, att.finalLayout);
        ensureState(tex, target);
    }
    if (dxFB->hasDepthAttachment() && dxRP->getDepthAttachment()) {
        RHIImageLayout final = dxRP->getDepthAttachment()->finalLayout;
        if (final != RHIImageLayout::Undefined) {
            DX12RHITexture* tex = dxFB->getDepthTexture();
            D3D12_RESOURCE_STATES target = textureStateFor(tex, final);
            if (final == RHIImageLayout::DepthStencilReadOnly) {
                target = D3D12_RESOURCE_STATE_DEPTH_READ;
            }
            ensureState(tex, target);
        }
    }

    activeRenderPass_ = nullptr;
    activeFramebuffer_ = nullptr;
}

// ---- Descriptor / Binding group ----

void DX12RHICommandBuffer::setBindingGroup(uint32_t set, RHIBindingGroup* group) {
    if (!currentPipeline_) {
        throw std::runtime_error("[DX12RHICommandBuffer] setBindingGroup without a bound pipeline");
    }
    auto* dxGroup = static_cast<DX12RHIBindingGroup*>(group);
    setDescriptorHeaps();

    const int resourceParam = currentPipeline_->getTableRootParam(set, /*sampler=*/false);
    const int samplerParam  = currentPipeline_->getTableRootParam(set, /*sampler=*/true);

    if (isCompute_) {
        if (resourceParam >= 0) {
            cmdList_->SetComputeRootDescriptorTable(static_cast<UINT>(resourceParam),
                                                    dxGroup->getResourceGPUHandle());
        }
        if (samplerParam >= 0 && dxGroup->hasSamplerBlock()) {
            cmdList_->SetComputeRootDescriptorTable(static_cast<UINT>(samplerParam),
                                                    dxGroup->getSamplerGPUHandle());
        }
    } else {
        if (resourceParam >= 0) {
            cmdList_->SetGraphicsRootDescriptorTable(static_cast<UINT>(resourceParam),
                                                     dxGroup->getResourceGPUHandle());
        }
        if (samplerParam >= 0 && dxGroup->hasSamplerBlock()) {
            cmdList_->SetGraphicsRootDescriptorTable(static_cast<UINT>(samplerParam),
                                                     dxGroup->getSamplerGPUHandle());
        }
    }
}

// ---- Viewport / Scissor ----

void DX12RHICommandBuffer::setViewport(float x, float y, float width, float height,
                                       float minDepth, float maxDepth) {
    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = x;
    viewport.TopLeftY = y;
    viewport.Width = width;
    viewport.Height = height;
    viewport.MinDepth = minDepth;
    viewport.MaxDepth = maxDepth;
    cmdList_->RSSetViewports(1, &viewport);
}

void DX12RHICommandBuffer::setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) {
    D3D12_RECT scissor = {};
    scissor.left = x;
    scissor.top = y;
    scissor.right = x + static_cast<LONG>(width);
    scissor.bottom = y + static_cast<LONG>(height);
    cmdList_->RSSetScissorRects(1, &scissor);
}

// ---- Vertex / Index buffer binding ----

void DX12RHICommandBuffer::bindVertexBuffer(uint32_t binding, RHIBuffer* buffer, uint64_t offset) {
    auto* dxBuf = static_cast<DX12RHIBuffer*>(buffer);
    const int stride = currentPipeline_ ? currentPipeline_->getVertexBindingStride(binding) : -1;
    if (stride < 0) {
        throw std::runtime_error("[DX12RHICommandBuffer] bindVertexBuffer: no input-layout stride known for slot "
                                 + std::to_string(binding) + " (bind a graphics pipeline first)");
    }
    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    vbv.BufferLocation = dxBuf->getGPUVirtualAddress() + offset;
    vbv.SizeInBytes = static_cast<UINT>(dxBuf->getResourceSize() - offset);
    vbv.StrideInBytes = static_cast<UINT>(stride);
    cmdList_->IASetVertexBuffers(binding, 1, &vbv);
}

void DX12RHICommandBuffer::bindIndexBuffer(RHIBuffer* buffer, uint64_t offset,
                                           RHIIndexType indexType) {
    auto* dxBuf = static_cast<DX12RHIBuffer*>(buffer);
    D3D12_INDEX_BUFFER_VIEW ibv = {};
    ibv.BufferLocation = dxBuf->getGPUVirtualAddress() + offset;
    ibv.SizeInBytes = static_cast<UINT>(dxBuf->getResourceSize() - offset);
    ibv.Format = toD3D12IndexFormat(indexType);
    cmdList_->IASetIndexBuffer(&ibv);
}

// ---- Draw commands ----

void DX12RHICommandBuffer::draw(uint32_t vertexCount, uint32_t instanceCount,
                                uint32_t firstVertex, uint32_t firstInstance) {
    cmdList_->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
}

void DX12RHICommandBuffer::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                       uint32_t firstIndex, int32_t vertexOffset,
                                       uint32_t firstInstance) {
    cmdList_->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void DX12RHICommandBuffer::drawIndexedIndirect(RHIBuffer* /*buffer*/, uint64_t /*offset*/,
                                               uint32_t /*drawCount*/, uint32_t /*stride*/) {
    throw std::runtime_error("[DX12RHICommandBuffer] drawIndexedIndirect needs a command signature "
                             "(ExecuteIndirect): not implemented (Phase 5 backlog)");
}

// ---- Compute commands ----

void DX12RHICommandBuffer::dispatch(uint32_t groupCountX, uint32_t groupCountY,
                                    uint32_t groupCountZ) {
    cmdList_->Dispatch(groupCountX, groupCountY, groupCountZ);
}

void DX12RHICommandBuffer::dispatchIndirect(RHIBuffer* /*buffer*/, uint64_t /*offset*/) {
    throw std::runtime_error("[DX12RHICommandBuffer] dispatchIndirect needs a command signature "
                             "(ExecuteIndirect): not implemented (Phase 5 backlog)");
}

// ---- Push constants ----

void DX12RHICommandBuffer::pushConstants(RHIShaderStage /*stages*/, uint32_t offset,
                                         uint32_t size, const void* data) {
    if (!currentPipeline_ || !currentPipeline_->hasPushConstants()) {
        throw std::runtime_error("[DX12RHICommandBuffer] pushConstants: pipeline has no push constant range");
    }
    if ((size & 3) != 0 || (offset & 3) != 0) {
        throw std::runtime_error("[DX12RHICommandBuffer] pushConstants must be 4-byte aligned");
    }
    const UINT rootParam = static_cast<UINT>(currentPipeline_->getPushConstantRootParam());
    const UINT numValues = size / sizeof(uint32_t);
    const UINT destOffset = offset / sizeof(uint32_t);
    if (isCompute_) {
        cmdList_->SetComputeRoot32BitConstants(rootParam, numValues, data, destOffset);
    } else {
        cmdList_->SetGraphicsRoot32BitConstants(rootParam, numValues, data, destOffset);
    }
}

// ---- Barriers / Transitions ----

void DX12RHICommandBuffer::pipelineBarrier(RHIPipelineStage /*srcStage*/,
                                           RHIPipelineStage /*dstStage*/,
                                           RHIAccessFlags /*srcAccess*/,
                                           RHIAccessFlags /*dstAccess*/) {
    // The engine only submits compute->graphics ShaderWrite->ShaderRead barriers,
    // i.e. UAV hazards on a single queue: a null-resource UAV barrier covers this
    // without tracking individual resources (legacy barrier model).
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.UAV.pResource = nullptr;
    cmdList_->ResourceBarrier(1, &barrier);
}

void DX12RHICommandBuffer::transitionImageLayout(RHITexture* texture,
                                                 RHIImageLayout oldLayout,
                                                 RHIImageLayout newLayout,
                                                 RHIPipelineStage /*srcStage*/,
                                                 RHIPipelineStage /*dstStage*/) {
    (void)oldLayout; // the tracked current state is authoritative
    auto* dxTex = static_cast<DX12RHITexture*>(texture);
    D3D12_RESOURCE_STATES target = textureStateFor(dxTex, newLayout);
    ensureState(dxTex, target);
}

// ---- Transfer ----

void DX12RHICommandBuffer::copyBuffer(RHIBuffer* src, RHIBuffer* dst, uint64_t size,
                                      uint64_t srcOffset, uint64_t dstOffset) {
    auto* dxSrc = static_cast<DX12RHIBuffer*>(src);
    auto* dxDst = static_cast<DX12RHIBuffer*>(dst);

    const D3D12_RESOURCE_STATES srcBefore = dxSrc->getCurrentState();
    const D3D12_RESOURCE_STATES dstBefore = dxDst->getCurrentState();

    transitionTo(dxSrc->getD3D12Resource(), srcBefore, D3D12_RESOURCE_STATE_COPY_SOURCE);
    dxSrc->setCurrentState(D3D12_RESOURCE_STATE_COPY_SOURCE);
    transitionTo(dxDst->getD3D12Resource(), dstBefore, D3D12_RESOURCE_STATE_COPY_DEST);
    dxDst->setCurrentState(D3D12_RESOURCE_STATE_COPY_DEST);

    cmdList_->CopyBufferRegion(dxDst->getD3D12Resource(), dstOffset,
                               dxSrc->getD3D12Resource(), srcOffset,
                               size);

    // Restore both buffers to their prior states so subsequent barriers issued by
    // the caller stay consistent with the tracked states.
    transitionTo(dxSrc->getD3D12Resource(), D3D12_RESOURCE_STATE_COPY_SOURCE, srcBefore);
    dxSrc->setCurrentState(srcBefore);
    transitionTo(dxDst->getD3D12Resource(), D3D12_RESOURCE_STATE_COPY_DEST, dstBefore);
    dxDst->setCurrentState(dstBefore);
}

void DX12RHICommandBuffer::fillBuffer(RHIBuffer* buffer, uint64_t offset, uint64_t size,
                                      uint32_t data) {
    auto* dxBuf = static_cast<DX12RHIBuffer*>(buffer);
    if (!hasFlag(dxBuf->getUsage(), RHIBufferUsage::Storage)) {
        throw std::runtime_error("[DX12RHICommandBuffer] fillBuffer requires Storage usage on the buffer");
    }
    if ((offset & 3) != 0 || (size & 3) != 0) {
        throw std::runtime_error("[DX12RHICommandBuffer] fillBuffer requires 4-byte aligned offset/size");
    }

    // UAV clear over exactly [offset, offset+size).
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = {};
    if (!device_->allocateResourceDescriptors(1, &cpu, &gpu)) {
        throw std::runtime_error("[DX12RHICommandBuffer] descriptor ring exhausted (fillBuffer)");
    }
    setDescriptorHeaps();

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
    uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Format = DXGI_FORMAT_R32_UINT;
    uav.Buffer.FirstElement = static_cast<UINT>(offset / 4);
    uav.Buffer.NumElements = static_cast<UINT>(std::max<uint64_t>(size / 4, 1));
    uav.Buffer.StructureByteStride = 0;
    uav.Buffer.CounterOffsetInBytes = 0;
    uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    device_->getDevice()->CreateUnorderedAccessView(dxBuf->getD3D12Resource(), nullptr, &uav, cpu);

    const D3D12_RESOURCE_STATES prior = dxBuf->getCurrentState();
    transitionTo(dxBuf->getD3D12Resource(), prior, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    dxBuf->setCurrentState(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    const UINT values[4] = { data, data, data, data };
    cmdList_->ClearUnorderedAccessViewUint(gpu, cpu, dxBuf->getD3D12Resource(),
                                           values, 0, nullptr);

    transitionTo(dxBuf->getD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, prior);
    dxBuf->setCurrentState(prior);
}

void DX12RHICommandBuffer::blitImage(RHITexture* src, RHIImageLayout /*srcLayout*/,
                                     RHITexture* dst, RHIImageLayout /*dstLayout*/,
                                     uint32_t srcWidth, uint32_t srcHeight,
                                     uint32_t dstWidth, uint32_t dstHeight,
                                     RHIFilter filter) {
    auto* dxSrc = static_cast<DX12RHITexture*>(src);
    auto* dxDst = static_cast<DX12RHITexture*>(dst);
    if (dstWidth == 0 || dstHeight == 0) {
        return;
    }

    // Transient view descriptors + RTV for the destination.
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = {};
    D3D12_CPU_DESCRIPTOR_HANDLE samCpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE samGpu = {};
    D3D12_CPU_DESCRIPTOR_HANDLE rtvCpu = {};
    if (!device_->allocateResourceDescriptors(1, &srvCpu, &srvGpu) ||
        !device_->allocateSamplerDescriptors(1, &samCpu, &samGpu) ||
        !device_->allocateRTVDescriptors(1, &rtvCpu)) {
        throw std::runtime_error("[DX12RHICommandBuffer] descriptor rings exhausted (blit)");
    }

    ID3D12Device* d3d = device_->getDevice();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = dxSrc->getSRVDesc();
    d3d->CreateShaderResourceView(dxSrc->getD3D12Resource(), &srvDesc, srvCpu);

    D3D12_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = (filter == RHIFilter::Nearest)
        ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
    d3d->CreateSampler(&samplerDesc, samCpu);

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = dxDst->getRTVDesc();
    d3d->CreateRenderTargetView(dxDst->getD3D12Resource(), &rtvDesc, rtvCpu);

    // dst must be a render target while the internal blit pipeline draws into it.
    const D3D12_RESOURCE_STATES srcBefore = dxSrc->getCurrentState();
    const D3D12_RESOURCE_STATES dstBefore = dxDst->getCurrentState();
    ensureState(dxSrc, shaderReadStates());
    ensureState(dxDst, D3D12_RESOURCE_STATE_RENDER_TARGET);

    std::shared_ptr<DX12RHIPipeline> blitPipeline = device_->getOrCreateBlitPipeline();
    bindPipelineInternal(blitPipeline.get(), false);

    setDescriptorHeaps();
    cmdList_->SetGraphicsRootDescriptorTable(0, srvGpu);
    cmdList_->SetGraphicsRootDescriptorTable(1, samGpu);

    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = static_cast<float>(dstWidth);
    viewport.Height = static_cast<float>(dstHeight);
    viewport.MaxDepth = 1.0f;
    cmdList_->RSSetViewports(1, &viewport);

    D3D12_RECT scissor = { 0, 0, static_cast<LONG>(dstWidth), static_cast<LONG>(dstHeight) };
    cmdList_->RSSetScissorRects(1, &scissor);

    cmdList_->OMSetRenderTargets(1, &rtvCpu, FALSE, nullptr);
    cmdList_->DrawInstanced(3, 1, 0, 0);

    // Restore caller-visible states so subsequent barrier calls stay consistent.
    ensureState(dxSrc, srcBefore);
    ensureState(dxDst, dstBefore);
}

// ---- Buffer barrier ----

void DX12RHICommandBuffer::bufferBarrier(RHIBuffer* buffer, uint64_t /*size*/,
                                         RHIPipelineStage /*srcStage*/,
                                         RHIPipelineStage /*dstStage*/,
                                         RHIAccessFlags srcAccess, RHIAccessFlags dstAccess) {
    auto* dxBuf = static_cast<DX12RHIBuffer*>(buffer);

    auto stateFor = [&](RHIAccessFlags access) -> D3D12_RESOURCE_STATES {
        const uint32_t a = static_cast<uint32_t>(access);
        if (a & static_cast<uint32_t>(RHIAccessFlags::ShaderWrite)) {
            return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }
        if (a & static_cast<uint32_t>(RHIAccessFlags::ShaderRead)) {
            // UAV buffers (Storage usage) are read in UNORDERED_ACCESS state.
            return hasFlag(dxBuf->getUsage(), RHIBufferUsage::Storage)
                ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
                : shaderReadStates();
        }
        if (a & static_cast<uint32_t>(RHIAccessFlags::TransferWrite)) {
            return D3D12_RESOURCE_STATE_COPY_DEST;
        }
        if (a & static_cast<uint32_t>(RHIAccessFlags::TransferRead)) {
            return D3D12_RESOURCE_STATE_COPY_SOURCE;
        }
        if (a & static_cast<uint32_t>(RHIAccessFlags::VertexAttributeRead)) {
            return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        }
        if (a & static_cast<uint32_t>(RHIAccessFlags::IndexRead)) {
            return D3D12_RESOURCE_STATE_INDEX_BUFFER;
        }
        if (a & static_cast<uint32_t>(RHIAccessFlags::UniformRead)) {
            return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        }
        return D3D12_RESOURCE_STATE_COMMON;
    };

    const D3D12_RESOURCE_STATES before = dxBuf->getCurrentState();
    const D3D12_RESOURCE_STATES after = stateFor(dstAccess);
    (void)srcAccess;
    if (before == after) {
        return;   // no state change -> nothing to transition
    }
    transitionTo(dxBuf->getD3D12Resource(), before, after);
    dxBuf->setCurrentState(after);
}

void DX12RHICommandBuffer::clearColorImage(RHITexture* texture, float r, float g, float b, float a) {
    auto* dxTex = static_cast<DX12RHITexture*>(texture);

    // Clear by rendering a constant-color fullscreen triangle (avoids the
    // ALLOW_UNORDERED_ACCESS requirement of ClearUnorderedAccessViewFloat).
    D3D12_CPU_DESCRIPTOR_HANDLE rtvCpu = {};
    if (!device_->allocateRTVDescriptors(1, &rtvCpu)) {
        throw std::runtime_error("[DX12RHICommandBuffer] RTV ring exhausted (clear)");
    }
    ID3D12Device* d3d = device_->getDevice();
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = dxTex->getRTVDesc();
    d3d->CreateRenderTargetView(dxTex->getD3D12Resource(), &rtvDesc, rtvCpu);

    const D3D12_RESOURCE_STATES prior = dxTex->getCurrentState();
    ensureState(dxTex, D3D12_RESOURCE_STATE_RENDER_TARGET);

    std::shared_ptr<DX12RHIPipeline> clearPipeline = device_->getOrCreateClearPipeline();
    bindPipelineInternal(clearPipeline.get(), false);

    const float color[4] = { r, g, b, a };
    pushConstants(RHIShaderStage::Fragment, 0, sizeof(color), color);

    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = static_cast<float>(dxTex->getWidth());
    viewport.Height = static_cast<float>(dxTex->getHeight());
    viewport.MaxDepth = 1.0f;
    cmdList_->RSSetViewports(1, &viewport);

    D3D12_RECT scissor = { 0, 0, static_cast<LONG>(dxTex->getWidth()),
                           static_cast<LONG>(dxTex->getHeight()) };
    cmdList_->RSSetScissorRects(1, &scissor);

    cmdList_->OMSetRenderTargets(1, &rtvCpu, FALSE, nullptr);
    cmdList_->DrawInstanced(3, 1, 0, 0);

    ensureState(dxTex, prior);
}
