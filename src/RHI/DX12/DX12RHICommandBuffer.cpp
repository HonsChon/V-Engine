#include "DX12RHICommandBuffer.h"
#include "DX12RHIDevice.h"
#include "DX12RHIBuffer.h"
#include "DX12RHITexture.h"
#include "DX12RHIPipeline.h"
#include "DX12RHIDescriptor.h"
#include "DX12RHIRenderPass.h"
#include "DX12RHIFramebuffer.h"
#include "DX12TypeConversions.h"

#include <stdexcept>

using namespace DX12TypeConversions;

DX12RHICommandBuffer::DX12RHICommandBuffer(DX12RHIDevice* device, ID3D12GraphicsCommandList* cmdList)
    : device_(device), cmdList_(cmdList)
{
}

void DX12RHICommandBuffer::reset(ID3D12GraphicsCommandList* cmdList) {
    cmdList_ = cmdList;
    currentRootSig_ = nullptr;
    isCompute_ = false;
    tempDescriptorsAllocated_ = 0;
}

void DX12RHICommandBuffer::ensureTempCPUDescriptorHeap() {
    if (tempCPUHeap_) return;

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = kMaxTempDescriptors;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    if (FAILED(device_->getDevice()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&tempCPUHeap_)))) {
        throw std::runtime_error("Failed to create temp descriptor heap");
    }
    tempDescriptorSize_ = device_->getDevice()->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12RHICommandBuffer::allocateTempUAV(ID3D12Resource* resource,
                                                                    DXGI_FORMAT format) {
    ensureTempCPUDescriptorHeap();
    if (tempDescriptorsAllocated_ >= kMaxTempDescriptors) {
        throw std::runtime_error("Temp descriptor heap exhausted");
    }

    D3D12_CPU_DESCRIPTOR_HANDLE handle = tempCPUHeap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(tempDescriptorsAllocated_) * tempDescriptorSize_;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = format;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = static_cast<UINT>(
        resource->GetDesc().Width / std::max(1u, static_cast<UINT>(sizeof(uint32_t))));
    uavDesc.Buffer.StructureByteStride = 0;
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    device_->getDevice()->CreateUnorderedAccessView(resource, nullptr, &uavDesc, handle);

    ++tempDescriptorsAllocated_;
    return handle;
}

// ---- RenderPass ----

void DX12RHICommandBuffer::beginRenderPass(RHIRenderPass* renderPass,
                                            RHIFramebuffer* framebuffer,
                                            const std::vector<RHIClearValue>& clearValues) {
    auto* dxFB = static_cast<DX12RHIFramebuffer*>(framebuffer);

    UINT numRTVs = dxFB->getColorAttachmentCount();
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[8] = {};
    for (UINT i = 0; i < numRTVs && i < 8; ++i) {
        rtvHandles[i] = dxFB->getRTVHandle(i);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = {};
    D3D12_CPU_DESCRIPTOR_HANDLE* pDSV = nullptr;
    if (dxFB->hasDepthAttachment()) {
        dsvHandle = dxFB->getDSVHandle();
        pDSV = &dsvHandle;
    }

    cmdList_->OMSetRenderTargets(numRTVs, rtvHandles, FALSE, pDSV);

    for (const auto& cv : clearValues) {
        if (cv.type == RHIClearValue::Type::Color) {
            float color[4] = { cv.color.r, cv.color.g, cv.color.b, cv.color.a };
            for (UINT i = 0; i < numRTVs && i < 8; ++i) {
                cmdList_->ClearRenderTargetView(rtvHandles[i], color, 0, nullptr);
            }
        }
        else if (cv.type == RHIClearValue::Type::DepthStencil) {
            if (dxFB->hasDepthAttachment()) {
                cmdList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH,
                                                cv.depthStencil.depth, cv.depthStencil.stencil,
                                                0, nullptr);
            }
        }
    }
}

void DX12RHICommandBuffer::endRenderPass() {
}

// ---- Pipeline Binding ----

void DX12RHICommandBuffer::bindGraphicsPipeline(RHIPipeline* pipeline) {
    auto* dxPipeline = static_cast<DX12RHIPipeline*>(pipeline);
    cmdList_->SetPipelineState(dxPipeline->getD3D12PipelineState());
    cmdList_->SetGraphicsRootSignature(dxPipeline->getD3D12RootSignature());
    cmdList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    currentRootSig_ = dxPipeline->getD3D12RootSignature();
    isCompute_ = false;
}

void DX12RHICommandBuffer::bindComputePipeline(RHIPipeline* pipeline) {
    auto* dxPipeline = static_cast<DX12RHIPipeline*>(pipeline);
    cmdList_->SetPipelineState(dxPipeline->getD3D12PipelineState());
    cmdList_->SetComputeRootSignature(dxPipeline->getD3D12RootSignature());
    currentRootSig_ = dxPipeline->getD3D12RootSignature();
    isCompute_ = true;
}

