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
    ID3D12Device* d3dDevice = device->getDevice();
    const uint32_t colorCount = dxRP->getColorAttachmentCount();
    const bool hasDepth = dxRP->hasDepthAttachment();

    if (colorCount + (hasDepth ? 1 : 0) > desc.attachments.size()) {
        throw std::runtime_error("[DX12RHIFramebuffer] missing attachments");
    }

    // RTV heap + views (per color attachment; layer views use 2DARRAY descriptors).
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = colorCount;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(d3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap_)))) {
        throw std::runtime_error("[DX12RHIFramebuffer] failed to create RTV descriptor heap");
    }

    const UINT rtvSize = d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = rtvHeap_->GetCPUDescriptorHandleForHeapStart();

    for (uint32_t i = 0; i < colorCount; ++i) {
        auto* texture = static_cast<DX12RHITexture*>(desc.attachments[i]);
        D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = texture->getRTVDesc();
        d3dDevice->CreateRenderTargetView(texture->getD3D12Resource(), &rtvDesc, cpu);

        colorTextures_.push_back(texture);
        rtvHandles_.push_back(cpu);
        cpu.ptr += rtvSize;
    }

    if (hasDepth) {
        auto* depthTexture = static_cast<DX12RHITexture*>(desc.attachments[colorCount]);
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(d3dDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap_)))) {
            throw std::runtime_error("[DX12RHIFramebuffer] failed to create DSV descriptor heap");
        }

        dsvHandle_ = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
        DXGI_FORMAT dsvFormat = toDXGIFormat(dxRP->getDepthFormat());
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = depthTexture->getDSVDesc(dsvFormat);
        d3dDevice->CreateDepthStencilView(depthTexture->getD3D12Resource(), &dsvDesc, dsvHandle_);
        depthTexture_ = depthTexture;
        hasDepth_ = true;
    }
}

DX12RHIFramebuffer::DX12RHIFramebuffer(DX12RHIDevice* device,
                                       uint32_t width, uint32_t height,
                                       std::vector<DX12RHITexture*> colorTextures,
                                       std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvHandles,
                                       DX12RHITexture* depthTexture,
                                       D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
                                       bool hasDepth)
    : width_(width)
    , height_(height)
    , rtvHandles_(std::move(rtvHandles))
    , colorTextures_(std::move(colorTextures))
    , depthTexture_(depthTexture)
    , dsvHandle_(dsvHandle)
    , hasDepth_(hasDepth)
{
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12RHIFramebuffer::getRTVHandle(uint32_t index) const {
    if (index < rtvHandles_.size()) {
        return rtvHandles_[index];
    }
    return {};
}
