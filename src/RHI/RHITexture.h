#pragma once

#include "RHITypes.h"
#include <cstdint>
#include <memory>
#include <string>

// =============================================================================
// RHI Texture Description
// =============================================================================

struct RHITextureDesc {
    uint32_t        width       = 1;
    uint32_t        height      = 1;
    uint32_t        depth       = 1;
    uint32_t        mipLevels   = 1;
    uint32_t        arrayLayers = 1;
    RHIFormat       format      = RHIFormat::R8G8B8A8_UNORM;
    RHITextureUsage usage       = RHITextureUsage::Sampled;
    RHISampleCount  samples     = RHISampleCount::Count1;
};

// =============================================================================
// RHI Texture — Abstract Interface
// =============================================================================

class RHITexture {
public:
    virtual ~RHITexture() = default;

    virtual uint32_t        getWidth() const = 0;
    virtual uint32_t        getHeight() const = 0;
    virtual uint32_t        getDepth() const = 0;
    virtual uint32_t        getMipLevels() const = 0;
    virtual uint32_t        getArrayLayers() const = 0;
    virtual RHIFormat       getFormat() const = 0;
    virtual RHITextureUsage getUsage() const = 0;

    // Non-copyable
    RHITexture(const RHITexture&) = delete;
    RHITexture& operator=(const RHITexture&) = delete;

protected:
    RHITexture() = default;
};
