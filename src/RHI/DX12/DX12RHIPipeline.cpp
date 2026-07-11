#include "DX12RHIPipeline.h"

DX12RHIPipeline::DX12RHIPipeline(DX12RHIDevice* device,
                                 Microsoft::WRL::ComPtr<ID3D12PipelineState> pso,
                                 Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig,
                                 RHIPipelineType type)
    : device_(device)
    , pso_(std::move(pso))
    , rootSig_(std::move(rootSig))
    , type_(type)
{
}
