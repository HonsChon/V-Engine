#pragma once

#include "RHIRenderPass.h"

#include <directx/d3d12.h>
#include <wrl/client.h>
#include <vector>

class DX12RHIDevice;
class DX12RHITexture;

class DX12RHIFramebuffer : public RHIFramebuffer
{
public:
    /// Owning constructor: creates its own RTV/DSV CPU heaps from the attachments.
    /// Attachment textures are referenced non-owning (the caller keeps them alive).
    DX12RHIFramebuffer(DX12RHIDevice* device, const RHIFramebufferDesc& desc);

    /// Non-owning constructor (swapchain path): the caller keeps the descriptor
    /// heaps and textures alive for the framebuffer's lifetime.
    DX12RHIFramebuffer(DX12RHIDevice* device,
                       uint32_t width, uint32_t height,
                       std::vector<DX12RHITexture*> colorTextures,
                       std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvHandles,
                       DX12RHITexture* depthTexture,
                       D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
                       bool hasDepth);

    ~DX12RHIFramebuffer() override = default;

    uint32_t getWidth() const override  { return width_; }
    uint32_t getHeight() const override { return height_; }

    D3D12_CPU_DESCRIPTOR_HANDLE getRTVHandle(uint32_t index) const;
    D3D12_CPU_DESCRIPTOR_HANDLE getDSVHandle() const { return dsvHandle_; }
    bool                         hasDepthAttachment() const { return hasDepth_; }
    uint32_t                     getColorAttachmentCount() const { return static_cast<uint32_t>(rtvHandles_.size()); }

    // Textures backing the attachments (used for state transitions around render passes).
    DX12RHITexture* getColorTexture(uint32_t index) const {
        return (index < colorTextures_.size()) ? colorTextures_[index] : nullptr;
    }
    DX12RHITexture* getDepthTexture() const { return depthTexture_; }

private:
    uint32_t width_  = 0;
    uint32_t height_ = 0;
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvHandles_;
    std::vector<DX12RHITexture*> colorTextures_;
    DX12RHITexture*              depthTexture_ = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE  dsvHandle_ = {};
    bool hasDepth_ = false;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;   // owned only in the owning ctor
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
};
