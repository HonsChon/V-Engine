#include "DX12RHIBindingGroup.h"

DX12RHIBindingGroup::DX12RHIBindingGroup(DX12RHIDevice* device, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle)
    : device_(device), gpuHandle_(gpuHandle)
{
}

void DX12RHIBindingGroup::updateBuffer(uint32_t /*binding*/, RHIBuffer* /*buffer*/,
                                        uint64_t /*offset*/, uint64_t /*range*/)
{
}

void DX12RHIBindingGroup::updateTexture(uint32_t /*binding*/, RHITexture* /*texture*/,
                                         RHISampler* /*sampler*/)
{
}