// ---- Descriptor / Binding Group ----

void DX12RHICommandBuffer::setBindingGroup(uint32_t set, RHIBindingGroup* group) {
    auto* dxGroup = static_cast<DX12RHIBindingGroup*>(group);
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = dxGroup->getGPUDescriptorHandle();

    if (isCompute_) {
        cmdList_->SetComputeRootDescriptorTable(set, gpuHandle);
    } else {
        cmdList_->SetGraphicsRootDescriptorTable(set, gpuHandle);
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

// ---- Vertex / Index Buffer Binding ----

void DX12RHICommandBuffer::bindVertexBuffer(uint32_t binding, RHIBuffer* buffer, uint64_t offset) {
    auto* dxBuf = static_cast<DX12RHIBuffer*>(buffer);
    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    vbv.BufferLocation = dxBuf->getGPUVirtualAddress() + offset;
    vbv.SizeInBytes = static_cast<UINT>(dxBuf->getSize() - offset);
    vbv.StrideInBytes = 0; // stride is set by pipeline's input layout
    cmdList_->IASetVertexBuffers(binding, 1, &vbv);
}

void DX12RHICommandBuffer::bindIndexBuffer(RHIBuffer* buffer, uint64_t offset,
                                            RHIIndexType indexType) {
    auto* dxBuf = static_cast<DX12RHIBuffer*>(buffer);
    D3D12_INDEX_BUFFER_VIEW ibv = {};
    ibv.BufferLocation = dxBuf->getGPUVirtualAddress() + offset;
    ibv.SizeInBytes = static_cast<UINT>(dxBuf->getSize() - offset);
    ibv.Format = toD3D12IndexFormat(indexType);
    cmdList_->IASetIndexBuffer(&ibv);
}

// ---- Draw Commands ----

void DX12RHICommandBuffer::draw(uint32_t vertexCount, uint32_t instanceCount,
                                 uint32_t firstVertex, uint32_t firstInstance) {
    cmdList_->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
}

void DX12RHICommandBuffer::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                        uint32_t firstIndex, int32_t vertexOffset,
                                        uint32_t firstInstance) {
    cmdList_->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void DX12RHICommandBuffer::drawIndexedIndirect(RHIBuffer* buffer, uint64_t offset,
                                                uint32_t drawCount, uint32_t stride) {
    auto* dxBuf = static_cast<DX12RHIBuffer*>(buffer);

    ID3D12CommandSignature* cmdSig = nullptr;
    if (currentRootSig_) {
        // get command signature from bound pipeline — for now, pipeline info is not tracked separately.
        // The command signature must be pre-created; if not available, skip.
    }

    if (cmdSig) {
        cmdList_->ExecuteIndirect(cmdSig, drawCount,
                                  dxBuf->getD3D12Resource(), offset,
                                  nullptr, 0);
    } else {
        // Fallback: treat as regular indirect args buffer (DrawIndexedInstancedIndirect style)
        // This requires a default command signature created at device level
    }
}

// ---- Compute Commands ----

void DX12RHICommandBuffer::dispatch(uint32_t groupCountX, uint32_t groupCountY,
                                     uint32_t groupCountZ) {
    cmdList_->Dispatch(groupCountX, groupCountY, groupCountZ);
}

void DX12RHICommandBuffer::dispatchIndirect(RHIBuffer* buffer, uint64_t offset) {
    auto* dxBuf = static_cast<DX12RHIBuffer*>(buffer);

    ID3D12CommandSignature* cmdSig = nullptr;
    if (currentRootSig_) {
        // same as drawIndexedIndirect
    }

    if (cmdSig) {
        cmdList_->ExecuteIndirect(cmdSig, 1,
                                  dxBuf->getD3D12Resource(), offset,
                                  nullptr, 0);
    }
}

// ---- Push Constants ----

void DX12RHICommandBuffer::pushConstants(RHIShaderStage /*stages*/, uint32_t offset,
                                          uint32_t size, const void* data) {
    UINT numValues = size / sizeof(uint32_t);
    UINT destOffset = offset / sizeof(uint32_t);

    if (isCompute_) {
        cmdList_->SetComputeRoot32BitConstants(0, numValues, data, destOffset);
    } else {
        cmdList_->SetGraphicsRoot32BitConstants(0, numValues, data, destOffset);
    }
}

// ---- Barriers / Transitions ----

void DX12RHICommandBuffer::pipelineBarrier(RHIPipelineStage /*srcStage*/, RHIPipelineStage /*dstStage*/,
                                            RHIAccessFlags /*srcAccess*/, RHIAccessFlags /*dstAccess*/) {
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
    auto* dxTex = static_cast<DX12RHITexture*>(texture);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = dxTex->getD3D12Resource();
    barrier.Transition.StateBefore = toD3D12ResourceStates(oldLayout);
    barrier.Transition.StateAfter = toD3D12ResourceStates(newLayout);
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    cmdList_->ResourceBarrier(1, &barrier);

    dxTex->setCurrentState(barrier.Transition.StateAfter);
}

// ---- Transfer ----

void DX12RHICommandBuffer::copyBuffer(RHIBuffer* src, RHIBuffer* dst, uint64_t size,
                                       uint64_t srcOffset, uint64_t dstOffset) {
    auto* dxSrc = static_cast<DX12RHIBuffer*>(src);
    auto* dxDst = static_cast<DX12RHIBuffer*>(dst);

    cmdList_->CopyBufferRegion(dxDst->getD3D12Resource(), dstOffset,
                               dxSrc->getD3D12Resource(), srcOffset,
                               size);
}

void DX12RHICommandBuffer::fillBuffer(RHIBuffer* buffer, uint64_t offset, uint64_t /*size*/, uint32_t data) {
    auto* dxBuf = static_cast<DX12RHIBuffer*>(buffer);
    ID3D12Resource* resource = dxBuf->getD3D12Resource();

    ensureTempCPUDescriptorHeap();
    tempDescriptorsAllocated_ = 0;

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = allocateTempUAV(resource, DXGI_FORMAT_R32_UINT);
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = {};
    gpuHandle.ptr = tempCPUHeap_->GetGPUDescriptorHandleForHeapStart().ptr;

    ID3D12DescriptorHeap* heaps[] = { tempCPUHeap_.Get() };
    cmdList_->SetDescriptorHeaps(1, heaps);

    UINT values[4] = { data, data, data, data };
    cmdList_->ClearUnorderedAccessViewUint(gpuHandle, cpuHandle, resource, values, 0, nullptr);
}

void DX12RHICommandBuffer::blitImage(RHITexture* src, RHIImageLayout /*srcLayout*/,
                                      RHITexture* dst, RHIImageLayout /*dstLayout*/,
                                      uint32_t srcWidth, uint32_t srcHeight,
                                      uint32_t dstWidth, uint32_t dstHeight,
                                      RHIFilter /*filter*/) {
    auto* dxSrc = static_cast<DX12RHITexture*>(src);
    auto* dxDst = static_cast<DX12RHITexture*>(dst);

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource = dxSrc->getD3D12Resource();
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    srcLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = dxDst->getD3D12Resource();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;

    D3D12_BOX srcBox = {};
    srcBox.left = 0;
    srcBox.top = 0;
    srcBox.front = 0;
    srcBox.right = srcWidth;
    srcBox.bottom = srcHeight;
    srcBox.back = 1;

    cmdList_->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, &srcBox);
}

// ---- Buffer Barrier ----

void DX12RHICommandBuffer::bufferBarrier(RHIBuffer* buffer, uint64_t /*size*/,
                                          RHIPipelineStage /*srcStage*/, RHIPipelineStage /*dstStage*/,
                                          RHIAccessFlags srcAccess, RHIAccessFlags dstAccess) {
    auto* dxBuf = static_cast<DX12RHIBuffer*>(buffer);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = dxBuf->getD3D12Resource();
    barrier.Transition.StateBefore = toD3D12BufferStates(srcAccess);
    barrier.Transition.StateAfter = toD3D12BufferStates(dstAccess);
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    cmdList_->ResourceBarrier(1, &barrier);

    dxBuf->setCurrentState(barrier.Transition.StateAfter);
}

void DX12RHICommandBuffer::clearColorImage(RHITexture* texture, float r, float g, float b, float a) {
    auto* dxTex = static_cast<DX12RHITexture*>(texture);
    ID3D12Resource* resource = dxTex->getD3D12Resource();

    DXGI_FORMAT format = toDXGIFormat(dxTex->getFormat());
    if (format == DXGI_FORMAT_UNKNOWN) {
        format = DXGI_FORMAT_R8G8B8A8_UNORM;
    }

    ensureTempCPUDescriptorHeap();
    tempDescriptorsAllocated_ = 0;

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = tempCPUHeap_->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = tempCPUHeap_->GetGPUDescriptorHandleForHeapStart();

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = format;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;
    uavDesc.Texture2D.PlaneSlice = 0;

    device_->getDevice()->CreateUnorderedAccessView(resource, nullptr, &uavDesc, cpuHandle);

    ID3D12DescriptorHeap* heaps[] = { tempCPUHeap_.Get() };
    cmdList_->SetDescriptorHeaps(1, heaps);

    float color[4] = { r, g, b, a };
    cmdList_->ClearUnorderedAccessViewFloat(gpuHandle, cpuHandle, resource, color, 0, nullptr);
}
