#include "DX12RHIBuffer.h"
#include "DX12RHIDevice.h"

#include <stdexcept>
#include <cstring>
#include <algorithm>

using namespace DX12TypeConversions;

static uint64_t roundUpTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

DX12RHIBuffer::DX12RHIBuffer(DX12RHIDevice* device, const RHIBufferDesc& desc)
    : device_(device), desc_(desc)
{
    // D3D12 CBVs must be 256-byte aligned; round the allocation so any buffer
    // may later be viewed as a constant buffer / SRV / UAV with legal sizes.
    resourceSize_ = roundUpTo(std::max<uint64_t>(desc.size, 1), 256);
    desc_.size = resourceSize_;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = toD3D12HeapType(desc_.memoryUsage);

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = resourceSize_;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = toD3D12ResourceFlags(desc_.usage);

    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
    switch (desc_.memoryUsage) {
        case RHIMemoryUsage::CPUToGPU: initialState = D3D12_RESOURCE_STATE_GENERIC_READ; break;
        case RHIMemoryUsage::GPUToCPU: initialState = D3D12_RESOURCE_STATE_COPY_DEST;    break;
        default:                       initialState = D3D12_RESOURCE_STATE_COMMON;       break;
    }
    currentState_ = initialState;

    createResource(heapProps.Type, resourceDesc, initialState);
}

DX12RHIBuffer::DX12RHIBuffer(DX12RHIDevice* device,
                             Microsoft::WRL::ComPtr<ID3D12Resource> resource,
                             uint64_t size,
                             const RHIBufferDesc& desc,
                             D3D12_RESOURCE_STATES initialState)
    : device_(device)
    , desc_(desc)
    , buffer_(std::move(resource))
    , resourceSize_(size)
    , currentState_(initialState)
{
    desc_.size = size;
}

DX12RHIBuffer::~DX12RHIBuffer() {
    if (mapped_) {
        buffer_->Unmap(0, nullptr);
        mapped_ = nullptr;
    }
}

void DX12RHIBuffer::createResource(D3D12_HEAP_TYPE heapType,
                                   const D3D12_RESOURCE_DESC& resourceDesc,
                                   D3D12_RESOURCE_STATES initialState) {
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = heapType;

    if (FAILED(device_->getDevice()->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            initialState,
            nullptr,
            IID_PPV_ARGS(&buffer_))))
    {
        throw std::runtime_error("[DX12RHIBuffer] failed to create buffer resource");
    }
}

void* DX12RHIBuffer::map() {
    if (mapped_) {
        return mapped_;
    }
    // Only CPU-accessible heaps may be mapped (GPUOnly = DEFAULT heap must use uploadData).
    if (desc_.memoryUsage == RHIMemoryUsage::GPUOnly) {
        throw std::runtime_error("[DX12RHIBuffer] map() is illegal on GPUOnly (DEFAULT heap) buffers; use uploadData()");
    }
    if (FAILED(buffer_->Map(0, nullptr, &mapped_))) {
        throw std::runtime_error("[DX12RHIBuffer] failed to map buffer");
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
    if (desc_.memoryUsage == RHIMemoryUsage::GPUOnly) {
        // Staging path: host-visible upload buffer -> GPU copy (single-time commands).
        if (offset + size > desc_.size) {
            throw std::runtime_error("[DX12RHIBuffer] uploadData range out of bounds");
        }

        D3D12_HEAP_PROPERTIES uploadHeap = {};
        uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC stagingDesc = {};
        stagingDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        stagingDesc.Width = roundUpTo(size, 256);
        stagingDesc.Height = 1;
        stagingDesc.DepthOrArraySize = 1;
        stagingDesc.MipLevels = 1;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        Microsoft::WRL::ComPtr<ID3D12Resource> staging;
        if (FAILED(device_->getDevice()->CreateCommittedResource(
                &uploadHeap, D3D12_HEAP_FLAG_NONE, &stagingDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&staging)))) {
            throw std::runtime_error("[DX12RHIBuffer] failed to create staging buffer");
        }

        void* ptr = nullptr;
        if (FAILED(staging->Map(0, nullptr, &ptr))) {
            throw std::runtime_error("[DX12RHIBuffer] failed to map staging buffer");
        }
        std::memcpy(ptr, data, size);
        staging->Unmap(0, nullptr);

        // Transition this buffer to COPY_DEST for the copy, then restore its prior state.
        D3D12_RESOURCE_STATES priorState = currentState_;
        ID3D12GraphicsCommandList* cmd = static_cast<ID3D12GraphicsCommandList*>(
            device_->beginSingleTimeCommands());
        {
            if (currentState_ != D3D12_RESOURCE_STATE_COPY_DEST) {
                D3D12_RESOURCE_BARRIER barrier = {};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = buffer_.Get();
                barrier.Transition.StateBefore = currentState_;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                cmd->ResourceBarrier(1, &barrier);
                currentState_ = D3D12_RESOURCE_STATE_COPY_DEST;
            }
            cmd->CopyBufferRegion(buffer_.Get(), offset, staging.Get(), 0, size);
            if (currentState_ != priorState) {
                D3D12_RESOURCE_BARRIER barrier = {};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = buffer_.Get();
                barrier.Transition.StateBefore = currentState_;
                barrier.Transition.StateAfter = priorState;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                cmd->ResourceBarrier(1, &barrier);
                currentState_ = priorState;
            }
        }
        device_->endSingleTimeCommands(cmd);
        return;
    }

    // CPU-visible heaps: map + copy directly.
    void* ptr = map();
    std::memcpy(static_cast<uint8_t*>(ptr) + offset, data, size);
    unmap();
}
