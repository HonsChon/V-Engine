#pragma once

#include "RHIPipeline.h"

#include <directx/d3d12.h>
#include <wrl/client.h>
#include <vector>
#include <string>

using Microsoft::WRL::ComPtr;

class DX12RHIDevice;

// Vertex input semantic convention: spirv-cross HLSL emits TEXCOORD<location>
// for vertex inputs, so the input layout always uses semantic "TEXCOORD" with
// SemanticIndex == attribute location.
constexpr const char* kDX12VertexInputSemantic = "TEXCOORD";

// =============================================================================
// DX12RHIPipeline — ID3D12PipelineState + ID3D12RootSignature + the mapping
// tables that let the command buffer translate (set, push constants) into
// concrete root parameters.
// =============================================================================

class DX12RHIPipeline : public RHIPipeline
{
public:
    /// Per binding layout: indices of the descriptor tables inside the root
    /// signature (-1 when the layout has no such table).
    struct LayoutTables {
        int resourceParam = -1;   // CBV/SRV/UAV table
        int samplerParam  = -1;   // SAMPLER table
    };

    DX12RHIDevice* device_;

    DX12RHIPipeline(DX12RHIDevice* device,
                    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso,
                    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig,
                    RHIPipelineType type,
                    std::vector<LayoutTables> layoutTables,
                    int pushConstantRootParam,
                    std::vector<std::pair<uint32_t, uint32_t>> vertexBindingStrides,
                    RHIPrimitiveTopology topology);
    ~DX12RHIPipeline() override = default;

    RHIPipelineType getType() const override { return type_; }

    ID3D12PipelineState* getD3D12PipelineState() const { return pso_.Get(); }
    ID3D12RootSignature* getD3D12RootSignature() const { return rootSig_.Get(); }

    // ---- Root-signature mapping (used by DX12RHICommandBuffer) ----
    int getTableRootParam(uint32_t set, bool samplerTable) const {
        if (set >= layoutTables_.size()) return -1;
        return samplerTable ? layoutTables_[set].samplerParam
                            : layoutTables_[set].resourceParam;
    }
    int  getPushConstantRootParam() const { return pushConstantRootParam_; }
    bool hasPushConstants() const { return pushConstantRootParam_ >= 0; }

    /// Stride of a vertex input slot (from the input layout); -1 if unknown.
    int  getVertexBindingStride(uint32_t binding) const;
    const std::vector<std::pair<uint32_t, uint32_t>>& getVertexBindingStrides() const {
        return vertexBindingStrides_;
    }
    RHIPrimitiveTopology getPrimitiveTopology() const { return topology_; }

    uint32_t getBindingLayoutCount() const { return static_cast<uint32_t>(layoutTables_.size()); }

private:
    Microsoft::WRL::ComPtr<ID3D12PipelineState>  pso_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature>  rootSig_;
    RHIPipelineType type_ = RHIPipelineType::Graphics;

    std::vector<LayoutTables> layoutTables_;
    int                       pushConstantRootParam_ = -1;
    std::vector<std::pair<uint32_t, uint32_t>> vertexBindingStrides_;
    RHIPrimitiveTopology topology_ = RHIPrimitiveTopology::TriangleList;
};

// =============================================================================
// Shared root-signature build (used by graphics + compute builders)
// =============================================================================

struct DX12RootSignatureResult {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig;
    std::vector<DX12RHIPipeline::LayoutTables>  layoutTables;
    int pushConstantRootParam = -1;

    // Snapshot of what the built root signature covers. Used (Debug builds
    // only) to cross-check DXIL bindings against the C++ root signature, so a
    // register/space mapping drift between shader content and pipeline layout
    // fails loudly at build() time instead of as a runtime validation error.
    struct RangeInfo {
        D3D12_DESCRIPTOR_RANGE_TYPE type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        UINT space = 0;
        UINT baseRegister = 0;
        UINT numDescriptors = 0;
    };
    std::vector<RangeInfo> ranges;        // every descriptor-table range
    bool hasRootConstants = false;        // push-constant parameter present
    UINT pcRegisterSpace  = 0;
    UINT pcNumRegisters   = 0;            // 32-bit-constant coverage in 16-byte cbuffer registers
};

/// Build the root signature for a pipeline.
/// Convention (see docs/DX12-RHI-Notes.md §11 appendix):
///   - binding layout `i` lives in register space `i`
///   - descriptor binding `b` maps to register `b` inside that space
///   - a layout maps to up to two tables: one CBV/SRV/UAV table and one
///     SAMPLER table (D3D12 forbids mixing sampler ranges into a resource table)
///   - push constants become one root 32-bit-constants parameter at
///     register b0 in space `layoutCount`
DX12RootSignatureResult DX12BuildRootSignature(
    DX12RHIDevice* device,
    const std::vector<const RHIBindingLayout*>& bindingLayouts,
    const std::vector<RHIPushConstantRange>& pushConstantRanges,
    bool allowInputAssembler);

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
    void buildGraphicsPipelineState(ID3D12RootSignature* rootSig,
                                    ComPtr<ID3D12PipelineState>& outPSO,
                                    const DX12RootSignatureResult& rootResult);

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
