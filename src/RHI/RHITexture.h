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
    /// 0 = 自动完整 mip 链(floor(log2(max(w,h))) + 1,createTexture 时解析);
    /// 1 = 单层;>1 = 显式层数。uploadPixels 上传 mip 0 后,若 mipLevels > 1
    /// 自动生成整个链(双后端实现,纹理最终处于 ShaderReadOnly)。
    uint32_t        mipLevels   = 1;
    uint32_t        arrayLayers = 1;
    RHIFormat       format      = RHIFormat::R8G8B8A8_UNORM;
    RHITextureUsage usage       = RHITextureUsage::Sampled;
    RHISampleCount  samples     = RHISampleCount::Count1;
};

/// 解析 desc.mipLevels == 0 → 完整 mip 链(device::createTexture 入口调用)。
inline void resolveMipLevels(RHITextureDesc& desc) {
    if (desc.mipLevels != 0) return;
    uint32_t largest = (desc.width > desc.height) ? desc.width : desc.height;
    uint32_t levels = 1;
    while (largest > 1) {
        largest >>= 1;
        ++levels;
    }
    desc.mipLevels = levels;
}

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

    /// Upload pixel data to this texture (uses staging buffer + layout transitions).
    /// The texture must have been created with TransferDst usage.
    /// After upload, the texture is left in ShaderReadOnly layout.
    virtual void uploadPixels(const void* data, uint64_t dataSize) { /* no-op for views */ }

    /// Create a view into a single layer of an array texture.
    /// The returned RHITexture is a non-owning view (does not destroy the underlying image).
    /// Used for per-layer framebuffer attachments (e.g., SSAO's 16-layer array).
    virtual std::shared_ptr<RHITexture> createLayerView(uint32_t layer) { return nullptr; }

    // Non-copyable
    RHITexture(const RHITexture&) = delete;
    RHITexture& operator=(const RHITexture&) = delete;

protected:
    RHITexture() = default;
};
