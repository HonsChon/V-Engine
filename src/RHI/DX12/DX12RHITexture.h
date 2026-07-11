#pragma once

#include "RHITexture.h"

#include <directx/d3d12.h>
#include <wrl/client.h>

class DX12RHIDevice;

class DX12RHITexture : public RHITexture
{
public:
    DX12RHITexture(DX12RHIDevice* device,
                   Microsoft::WRL::ComPtr<ID3D12Resource> resource,
                   const RHITextureDesc& desc,
                   D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON);
    ~DX12RHITexture() override = default;

    uint32_t        getWidth() const override       { return width_; }
    uint32_t        getHeight() const override      { return height_; }
    uint32_t        getDepth() const override       { return depth_; }
    uint32_t        getMipLevels() const override   { return mipLevels_; }
    uint32_t        getArrayLayers() const override { return arrayLayers_; }
    RHIFormat       getFormat() const override      { return format_; }
    RHITextureUsage getUsage() const override       { return usage_; }

    ID3D12Resource*       getD3D12Resource() const { return resource_.Get(); }
    D3D12_RESOURCE_STATES getCurrentState() const  { return currentState_; }
    void                  setCurrentState(D3D12_RESOURCE_STATES s) { currentState_ = s; }

private:
    DX12RHIDevice*                        device_;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    D3D12_RESOURCE_STATES                 currentState_ = D3D12_RESOURCE_STATE_COMMON;
    uint32_t width_       = 1;
    uint32_t height_      = 1;
    uint32_t depth_       = 1;
    uint32_t mipLevels_   = 1;
    uint32_t arrayLayers_ = 1;
    RHIFormat       format_ = RHIFormat::R8G8B8A8_UNORM;
    RHITextureUsage usage_  = RHITextureUsage::Sampled;
};

