#pragma once

#include "RHISwapChain.h"

// forward declarations
class DX12RHIDevice;

#include <directx/d3d12.h>
#include <vector>

class DX12RHIDevice: public RHIDevice
{
public:
    DX12RHIDevice();

}