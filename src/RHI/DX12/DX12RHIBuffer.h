#pragma once

#include "RHIBuffer.h"

class DX12RHIDevice;

class DX12RHIBuffer : public RHIBuffer
{
public:
    DX12RHIBuffer(DirectXRHIDevice* device, RHIBuffre& desc);
    ~DX12RHIBuffer() override;

    void* map() override;
    void* unmap() override;
    void  uploadData(const void* data, uint64_t size, uint64_t offset = 0) override;

    uint64_t       getSize() const override { return desc_.size; }
    RHIBufferUsage getUsage() const override { return desc_.usage; }
    RHIMemoryUsage getMemoryUsage() const override { return desc_.memoryUsage; }

}