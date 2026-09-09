#include "DX12RHITexture.h"
#include "DX12RHIDevice.h"
#include "DX12TypeConversions.h"

#include <stdexcept>
#include <cstring>

using namespace DX12TypeConversions;

namespace {

uint32_t bytesPerPixel(RHIFormat format) {
    switch (format) {
        case RHIFormat::R8_UNORM:         return 1;
        case RHIFormat::R8G8_UNORM:       return 2;
        case RHIFormat::R8G8B8A8_UNORM:
        case RHIFormat::R8G8B8A8_SRGB:
        case RHIFormat::B8G8R8A8_UNORM:
        case RHIFormat::B8G8R8A8_SRGB:
        case RHIFormat::R32_SFLOAT:
        case RHIFormat::R32_UINT:         return 4;
        case RHIFormat::R16_SFLOAT:       return 2;
        case RHIFormat::R16G16_SFLOAT:    return 4;
        case RHIFormat::R16G16B16A16_SFLOAT: return 8;
        default: return 4;
    }
}

} // namespace

DX12RHITexture::DX12RHITexture(DX12RHIDevice* device, const RHITextureDesc& desc,
                               const D3D12_CLEAR_VALUE* optimizedClearValue)
    : device_(device)
    , width_(desc.width)
    , height_(desc.height)
    , depth_(desc.depth)
    , mipLevels_(desc.mipLevels)
    , arrayLayers_(desc.arrayLayers)
    , format_(desc.format)
    , usage_(desc.usage)
{
    if (optimizedClearValue) {
        optimizedClearValue_ = *optimizedClearValue;
    }
    // Sampled/stored depth textures must be created typeless so an SRV/UAV can
    // share the resource with the depth/stencil view.
    depthTypeless_ = isDepthFormat(format_)
        && (hasFlag(usage_, RHITextureUsage::Sampled) || hasFlag(usage_, RHITextureUsage::Storage));

    DXGI_FORMAT resourceFormat = depthTypeless_ ? toDXGIResourceFormat(format_) : toDXGIFormat(format_);

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = (desc.depth > 1) ? D3D12_RESOURCE_DIMENSION_TEXTURE3D
                                              : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Width = width_;
    resourceDesc.Height = height_;
    resourceDesc.DepthOrArraySize = (desc.depth > 1) ? desc.depth : desc.arrayLayers;
    resourceDesc.MipLevels = mipLevels_;
    resourceDesc.Format = resourceFormat;
    resourceDesc.SampleDesc.Count = static_cast<UINT>(desc.samples);
    resourceDesc.Flags = toD3D12TextureFlags(usage_);
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
    if (FAILED(device_->getDevice()->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            initialState,
            optimizedClearValue_.has_value() ? &optimizedClearValue_.value() : nullptr,
            IID_PPV_ARGS(&resource_))))
    {
        throw std::runtime_error("[DX12RHITexture] failed to create texture resource");
    }
    currentState_ = initialState;
}

DX12RHITexture::DX12RHITexture(DX12RHIDevice* device,
                               Microsoft::WRL::ComPtr<ID3D12Resource> resource,
                               const RHITextureDesc& desc,
                               D3D12_RESOURCE_STATES initialState)
    : DX12RHITexture(device, std::move(resource), desc, initialState, 0)
{
}

DX12RHITexture::DX12RHITexture(DX12RHIDevice* device,
                               Microsoft::WRL::ComPtr<ID3D12Resource> resource,
                               const RHITextureDesc& desc,
                               D3D12_RESOURCE_STATES initialState,
                               uint32_t baseLayer)
    : device_(device)
    , resource_(std::move(resource))
    , currentState_(initialState)
    , width_(desc.width)
    , height_(desc.height)
    , depth_(desc.depth)
    , mipLevels_(desc.mipLevels)
    , arrayLayers_(desc.arrayLayers)
    , format_(desc.format)
    , usage_(desc.usage)
    , baseLayer_(baseLayer)
{
    depthTypeless_ = isDepthFormat(format_)
        && (hasFlag(usage_, RHITextureUsage::Sampled) || hasFlag(usage_, RHITextureUsage::Storage));
}

// -----------------------------------------------------------------------------
// uploadPixels
// -----------------------------------------------------------------------------

