#pragma once

#include "RHIShader.h"

#include <directx/d3d12.h>
#include <wrl/client.h>
#include <string>

class DX12RHIDevice;

class DX12RHIShader : public RHIShader
{
public:
    DX12RHIShader(DX12RHIDevice* device, RHIShaderStage stage,
                  const std::string& filePath,
                  const std::string& entryPoint = "main");
    ~DX12RHIShader() override = default;

    RHIShaderStage getStage() const override { return stage_; }
    const std::string& getEntryPoint() const { return entryPoint_; }

    D3D12_SHADER_BYTECODE getBytecode() const;

private:
    DX12RHIDevice*                      device_;
    RHIShaderStage                      stage_;
    std::string                         entryPoint_;
    Microsoft::WRL::ComPtr<ID3DBlob>    shaderBlob_;
};

