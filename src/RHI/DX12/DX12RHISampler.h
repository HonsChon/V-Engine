#pragma once

#include "RHISampler.h"
#include "DX12TypeConversions.h"

class DX12RHIDevice;

class DX12RHISampler : public RHISampler
{
public:
    DX12RHIDevice*     device_;
    RHISamplerDesc     desc_;

    explicit DX12RHISampler(DX12RHIDevice* device) : device_(device) {}
    DX12RHISampler(DX12RHIDevice* device, const RHISamplerDesc& desc)
        : device_(device), desc_(desc) {}
    ~DX12RHISampler() override = default;

    const RHISamplerDesc& getDesc() const { return desc_; }

    /// D3D12 sampler description (written into shader-visible sampler heaps on use).
    D3D12_SAMPLER_DESC getD3D12SamplerDesc() const {
        D3D12_SAMPLER_DESC out = {};
        DX12TypeConversions::toD3D12SamplerDesc(desc_, out);
        return out;
    }
};