void DX12RHITexture::uploadPixels(const void* data, uint64_t dataSize) {
    if (!hasFlag(usage_, RHITextureUsage::TransferDst)) {
        throw std::runtime_error("[DX12RHITexture] uploadPixels requires TransferDst usage");
    }
    if (mipLevels_ != 1) {
        throw std::runtime_error("[DX12RHITexture] uploadPixels: only single-mip textures are supported");
    }

    const uint32_t bpp = bytesPerPixel(format_);
    const uint64_t bytesPerRow = static_cast<uint64_t>(width_) * bpp;
    const uint64_t rowPitch = (bytesPerRow + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
                            / D3D12_TEXTURE_DATA_PITCH_ALIGNMENT * D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    const uint64_t totalBytes = rowPitch * height_ * depth_ * arrayLayers_;

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC stagingDesc = {};
    stagingDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    stagingDesc.Width = totalBytes;
    stagingDesc.Height = 1;
    stagingDesc.DepthOrArraySize = 1;
    stagingDesc.MipLevels = 1;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    Microsoft::WRL::ComPtr<ID3D12Resource> staging;
    if (FAILED(device_->getDevice()->CreateCommittedResource(
            &uploadHeap, D3D12_HEAP_FLAG_NONE, &stagingDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&staging)))) {
        throw std::runtime_error("[DX12RHITexture] failed to create upload staging buffer");
    }

    // Repack tightly packed rows into the 256-byte aligned row pitch.
    void* mapped = nullptr;
    if (FAILED(staging->Map(0, nullptr, &mapped))) {
        throw std::runtime_error("[DX12RHITexture] failed to map upload staging buffer");
    }
    {
        const uint8_t* src = static_cast<const uint8_t*>(data);
        uint8_t* dst = static_cast<uint8_t*>(mapped);
        const uint64_t layerBytes = rowPitch * height_ * depth_;
        for (uint32_t layer = 0; layer < arrayLayers_; ++layer) {
            for (uint64_t row = 0; row < height_ * depth_; ++row) {
                std::memcpy(dst + layer * layerBytes + row * rowPitch,
                            src + layer * height_ * depth_ * bytesPerRow + row * bytesPerRow,
                            bytesPerRow);
            }
        }
    }
    staging->Unmap(0, nullptr);

    ID3D12GraphicsCommandList* cmd = static_cast<ID3D12GraphicsCommandList*>(
        device_->beginSingleTimeCommands());
    {
        if (currentState_ != D3D12_RESOURCE_STATE_COPY_DEST) {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = resource_.Get();
            barrier.Transition.StateBefore = currentState_;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmd->ResourceBarrier(1, &barrier);
            currentState_ = D3D12_RESOURCE_STATE_COPY_DEST;
        }

        D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
        dstLoc.pResource = resource_.Get();
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
        srcLoc.pResource = staging.Get();
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint.Offset = 0;
        srcLoc.PlacedFootprint.Footprint.Width = width_;
        srcLoc.PlacedFootprint.Footprint.Height = height_;
        srcLoc.PlacedFootprint.Footprint.Depth = depth_;
        srcLoc.PlacedFootprint.Footprint.RowPitch = rowPitch;
        srcLoc.PlacedFootprint.Footprint.Format = toDXGIFormat(format_);

        // Copy all array layers (mip 0); the footprint covers one layer.
        for (uint32_t layer = 0; layer < arrayLayers_; ++layer) {
            dstLoc.SubresourceIndex = layer;
            srcLoc.PlacedFootprint.Offset = layer * rowPitch * height_ * depth_;
            cmd->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);
        }

        // Contract: after upload the texture is in ShaderReadOnly.
        const D3D12_RESOURCE_STATES shaderRead =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        if (currentState_ != shaderRead) {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = resource_.Get();
            barrier.Transition.StateBefore = currentState_;
            barrier.Transition.StateAfter = shaderRead;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            cmd->ResourceBarrier(1, &barrier);
            currentState_ = shaderRead;
        }
    }
    device_->endSingleTimeCommands(cmd);
}

std::shared_ptr<RHITexture> DX12RHITexture::createLayerView(uint32_t layer) {
    if (layer >= arrayLayers_) {
        return nullptr;
    }
    return std::make_shared<DX12RHITextureLayerView>(device_, this, layer);
}

