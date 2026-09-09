#pragma once

#include "RHITexture.h"

#include <directx/d3d12.h>
#include <wrl/client.h>
#include <optional>

class DX12RHIDevice;

/// Owned or wrapped D3D12 texture resource.
class DX12RHITexture : public RHITexture
{
public:
    /// Create a real committed texture resource (state starts at COMMON).
    /// @param optimizedClearValue optional D3D12_CLEAR_VALUE passed to resource
    ///        creation (enables fast clears; must match the format).
    DX12RHITexture(DX12RHIDevice* device, const RHITextureDesc& desc,
                   const D3D12_CLEAR_VALUE* optimizedClearValue = nullptr);

    /// Wrap an externally owned ID3D12Resource (AddRef; used by swapchain back buffers).
    DX12RHITexture(DX12RHIDevice* device,
                   Microsoft::WRL::ComPtr<ID3D12Resource> resource,
                   const RHITextureDesc& desc,
                   D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON);

    /// Wrap constructor used by DX12RHITextureLayerView.
    DX12RHITexture(DX12RHIDevice* device,
                   Microsoft::WRL::ComPtr<ID3D12Resource> resource,
                   const RHITextureDesc& desc,
                   D3D12_RESOURCE_STATES initialState,
                   uint32_t baseLayer);

    ~DX12RHITexture() override = default;

    uint32_t        getWidth() const override       { return width_; }
    uint32_t        getHeight() const override      { return height_; }
    uint32_t        getDepth() const override       { return depth_; }
    uint32_t        getMipLevels() const override   { return mipLevels_; }
    uint32_t        getArrayLayers() const override { return arrayLayers_; }
    RHIFormat       getFormat() const override      { return format_; }
    RHITextureUsage getUsage() const override       { return usage_; }

    /// Upload mip-0 pixel data through a staging buffer. The texture must have
    /// TransferDst usage; after the upload the tracked state is ShaderReadOnly
    /// (matches the RHITexture contract).
    void uploadPixels(const void* data, uint64_t dataSize) override;

    /// Non-owning single-layer view into this (array) texture.
    std::shared_ptr<RHITexture> createLayerView(uint32_t layer) override;

    ID3D12Resource*       getD3D12Resource() const { return resource_.Get(); }
    D3D12_RESOURCE_STATES getCurrentState() const  { return currentState_; }
    void                  setCurrentState(D3D12_RESOURCE_STATES s) { currentState_ = s; }

    bool     isLayerView() const      { return isView_; }
    uint32_t getViewBaseLayer() const { return baseLayer_; }

    /// True when this resource was created typeless because it is a sampled depth texture.
    bool isDepthResourceTypeless() const { return depthTypeless_; }

    // ---- View descriptor builders (shared by framebuffer + descriptor writer) ----
    D3D12_RENDER_TARGET_VIEW_DESC       getRTVDesc() const;
    D3D12_DEPTH_STENCIL_VIEW_DESC       getDSVDesc(DXGI_FORMAT concreteDepthFormat) const;
    D3D12_SHADER_RESOURCE_VIEW_DESC     getSRVDesc() const;
    D3D12_UNORDERED_ACCESS_VIEW_DESC    getUAVDesc() const;

protected:
    DX12RHIDevice*                        device_;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
    D3D12_RESOURCE_STATES                 currentState_ = D3D12_RESOURCE_STATE_COMMON;
    uint32_t width_       = 1;
    uint32_t height_      = 1;
    uint32_t depth_       = 1;
    uint32_t mipLevels_   = 1;
    uint32_t arrayLayers_ = 1;
    RHIFormat       format_ = RHIFormat::R8G8B8A8_UNORM;
    RHITextureUsage usage_  = RHITextureUsage::Sampled;
    bool            depthTypeless_ = false;
    bool            isView_  = false;
    uint32_t        baseLayer_ = 0;
    std::optional<D3D12_CLEAR_VALUE> optimizedClearValue_;
};

/// Non-owning single layer view: shares the parent resource (AddRef'd) and only
/// changes how RTV/DSV/SRV/UAV descriptors address it.
class DX12RHITextureLayerView : public DX12RHITexture
{
public:
    DX12RHITextureLayerView(DX12RHIDevice* device, DX12RHITexture* parent, uint32_t layer);

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> keepAlive_;
};
