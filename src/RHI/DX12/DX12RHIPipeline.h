#pragma once

#include "RHIPipeline.h"

#include <directx/d3d12.h>
#include <wrl/client.h>

class DX12RHIDevice;

class DX12RHIPipeline : public RHIPipeline
{
public:
    DX12RHIPipeline(DX12RHIDevice* device,
                    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso,
                    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig,
                    RHIPipelineType type);
    ~DX12RHIPipeline() override = default;

    RHIPipelineType getType() const override { return type_; }

    ID3D12PipelineState*	getD3D12PipelineState() const { return pso_.Get(); }
    ID3D12RootSignature*     getD3D12RootSignature() const { return rootSig_.Get(); }
    ID3D12CommandSignature*  getDrawCommandSignature() const { return drawCmdSig_.Get(); }
    ID3D12CommandSignature*  getDispatchCommandSignature() const { return dispatchCmdSig_.Get(); }

    void setDrawCommandSignature(Microsoft::WRL::ComPtr<ID3D12CommandSignature> sig) { drawCmdSig_ = sig; }
    void setDispatchCommandSignature(Microsoft::WRL::ComPtr<ID3D12CommandSignature> sig) { dispatchCmdSig_ = sig; }

private:
    DX12RHIDevice*                            device_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState>  pso_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature>  rootSig_;
    Microsoft::WRL::ComPtr<ID3D12CommandSignature> drawCmdSig_;
    Microsoft::WRL::ComPtr<ID3D12CommandSignature> dispatchCmdSig_;
    RHIPipelineType type_ = RHIPipelineType::Graphics;
};