// -----------------------------------------------------------------------------
// View descriptor builders
// -----------------------------------------------------------------------------

namespace {

bool useArrayDim(const DX12RHITexture& tex) {
    // Layer views and array textures both use the 2DARRAY view dimension.
    return tex.isLayerView() || tex.getArrayLayers() > 1;
}

} // namespace

D3D12_RENDER_TARGET_VIEW_DESC DX12RHITexture::getRTVDesc() const {
    D3D12_RENDER_TARGET_VIEW_DESC desc = {};
    desc.Format = toDXGIFormat(format_);
    if (useArrayDim(*this)) {
        desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray.FirstArraySlice = baseLayer_;
        desc.Texture2DArray.ArraySize = isView_ ? 1 : arrayLayers_;
        desc.Texture2DArray.MipSlice = 0;
        desc.Texture2DArray.PlaneSlice = 0;
    } else {
        desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipSlice = 0;
        desc.Texture2D.PlaneSlice = 0;
    }
    return desc;
}

D3D12_DEPTH_STENCIL_VIEW_DESC DX12RHITexture::getDSVDesc(DXGI_FORMAT concreteDepthFormat) const {
    D3D12_DEPTH_STENCIL_VIEW_DESC desc = {};
    desc.Format = concreteDepthFormat;
    desc.Flags = D3D12_DSV_FLAG_NONE;
    if (useArrayDim(*this)) {
        desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray.FirstArraySlice = baseLayer_;
        desc.Texture2DArray.ArraySize = isView_ ? 1 : arrayLayers_;
        desc.Texture2DArray.MipSlice = 0;
    } else {
        desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipSlice = 0;
    }
    return desc;
}

D3D12_SHADER_RESOURCE_VIEW_DESC DX12RHITexture::getSRVDesc() const {
    D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
    desc.Format = isDepthFormat(format_)
        ? toDXGISRVFormat(format_)          // depth texels read as float color
        : toDXGIFormat(format_);
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (useArrayDim(*this)) {
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray.FirstArraySlice = baseLayer_;
        desc.Texture2DArray.ArraySize = isView_ ? 1 : arrayLayers_;
        desc.Texture2DArray.MostDetailedMip = 0;
        desc.Texture2DArray.MipLevels = mipLevels_;
        desc.Texture2DArray.PlaneSlice = 0;
        desc.Texture2DArray.ResourceMinLODClamp = 0;
    } else {
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MostDetailedMip = 0;
        desc.Texture2D.MipLevels = mipLevels_;
        desc.Texture2D.PlaneSlice = 0;
        desc.Texture2D.ResourceMinLODClamp = 0;
    }
    return desc;
}

D3D12_UNORDERED_ACCESS_VIEW_DESC DX12RHITexture::getUAVDesc() const {
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
    desc.Format = isDepthFormat(format_)
        ? toDXGISRVFormat(format_)
        : toDXGIFormat(format_);
    if (useArrayDim(*this)) {
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray.FirstArraySlice = baseLayer_;
        desc.Texture2DArray.ArraySize = isView_ ? 1 : arrayLayers_;
        desc.Texture2DArray.MipSlice = 0;
        desc.Texture2DArray.PlaneSlice = 0;
    } else {
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipSlice = 0;
        desc.Texture2D.PlaneSlice = 0;
    }
    return desc;
}

// -----------------------------------------------------------------------------
// DX12RHITextureLayerView
// -----------------------------------------------------------------------------

DX12RHITextureLayerView::DX12RHITextureLayerView(DX12RHIDevice* device,
                                                 DX12RHITexture* parent,
                                                 uint32_t layer)
    : DX12RHITexture(device,
                     Microsoft::WRL::ComPtr<ID3D12Resource>(parent->getD3D12Resource()),
                     RHITextureDesc{},   // fields set below
                     parent->getCurrentState(),
                     layer)
{
    // Rebuild info from the parent.
    width_       = parent->getWidth();
    height_      = parent->getHeight();
    depth_       = parent->getDepth();
    mipLevels_   = parent->getMipLevels();
    arrayLayers_ = 1;               // the view addresses exactly one layer
    format_      = parent->getFormat();
    usage_       = parent->getUsage();
    depthTypeless_ = parent->isDepthResourceTypeless();
    isView_      = true;
}
