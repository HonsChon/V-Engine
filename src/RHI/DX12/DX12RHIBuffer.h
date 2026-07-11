#pragma once

#include "DX12TypeConversions.h"
#include "RHIBuffer.h"

#include <directx/d3d12.h>
#include <wrl/client.h>

class DX12RHIDevice;

class DX12RHIBuffer : public RHIBuffer
{
public:
    DX12RHIBuffer(DX12RHIDevice* device, const RHIBufferDesc& desc);
    ~DX12RHIBuffer() override;

    void* map() override;
    void  unmap() override;
    void  uploadData(const void* data, uint64_t size, uint64_t offset = 0) override;

    uint64_t       getSize() const override { return desc_.size; }
    RHIBufferUsage getUsage() const override { return desc_.usage; }
    RHIMemoryUsage getMemoryUsage() const override { return desc_.memoryUsage; }

    ID3D12Resource*           getD3D12Resource() const { return buffer_.Get(); }
    D3D12_GPU_VIRTUAL_ADDRESS getGPUVirtualAddress() const { return buffer_->GetGPUVirtualAddress(); }
    D3D12_RESOURCE_STATES      getCurrentState() const { return currentState_; }
    void                       setCurrentState(D3D12_RESOURCE_STATES state) { currentState_ = state; }

private:
    DX12RHIDevice*                        device_;
    RHIBufferDesc                         desc_;
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer_;
    D3D12_RESOURCE_STATES                 currentState_ = D3D12_RESOURCE_STATE_COMMON;
    void*                                 mapped_ = nullptr;
};

