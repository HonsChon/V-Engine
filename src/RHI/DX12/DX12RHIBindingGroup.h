#pragma once

#include "RHIDescriptor.h"

#include <directx/d3d12.h>

class DX12RHIDevice;

class DX12RHIBindingGroup : public RHIBindingGroup
{
public:
    DX12RHIBindingGroup(DX12RHIDevice* device, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);
    ~DX12RHIBindingGroup() override = default;

    void updateBuffer(uint32_t binding, RHIBuffer* buffer,
                      uint64_t offset = 0, uint64_t range = 0) override;
    void updateTexture(uint32_t binding, RHITexture* texture,
                       RHISampler* sampler = nullptr) override;

    D3D12_GPU_DESCRIPTOR_HANDLE getGPUDescriptorHandle() const { return gpuHandle_; }

private:
    DX12RHIDevice*            device_;
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle_ = {};
};

