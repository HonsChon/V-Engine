#pragma once

#include "RHISwapChain.h"

// forward declarations
class DX12RHIDevice;

#include <directx/d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <vector>

using Microsoft::WRL::ComPtr;

class DX12RHISwapChain: public RHISwapChain
{
public:
    DX12RHISwapChain(DX12RHIDevice* device, const RHISwapChainDesc& desc);

private:
    DX12RHIDevice*          device = nullptr;
    RHISwapChainDesc        desc;
    ComPtr<IDXGISwapChain3> swapChain;
    uint32_t                frameIndex = 0;
};