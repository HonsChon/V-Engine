#pragma once

#include "RHISampler.h"

class DX12RHIDevice;

class DX12RHISampler : public RHISampler
{
public:
    explicit DX12RHISampler(DX12RHIDevice* device) : device_(device) {}
    ~DX12RHISampler() override = default;

private:
    DX12RHIDevice* device_;
};

