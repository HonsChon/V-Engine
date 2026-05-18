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

/// Result of acquireNextImage / present operations
enum class RHISwapChainResult {
    Success,
    Suboptimal,
    OutOfDate,
    Error
};

// =============================================================================
// RHI SwapChain Description
// =============================================================================

enum class RHIPresentMode {
    Immediate,    // No VSync, tearing allowed (DX12: allow tearing, Vulkan: MAILBOX/IMMEDIATE)
    VSync,        // VSync enabled (DX12: normal present, Vulkan: FIFO)
};

struct RHISwapChainDesc {
    uint32_t       width       = 0;
    uint32_t       height      = 0;
    RHIFormat      format      = RHIFormat::R8G8B8A8_UNORM;
    uint32_t       bufferCount = 2;       // Double buffering (2) or triple buffering (3)
    RHIPresentMode presentMode = RHIPresentMode::VSync;

    RHISwapChainDesc& setWidth(uint32_t w)              { width = w; return *this; }
    RHISwapChainDesc& setHeight(uint32_t h)             { height = h; return *this; }
    RHISwapChainDesc& setFormat(RHIFormat f)            { format = f; return *this; }
    RHISwapChainDesc& setBufferCount(uint32_t c)        { bufferCount = c; return *this; }
    RHISwapChainDesc& setPresentMode(RHIPresentMode m)  { presentMode = m; return *this; }
};

class RHISwapChain {
public:
    virtual ~RHISwapChain() = default;

    // ---- Query ----
    virtual RHIFormat    getFormat() const = 0;
    virtual RHIExtent2D  getExtent() const = 0;
    virtual uint32_t     getImageCount() const = 0;

    // ---- Frame lifecycle ----
    /// Acquire the next image index. Signals the provided native semaphore.
    /// @param signalSemaphore  Native semaphore handle (e.g. VkSemaphore cast to void*)
    /// @param outImageIndex    Output: index of the acquired image
    virtual RHISwapChainResult acquireNextImage(void* signalSemaphore, uint32_t* outImageIndex) = 0;

    /// Present the rendered image. Waits on the provided native semaphore.
    /// @param waitSemaphore  Native semaphore to wait on before presenting
    /// @param imageIndex     Index of the image to present
    virtual RHISwapChainResult present(void* waitSemaphore, uint32_t imageIndex) = 0;

    /// Recreate the swap chain (e.g. after window resize)
    virtual void recreate(uint32_t width, uint32_t height) = 0;

    // ---- RHI accessors ----
    /// Get the presentation render pass as an RHI object
    virtual RHIRenderPass* getRHIRenderPass() const = 0;

    /// Get the framebuffer for a given swapchain image as an RHI object
    virtual RHIFramebuffer* getRHIFramebuffer(uint32_t imageIndex) const = 0;

    // ---- Native handle access (backend internals) ----
    /// Get native render pass handle (e.g. VkRenderPass cast to void*)
    virtual void* getNativeRenderPass() const = 0;

    /// Get native framebuffer handle for a given image index
    virtual void* getNativeFramebuffer(uint32_t imageIndex) const = 0;

    // Non-copyable
    RHISwapChain(const RHISwapChain&) = delete;
    RHISwapChain& operator=(const RHISwapChain&) = delete;

protected:
    RHISwapChain() = default;
};