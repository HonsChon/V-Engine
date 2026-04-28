#pragma once

#include "RHITypes.h"
#include <cstdint>
#include <cstddef>
#include <memory>

// =============================================================================
// RHI Buffer Description
// =============================================================================

struct RHIBufferDesc {
    uint64_t       size        = 0;
    RHIBufferUsage usage       = RHIBufferUsage::None;
    RHIMemoryUsage memoryUsage = RHIMemoryUsage::GPUOnly;
};

// =============================================================================
// RHI Buffer — Abstract Interface
// =============================================================================

class RHIBuffer {
public:
    virtual ~RHIBuffer() = default;

    // Map / Unmap for CPU-visible buffers
    virtual void* map() = 0;
    virtual void  unmap() = 0;

    // Convenience: upload data. For CPU-visible buffers, maps and copies.
    // For GPU-only buffers, uses an internal staging buffer.
    virtual void uploadData(const void* data, uint64_t size, uint64_t offset = 0) = 0;

    // Getters
    virtual uint64_t       getSize() const = 0;
    virtual RHIBufferUsage getUsage() const = 0;
    virtual RHIMemoryUsage getMemoryUsage() const = 0;

    // Non-copyable
    RHIBuffer(const RHIBuffer&) = delete;
    RHIBuffer& operator=(const RHIBuffer&) = delete;

protected:
    RHIBuffer() = default;
};
