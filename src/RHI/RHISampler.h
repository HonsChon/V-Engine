#pragma once

#include "RHITypes.h"
#include <memory>

// =============================================================================
// RHI Sampler Description
// =============================================================================

struct RHISamplerDesc {
    RHIFilter      minFilter    = RHIFilter::Linear;
    RHIFilter      magFilter    = RHIFilter::Linear;
    RHIFilter      mipMapFilter = RHIFilter::Linear;
    RHIAddressMode addressModeU = RHIAddressMode::Repeat;
    RHIAddressMode addressModeV = RHIAddressMode::Repeat;
    RHIAddressMode addressModeW = RHIAddressMode::Repeat;
    float          maxAnisotropy = 1.0f;   // 0 or 1 = disabled, >1 = enabled
    bool           anisotropyEnable = false;
    bool           compareEnable = false;
    RHICompareOp   compareOp    = RHICompareOp::Always;
    float          minLod       = 0.0f;
    float          maxLod       = 12.0f;
    float          mipLodBias   = 0.0f;
};

// =============================================================================
// RHI Sampler — Abstract Interface
// =============================================================================

class RHISampler {
public:
    virtual ~RHISampler() = default;

    // Non-copyable
    RHISampler(const RHISampler&) = delete;
    RHISampler& operator=(const RHISampler&) = delete;

protected:
    RHISampler() = default;
};
