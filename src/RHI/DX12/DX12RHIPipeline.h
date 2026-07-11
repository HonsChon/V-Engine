#pragma once

#include "RHIPipeline.h"

#include <directx/d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <string>

class DX12RHIDevice;

// =============================================================================
// DX12RHIPipeline — wraps ID3D12PipelineState + ID3D12RootSignature
// =============================================================================

class DX12RHIPipeline : public RHIPipeline
{
public:
    DX12RHIPipeline(DX12RHIDevice* device,
                    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso,
                    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig,
                    RHIPipelineType type);
    ~DX12RHIPipeline() override = default;

    RHIPipelineType getType() const override { return type_; }

    ID3D12PipelineState*  getD3D12PipelineState() const { return pso_.Get(); }
    ID3D12RootSignature*  getD3D12RootSignature() const { return rootSig_.Get(); }
    ID3D12CommandSignature* getDrawCommandSignature() const { return drawCmdSig_.Get(); }
    ID3D12CommandSignature* getDispatchCommandSignature() const { return dispatchCmdSig_.Get(); }

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

// =============================================================================
// DX12GraphicsPipelineBuilder
// =============================================================================

class DX12GraphicsPipelineBuilder : public RHIGraphicsPipelineBuilder
{
public:
    explicit DX12GraphicsPipelineBuilder(DX12RHIDevice* device);
    ~DX12GraphicsPipelineBuilder() override = default;

    RHIGraphicsPipelineBuilder& setVertexShader(const std::string& path) override;
    RHIGraphicsPipelineBuilder& setFragmentShader(const std::string& path) override;

    RHIGraphicsPipelineBuilder& addVertexBinding(uint32_t binding, uint32_t stride,
                                                  RHIVertexInputRate inputRate) override;
    RHIGraphicsPipelineBuilder& addVertexAttribute(uint32_t binding, uint32_t location,
                                                    RHIFormat format, uint32_t offset) override;

    RHIGraphicsPipelineBuilder& setTopology(RHIPrimitiveTopology topology) override;

    RHIGraphicsPipelineBuilder& setCullMode(RHICullMode mode) override;
    RHIGraphicsPipelineBuilder& setFrontFace(RHIFrontFace face) override;
    RHIGraphicsPipelineBuilder& setPolygonMode(RHIPolygonMode mode) override;
    RHIGraphicsPipelineBuilder& setLineWidth(float width) override;
    RHIGraphicsPipelineBuilder& setDepthBias(bool enable, float constantFactor,
                                              float slopeFactor, float clamp) override;

    RHIGraphicsPipelineBuilder& setDepthTest(bool enable, bool writeEnable,
                                              RHICompareOp compareOp) override;
    RHIGraphicsPipelineBuilder& setStencilTest(bool enable) override;

    RHIGraphicsPipelineBuilder& setSampleCount(RHISampleCount count) override;
    RHIGraphicsPipelineBuilder& addColorBlendAttachment(const RHIColorBlendAttachment& attachment) override;
    RHIGraphicsPipelineBuilder& setColorAttachmentCount(uint32_t count) override;
    RHIGraphicsPipelineBuilder& addDynamicState(RHIDynamicState state) override;
    RHIGraphicsPipelineBuilder& addBindingLayout(RHIBindingLayout* layout) override;
    RHIGraphicsPipelineBuilder& addPushConstant(RHIShaderStage stages, uint32_t offset, uint32_t size) override;
    RHIGraphicsPipelineBuilder& setRenderPass(RHIRenderPass* renderPass, uint32_t subpass) override;

    std::shared_ptr<RHIPipeline> build() override;

private:
    void buildRootSignature(ComPtr<ID3D12RootSignature>& outRootSig);
    void buildGraphicsPipelineState(ComPtr<ID3D12RootSignature>& rootSig,
                                     ComPtr<ID3D12PipelineState>& outPSO);

    DX12RHIDevice* device_;

    std::string vertShaderPath_;
    std::string fragShaderPath_;

    struct VertexBinding {
        uint32_t binding;
        uint32_t stride;
        RHIVertexInputRate inputRate;
    };
    struct VertexAttribute {
        uint32_t binding;
        uint32_t location;
        RHIFormat format;
        uint32_t offset;
    };
    std::vector<VertexBinding>   vertexBindings_;
    std::vector<VertexAttribute> vertexAttributes_;

    RHIPrimitiveTopology topology_ = RHIPrimitiveTopology::TriangleList;

    RHICullMode      cullMode_      = RHICullMode::Back;
    RHIFrontFace     frontFace_     = RHIFrontFace::CounterClockwise;
    RHIPolygonMode   polygonMode_   = RHIPolygonMode::Fill;
    float            lineWidth_     = 1.0f;
    bool             depthBiasEnable_   = false;
    float            depthBiasConstant_  = 0.0f;
    float            depthBiasSlope_     = 0.0f;
    float            depthBiasClamp_     = 0.0f;

    bool         depthTestEnable_  = true;
    bool         depthWriteEnable_ = true;
    RHICompareOp depthCompareOp_   = RHICompareOp::Less;
    bool         stencilTestEnable_ = false;

    RHISampleCount sampleCount_ = RHISampleCount::Count1;

    std::vector<RHIColorBlendAttachment> colorBlendAttachments_;
    uint32_t colorAttachmentCount_ = 1;
    bool     userSetAttachmentCount_ = false;

    std::vector<RHIDynamicState>   dynamicStates_;

    std::vector<const RHIBindingLayout*> bindingLayouts_;
    std::vector<RHIPushConstantRange>    pushConstantRanges_;

    std::vector<RHIFormat>  rtvFormats_;
    RHIFormat               dsvFormat_     = RHIFormat::Undefined;
    bool                    hasDepthStencil_ = false;
};

// =============================================================================
// DX12ComputePipelineBuilder
// =============================================================================

class DX12ComputePipelineBuilder : public RHIComputePipelineBuilder
{
public:
    explicit DX12ComputePipelineBuilder(DX12RHIDevice* device);
    ~DX12ComputePipelineBuilder() override = default;

    RHIComputePipelineBuilder& setComputeShader(const std::string& path) override;
    RHIComputePipelineBuilder& addBindingLayout(RHIBindingLayout* layout) override;
    RHIComputePipelineBuilder& addPushConstant(RHIShaderStage stages, uint32_t offset, uint32_t size) override;

    std::shared_ptr<RHIPipeline> build() override;

private:
    DX12RHIDevice* device_;
    std::string computeShaderPath_;
    std::vector<const RHIBindingLayout*> bindingLayouts_;
    std::vector<RHIPushConstantRange>    pushConstantRanges_;
};
