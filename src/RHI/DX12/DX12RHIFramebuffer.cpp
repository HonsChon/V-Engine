#include "DX12RHIFramebuffer.h"
#include "DX12RHIDevice.h"
#include "DX12RHITexture.h"
#include "DX12RHIRenderPass.h"
#include "DX12TypeConversions.h"

#include <stdexcept>

using namespace DX12TypeConversions;

DX12RHIFramebuffer::DX12RHIFramebuffer(DX12RHIDevice* device, const RHIFramebufferDesc& desc)
    : width_(desc.width)
    , height_(desc.height)
{
    auto* dxRP = static_cast<DX12RHIRenderPass*>(desc.renderPass);
    colorAttachmentCount_ = dxRP->getColorAttachmentCount();
    hasDepth_ = dxRP->hasDepthAttachment();

    ID3D12Device* d3dDevice = device->getDevice();

    // Create RTV heap
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = colorAttachmentCount_;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(d3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap_)))) {
        throw std::runtime_error("[DX12RHIFramebuffer] Failed to create RTV descriptor heap");
    }

    UINT rtvDescriptorSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE rtvCpuHandle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();

    for (uint32_t i = 0; i < colorAttachmentCount_; ++i) {
        auto* texture = static_cast<DX12RHITexture*>(desc.attachments[i]);
        d3dDevice->CreateRenderTargetView(texture->getD3D12Resource(), nullptr, rtvCpuHandle);
        rtvHandles_.push_back(rtvCpuHandle);
        rtvCpuHandle.ptr += rtvDescriptorSize;
    }

    // Create DSV heap + view
    if (hasDepth_) {
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(d3dDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap_)))) {
            throw std::runtime_error("[DX12RHIFramebuffer] Failed to create DSV descriptor heap");
        }

        dsvHandle_ = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
        auto* texture = static_cast<DX12RHITexture*>(desc.attachments[colorAttachmentCount_]);

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = toDXGIFormat(dxRP->getDepthFormat());
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
        d3dDevice->CreateDepthStencilView(texture->getD3D12Resource(), &dsvDesc, dsvHandle_);
    }
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12RHIFramebuffer::getRTVHandle(uint32_t index) const {
    if (index < rtvHandles_.size()) {
        return rtvHandles_[index];
    }
    return {};
}
