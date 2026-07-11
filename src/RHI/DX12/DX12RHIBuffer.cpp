#include "DX12RHIBuffer.h"
#include "DX12RHIDevice.h"

#include <stdexcept>
#include <cstring>

using namespace DX12TypeConversions;

DX12RHIBuffer::DX12RHIBuffer(DX12RHIDevice* device, const RHIBufferDesc& desc)
    : device_(device), desc_(desc)
{
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

    switch (desc.memoryUsage) {
        case RHIMemoryUsage::GPUOnly:  currentState_ = D3D12_RESOURCE_STATE_COMMON; break;
        case RHIMemoryUsage::CPUToGPU: currentState_ = D3D12_RESOURCE_STATE_GENERIC_READ; break;
        case RHIMemoryUsage::GPUToCPU: currentState_ = D3D12_RESOURCE_STATE_COPY_DEST; break;
        default: currentState_ = D3D12_RESOURCE_STATE_COMMON; break;
    }

    if (FAILED(device_->getDevice()->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            currentState_,
            nullptr,
            IID_PPV_ARGS(&buffer_))))
    {
        throw std::runtime_error("Failed to create buffer resource");
    }
}

DX12RHIBuffer::~DX12RHIBuffer() {
    if (mapped_) {
        buffer_->Unmap(0, nullptr);
        mapped_ = nullptr;
    }
}

void* DX12RHIBuffer::map() {
    if (mapped_) {
        return mapped_;
    }
    if (FAILED(buffer_->Map(0, nullptr, &mapped_))) {
        throw std::runtime_error("Failed to map buffer");
    }
    return mapped_;
}

void DX12RHIBuffer::unmap() {
    if (mapped_) {
        buffer_->Unmap(0, nullptr);
        mapped_ = nullptr;
    }
}

void DX12RHIBuffer::uploadData(const void* data, uint64_t size, uint64_t offset) {
    void* ptr = map();
    std::memcpy(static_cast<uint8_t*>(ptr) + offset, data, size);
}
