#pragma once

#include "DX12TypeConversions.h"
#include "RHIBuffer.h"

#include <directx/d3d12.h>
#include <wrl/client.h>

class DX12RHIDevice;

class DX12RHIBuffer : public RHIBuffer
{
public:
    DX12RHIDevice* device_;
    RHIBufferDesc  desc_;

    /// Create a real committed buffer resource.
    DX12RHIBuffer(DX12RHIDevice* device, const RHIBufferDesc& desc);

    /// Wrap an externally owned ID3D12Resource (no ownership transfer; AddRef).
    DX12RHIBuffer(DX12RHIDevice* device, Microsoft::WRL::ComPtr<ID3D12Resource> resource,
                  uint64_t size, const RHIBufferDesc& desc,
                  D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON);

    ~DX12RHIBuffer() override;

    void* map() override;
    void  unmap() override;
    void  uploadData(const void* data, uint64_t size, uint64_t offset = 0) override;

    uint64_t       getSize() const override { return desc_.size; }
    RHIBufferUsage getUsage() const override { return desc_.usage; }
    RHIMemoryUsage getMemoryUsage() const override { return desc_.memoryUsage; }

    ID3D12Resource*           getD3D12Resource() const { return buffer_.Get(); }
    D3D12_GPU_VIRTUAL_ADDRESS getGPUVirtualAddress() const { return buffer_->GetGPUVirtualAddress(); }
    /// Actual allocated resource size (>= desc_.size; rounded for D3D12 constraints).
    uint64_t                  getResourceSize() const { return resourceSize_; }
    D3D12_RESOURCE_STATES     getCurrentState() const { return currentState_; }
    void                      setCurrentState(D3D12_RESOURCE_STATES state) { currentState_ = state; }

private:
    void createResource(D3D12_HEAP_TYPE heapType, const D3D12_RESOURCE_DESC& resourceDesc,
                        D3D12_RESOURCE_STATES initialState);

    Microsoft::WRL::ComPtr<ID3D12Resource> buffer_;
    uint64_t                               resourceSize_ = 0;
    D3D12_RESOURCE_STATES                  currentState_ = D3D12_RESOURCE_STATE_COMMON;
    void*                                  mapped_ = nullptr;
};
