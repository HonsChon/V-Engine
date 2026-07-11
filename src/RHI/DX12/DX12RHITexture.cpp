#include "DX12RHITexture.h"

DX12RHITexture::DX12RHITexture(DX12RHIDevice* device,
                               Microsoft::WRL::ComPtr<ID3D12Resource> resource,
                               const RHITextureDesc& desc,
                               D3D12_RESOURCE_STATES initialState)
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
{
}
