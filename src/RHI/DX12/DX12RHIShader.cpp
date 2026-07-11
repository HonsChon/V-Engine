#include "DX12RHIShader.h"

#include <stdexcept>
#include <d3dcompiler.h>

DX12RHIShader::DX12RHIShader(DX12RHIDevice* device, RHIShaderStage stage,
                             const std::string& filePath,
                             const std::string& entryPoint)
    : device_(device), stage_(stage), entryPoint_(entryPoint)
{
    std::wstring widePath(filePath.begin(), filePath.end());
    if (FAILED(D3DReadFileToBlob(widePath.c_str(), &shaderBlob_))) {
        throw std::runtime_error("[DX12RHIShader] Failed to load shader blob: " + filePath);
    }
}

D3D12_SHADER_BYTECODE DX12RHIShader::getBytecode() const {
    D3D12_SHADER_BYTECODE bytecode = {};
    bytecode.pShaderBytecode = shaderBlob_->GetBufferPointer();
    bytecode.BytecodeLength = shaderBlob_->GetBufferSize();
    return bytecode;
}
