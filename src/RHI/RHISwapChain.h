#pragma once

#include "RHITypes.h"
#include <cstdint>
#include <memory>

// Forward declarations
class RHITexture;
class RHIRenderPass;
class RHIFramebuffer;

// =============================================================================
// RHI SwapChain — Abstract Interface
// =============================================================================

class RHISwapChain {
public:
    virtual ~RHISwapChain() = default;

    virtual RHIFormat    getFormat() const = 0;
    virtual RHIExtent2D  getExtent() const = 0;
    virtual uint32_t     getImageCount() const = 0;

    // Non-copyable
    RHISwapChain(const RHISwapChain&) = delete;
    RHISwapChain& operator=(const RHISwapChain&) = delete;

protected:
    RHISwapChain() = default;
};
