#pragma once

#include "RHIRenderPass.h"

#include <vector>
#include <directx/d3d12.h>

class DX12RHIRenderPass;
class DX12RHITexture;

class DX12RHIFramebuffer : public RHIFramebuffer
{
public:
    DX12RHIFramebuffer(DX12RHIRenderPass* renderPass,
                       const std::vector<DX12RHITexture*>& attachments,
                       uint32_t width, uint32_t height,
                       D3D12_CPU_DESCRIPTOR_HANDLE* rtvHandles,
                       D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle,
                       bool hasDepth);
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
};

