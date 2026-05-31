#include "DX12RHIBuffer.h"

DX12RHIBuffer::DX12RHIBuffer(DirectXRHIDevice *device, RHIBuffre &desc):device_(device),desc_(desc)
{
    // comfirm usage of buffer
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = toD3D12HeapType(desc.memoryUsage);

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = desc.size;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = toD3D12ResourceFlags(desc.usage);

    D3D12_RESOURCE_STATES initialState;
    switch (desc.memoryUsage) {
        case RHIMemoryUsage::GPUOnly:  initialState = D3D12_RESOURCE_STATE_COMMON; break;
        case RHIMemoryUsage::CPUToGPU: initialState = D3D12_RESOURCE_STATE_GENERIC_READ; break;
        case RHIMemoryUsage::GPUToCPU: initialState = D3D12_RESOURCE_STATE_COPY_DEST; break;
        default: initialState = D3D12_RESOURCE_STATE_COMMON; break;
    }

    if(FAILED(device_->getDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        initialState,
        nullptr,
        IID_PPV_ARGS(&buffer_))))
    {
        throw std::runtime_error("Failed to create buffer resource");
    }


}

void *DX12RHIBuffer::map()
{
    if (mapped_)
    {
        return;
    }
}