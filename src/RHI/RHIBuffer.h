#pragma once

#include "RHITypes.h"
#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>

// =============================================================================
// RHI Buffer Description
// =============================================================================

struct RHIBufferDesc {
    uint64_t       size        = 0;
    RHIBufferUsage usage       = RHIBufferUsage::None;
    RHIMemoryUsage memoryUsage = RHIMemoryUsage::GPUOnly;

    // Structured buffer stride (0 = not structured)
    uint32_t       structStride = 0;

    // Format for typed buffer views (e.g., R32_UINT for index buffer SRV)
    RHIFormat      format      = RHIFormat::Undefined;

    // Initial resource state (for backends that require explicit state tracking like DX12)
    RHIImageLayout initialState = RHIImageLayout::Undefined;

    // Debug name (useful for RenderDoc / PIX / validation layer)
    std::string    debugName;

    // --- Builder-style setters for convenience ---
    RHIBufferDesc& setSize(uint64_t value)          { size = value; return *this; }
    RHIBufferDesc& setUsage(RHIBufferUsage value)   { usage = value; return *this; }
    RHIBufferDesc& setMemoryUsage(RHIMemoryUsage v) { memoryUsage = v; return *this; }
    RHIBufferDesc& setStructStride(uint32_t value)  { structStride = value; return *this; }
    RHIBufferDesc& setFormat(RHIFormat value)        { format = value; return *this; }
    RHIBufferDesc& setDebugName(const std::string& name) { debugName = name; return *this; }
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
