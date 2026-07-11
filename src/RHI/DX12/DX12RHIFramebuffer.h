#pragma once

#include "RHIRenderPass.h"

#include <directx/d3d12.h>
#include <wrl/client.h>
#include <vector>

class DX12RHIDevice;

class DX12RHIFramebuffer : public RHIFramebuffer
{
public:
    DX12RHIFramebuffer(DX12RHIDevice* device, const RHIFramebufferDesc& desc);
    ~DX12RHIFramebuffer() override = default;

    uint32_t getWidth() const override  { return width_; }
    uint32_t getHeight() const override { return height_; }

    D3D12_CPU_DESCRIPTOR_HANDLE getRTVHandle(uint32_t index) const;
    D3D12_CPU_DESCRIPTOR_HANDLE getDSVHandle() const { return dsvHandle_; }
    bool                         hasDepthAttachment() const { return hasDepth_; }
    uint32_t                     getColorAttachmentCount() const { return colorAttachmentCount_; }

private:
    uint32_t width_  = 0;
    uint32_t height_ = 0;
    uint32_t colorAttachmentCount_ = 0;
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvHandles_;
    D3D12_CPU_DESCRIPTOR_HANDLE               dsvHandle_ = {};
    bool hasDepth_ = false;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
};

