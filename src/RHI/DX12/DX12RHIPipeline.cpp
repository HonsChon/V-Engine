#include "DX12RHIPipeline.h"
#include "DX12RHIDevice.h"
#include "DX12RHIShader.h"
#include "DX12RHIDescriptor.h"
#include "DX12RHIRenderPass.h"
#include "DX12TypeConversions.h"

#include <stdexcept>
#include <d3dcompiler.h>

using namespace DX12TypeConversions;

// =============================================================================
// DX12RHIPipeline
// =============================================================================

DX12RHIPipeline::DX12RHIPipeline(DX12RHIDevice* device,
                                 ComPtr<ID3D12PipelineState> pso,
                                 ComPtr<ID3D12RootSignature> rootSig,
                                 RHIPipelineType type)
    : device_(device)
    , pso_(std::move(pso))
    , rootSig_(std::move(rootSig))
    , type_(type)
{
}

// =============================================================================
// Static helpers: RHI → DX12 type conversions for PSO building
// =============================================================================

static D3D12_FILL_MODE toD3D12FillMode(RHIPolygonMode mode) {
    switch (mode) {
        case RHIPolygonMode::Fill:  return D3D12_FILL_MODE_SOLID;
        case RHIPolygonMode::Line:  return D3D12_FILL_MODE_WIREFRAME;
        case RHIPolygonMode::Point: return D3D12_FILL_MODE_WIREFRAME; // not supported; fallback
        default: return D3D12_FILL_MODE_SOLID;
    }
}

static D3D12_DESCRIPTOR_RANGE_TYPE toD3D12RangeType(RHIDescriptorType type) {
    switch (type) {
        case RHIDescriptorType::UniformBuffer:
        case RHIDescriptorType::UniformBufferDynamic:
            return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        case RHIDescriptorType::StorageBuffer:
        case RHIDescriptorType::StorageBufferDynamic:
            return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        case RHIDescriptorType::SampledImage:
        case RHIDescriptorType::CombinedImageSampler:
        case RHIDescriptorType::InputAttachment:
            return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        case RHIDescriptorType::StorageImage:
            return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        case RHIDescriptorType::Sampler:
            return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        default: return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    }
}

static D3D12_SHADER_VISIBILITY toD3D12ShaderVisibility(RHIShaderStage stages) {
    // multi-stage → ALL
    uint32_t s = static_cast<uint32_t>(stages);
    uint32_t vert = static_cast<uint32_t>(RHIShaderStage::Vertex);
    uint32_t frag = static_cast<uint32_t>(RHIShaderStage::Fragment);
    uint32_t comp = static_cast<uint32_t>(RHIShaderStage::Compute);

    if (s == vert)        return D3D12_SHADER_VISIBILITY_VERTEX;
    if (s == frag)        return D3D12_SHADER_VISIBILITY_PIXEL;
    if (s == (vert | frag)) return D3D12_SHADER_VISIBILITY_ALL;
    return D3D12_SHADER_VISIBILITY_ALL;
}

static D3D12_PRIMITIVE_TOPOLOGY_TYPE toD3D12TopoType(RHIPrimitiveTopology topo) {
    switch (topo) {
        case RHIPrimitiveTopology::TriangleList:
        case RHIPrimitiveTopology::TriangleStrip:
        case RHIPrimitiveTopology::TriangleFan:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        case RHIPrimitiveTopology::LineList:
        case RHIPrimitiveTopology::LineStrip:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        case RHIPrimitiveTopology::PointList:
            return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        default: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }
}

static D3D12_INPUT_CLASSIFICATION toD3D12InputClassification(RHIVertexInputRate rate) {
    return (rate == RHIVertexInputRate::Instance)
        ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
        : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
}

static D3D12_RENDER_TARGET_BLEND_DESC toD3D12RenderTargetBlendDesc(const RHIColorBlendAttachment& att) {
    D3D12_RENDER_TARGET_BLEND_DESC desc = {};
    desc.BlendEnable = att.blendEnable ? TRUE : FALSE;
    desc.SrcBlend = toD3D12Blend(att.srcColorFactor);
    desc.DestBlend = toD3D12Blend(att.dstColorFactor);
    desc.BlendOp = toD3D12BlendOp(att.colorBlendOp);
    desc.SrcBlendAlpha = toD3D12Blend(att.srcAlphaFactor);
    desc.DestBlendAlpha = toD3D12Blend(att.dstAlphaFactor);
    desc.BlendOpAlpha = toD3D12BlendOp(att.alphaBlendOp);

    uint32_t mask = static_cast<uint32_t>(att.colorWriteMask);
    desc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL; // simplified
    if (mask == static_cast<uint32_t>(RHIColorComponent::All)) {
        desc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    } else {
        UINT8 m = 0;
        if (mask & static_cast<uint32_t>(RHIColorComponent::R)) m |= D3D12_COLOR_WRITE_ENABLE_RED;
        if (mask & static_cast<uint32_t>(RHIColorComponent::G)) m |= D3D12_COLOR_WRITE_ENABLE_GREEN;
        if (mask & static_cast<uint32_t>(RHIColorComponent::B)) m |= D3D12_COLOR_WRITE_ENABLE_BLUE;
        if (mask & static_cast<uint32_t>(RHIColorComponent::A)) m |= D3D12_COLOR_WRITE_ENABLE_ALPHA;
        desc.RenderTargetWriteMask = m;
    }
    return desc;
}

// =============================================================================
// DX12GraphicsPipelineBuilder
// =============================================================================

DX12GraphicsPipelineBuilder::DX12GraphicsPipelineBuilder(DX12RHIDevice* device)
    : device_(device) {}

RHIGraphicsPipelineBuilder& DX12GraphicsPipelineBuilder::setVertexShader(const std::string& path) {
    vertShaderPath_ = path; return *this;
}
RHIGraphicsPipelineBuilder& DX12GraphicsPipelineBuilder::setFragmentShader(const std::string& path) {
    fragShaderPath_ = path; return *this;
}

RHIGraphicsPipelineBuilder& DX12GraphicsPipelineBuilder::addVertexBinding(
    uint32_t binding, uint32_t stride, RHIVertexInputRate inputRate) {
    vertexBindings_.push_back({ binding, stride, inputRate });
    return *this;
}

RHIGraphicsPipelineBuilder& DX12GraphicsPipelineBuilder::addVertexAttribute(
    uint32_t binding, uint32_t location, RHIFormat format, uint32_t offset) {
    vertexAttributes_.push_back({ binding, location, format, offset });
    return *this;
}

RHIGraphicsPipelineBuilder& DX12GraphicsPipelineBuilder::setTopology(RHIPrimitiveTopology topology) {
    topology_ = topology; return *this;
}

RHIGraphicsPipelineBuilder& DX12GraphicsPipelineBuilder::setCullMode(RHICullMode mode) {
    cullMode_ = mode; return *this;
}
RHIGraphicsPipelineBuilder& DX12GraphicsPipelineBuilder::setFrontFace(RHIFrontFace face) {
    frontFace_ = face; return *this;
}
RHIGraphicsPipelineBuilder& DX12GraphicsPipelineBuilder::setPolygonMode(RHIPolygonMode mode) {
    polygonMode_ = mode; return *this;
}
RHIGraphicsPipelineBuilder& DX12GraphicsPipelineBuilder::setLineWidth(float width) {
    lineWidth_ = width; return *this;
}
RHIGraphicsPipelineBuilder& DX12GraphicsPipelineBuilder::setDepthBias(
    bool enable, float constantFactor, float slopeFactor, float clamp) {
    depthBiasEnable_ = enable;
    depthBiasConstant_ = constantFactor;
    depthBiasSlope_ = slopeFactor;
    depthBiasClamp_ = clamp;
    return *this;
}

RHIGraphicsPipelineBuilder& DX12GraphicsPipelineBuilder::setDepthTest(
    bool enable, bool writeEnable, RHICompareOp compareOp) {
    depthTestEnable_ = enable;
    depthWriteEnable_ = writeEnable;
    depthCompareOp_ = compareOp;
    return *this;
}
RHIGraphicsPipelineBuilder& DX12GraphicsPipelineBuilder::setStencilTest(bool enable) {
    stencilTestEnable_ = enable; return *this;
}

RHIGraphicsPipelineBuilder& DX12GraphicsPipelineBuilder::setSampleCount(RHISampleCount count) {
    sampleCount_ = count; return *this;
}
RHIGraphicsPipelineBuilder& DX12GraphicsPipelineBuilder::addColorBlendAttachment(
    const RHIColorBlendAttachment& attachment) {
    colorBlendAttachments_.push_back(attachment); return *this;
}
RHIGraphicsPipelineBuilder& DX12GraphicsPipelineBuilder::setColorAttachmentCount(uint32_t count) {
    colorAttachmentCount_ = count;
    userSetAttachmentCount_ = true;
    return *this;
}
RHIGraphicsPipelineBuilder& DX12GraphicsPipelineBuilder::addDynamicState(RHIDynamicState state) {
    dynamicStates_.push_back(state); return *this;
}
RHIGraphicsPipelineBuilder& DX12GraphicsPipelineBuilder::addBindingLayout(RHIBindingLayout* layout) {
    bindingLayouts_.push_back(layout); return *this;
}
RHIGraphicsPipelineBuilder& DX12GraphicsPipelineBuilder::addPushConstant(
    RHIShaderStage stages, uint32_t offset, uint32_t size) {
    pushConstantRanges_.push_back({ stages, offset, size }); return *this;
}
RHIGraphicsPipelineBuilder& DX12GraphicsPipelineBuilder::setRenderPass(
    RHIRenderPass* renderPass, uint32_t /*subpass*/) {
    if (renderPass) {
        auto* dxRP = static_cast<DX12RHIRenderPass*>(renderPass);
        for (uint32_t i = 0; i < dxRP->getColorAttachmentCount(); ++i) {
            rtvFormats_.push_back(dxRP->getColorFormat(i));
        }
        hasDepthStencil_ = dxRP->hasDepthAttachment();
        dsvFormat_ = dxRP->getDepthFormat();
    }
    return *this;
}

// ---- Root Signature ----

void DX12GraphicsPipelineBuilder::buildRootSignature(ComPtr<ID3D12RootSignature>& outRootSig) {
    std::vector<D3D12_ROOT_PARAMETER> rootParams;
    std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>> allRanges;

    // Build root parameters from binding layouts
    for (const auto* layout : bindingLayouts_) {
        auto* dxLayout = static_cast<const DX12RHIBindingLayout*>(layout);
        for (const auto& entry : dxLayout->getDesc().entries) {
            D3D12_DESCRIPTOR_RANGE_TYPE rangeType = toD3D12RangeType(entry.type);

            std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
            D3D12_DESCRIPTOR_RANGE range = {};
            range.RangeType = rangeType;
            range.NumDescriptors = entry.count;
            range.BaseShaderRegister = entry.binding;
            range.RegisterSpace = 0;
            range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            ranges.push_back(range);

            D3D12_ROOT_PARAMETER param = {};
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.ShaderVisibility = toD3D12ShaderVisibility(entry.stageFlags);

            allRanges.push_back(std::move(ranges));
            param.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(allRanges.back().size());
            param.DescriptorTable.pDescriptorRanges = allRanges.back().data();

            rootParams.push_back(param);
        }
    }

    // Build root constants from push constants
    std::vector<D3D12_ROOT_PARAMETER> pcParams;
    for (const auto& pc : pushConstantRanges_) {
        D3D12_ROOT_PARAMETER param = {};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        param.ShaderVisibility = toD3D12ShaderVisibility(pc.stageFlags);
        param.Constants.Num32BitValues = pc.size / sizeof(uint32_t);
        param.Constants.ShaderRegister = 0;
        param.Constants.RegisterSpace = 0;
        pcParams.push_back(param);
    }

    // Put push constants after descriptor tables
    for (auto& p : pcParams) {
        rootParams.push_back(p);
    }

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = static_cast<UINT>(rootParams.size());
    rootDesc.pParameters = rootParams.data();
    rootDesc.NumStaticSamplers = 0;
    rootDesc.pStaticSamplers = nullptr;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                             &signature, &error);
    if (FAILED(hr)) {
        std::string errMsg = "[DX12GraphicsPipelineBuilder] Failed to serialize root signature";
        if (error) {
            errMsg += ": " + std::string(static_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize());
        }
        throw std::runtime_error(errMsg);
    }

    hr = device_->getDevice()->CreateRootSignature(0,
        signature->GetBufferPointer(), signature->GetBufferSize(),
        IID_PPV_ARGS(&outRootSig));
    if (FAILED(hr)) {
        throw std::runtime_error("[DX12GraphicsPipelineBuilder] Failed to create root signature");
    }
}

// ---- Graphics Pipeline State ----

void DX12GraphicsPipelineBuilder::buildGraphicsPipelineState(
    ComPtr<ID3D12RootSignature>& rootSig,
    ComPtr<ID3D12PipelineState>& outPSO)
{
    // Shaders
    DX12RHIShader vs(device_, RHIShaderStage::Vertex, vertShaderPath_);
    DX12RHIShader ps(device_, RHIShaderStage::Fragment, fragShaderPath_);

    D3D12_SHADER_BYTECODE vsBytecode = vs.getBytecode();
    D3D12_SHADER_BYTECODE psBytecode = ps.getBytecode();

    // Input layout
    std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
    for (const auto& attr : vertexAttributes_) {
        D3D12_INPUT_ELEMENT_DESC elem = {};
        elem.SemanticName = "TEXCOORD";
        elem.SemanticIndex = attr.location;
        elem.Format = toDXGIFormat(attr.format);
        elem.InputSlot = attr.binding;
        elem.AlignedByteOffset = attr.offset;

        bool isPerInstance = false;
        for (const auto& vb : vertexBindings_) {
            if (vb.binding == attr.binding) {
                isPerInstance = (vb.inputRate == RHIVertexInputRate::Instance);
                break;
            }
        }
        elem.InputSlotClass = isPerInstance ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
                                            : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        elem.InstanceDataStepRate = isPerInstance ? 1u : 0u;
        inputElements.push_back(elem);
    }

    // Rasterizer
    D3D12_RASTERIZER_DESC rasterizer = {};
    rasterizer.FillMode = toD3D12FillMode(polygonMode_);
    rasterizer.CullMode = toD3D12CullMode(cullMode_);
    rasterizer.FrontCounterClockwise = (frontFace_ == RHIFrontFace::CounterClockwise) ? TRUE : FALSE;
    rasterizer.DepthBias = depthBiasEnable_ ? static_cast<INT>(depthBiasConstant_) : 0;
    rasterizer.DepthBiasClamp = depthBiasClamp_;
    rasterizer.SlopeScaledDepthBias = depthBiasSlope_;
    rasterizer.DepthClipEnable = TRUE;
    rasterizer.MultisampleEnable = (static_cast<uint32_t>(sampleCount_) > 1) ? TRUE : FALSE;
    rasterizer.AntialiasedLineEnable = FALSE;
    rasterizer.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    // Depth-stencil
    D3D12_DEPTH_STENCIL_DESC depthStencil = {};
    depthStencil.DepthEnable = depthTestEnable_ ? TRUE : FALSE;
    depthStencil.DepthWriteMask = depthWriteEnable_ ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    depthStencil.DepthFunc = toD3D12CompareFunc(depthCompareOp_);
    depthStencil.StencilEnable = stencilTestEnable_ ? TRUE : FALSE;
    // Stencil state defaults
    depthStencil.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    depthStencil.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;

    // Blend
    if (colorBlendAttachments_.empty()) {
        if (userSetAttachmentCount_) {
            colorBlendAttachments_.resize(colorAttachmentCount_);
        } else {
            RHIColorBlendAttachment defaultAtt;
            defaultAtt.blendEnable = false;
            colorBlendAttachments_.push_back(defaultAtt);
        }
    }

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    blendDesc.RenderTarget[0] = toD3D12RenderTargetBlendDesc(colorBlendAttachments_[0]);

    // Ensure enough RTV formats
    while (rtvFormats_.size() < colorBlendAttachments_.size()) {
        rtvFormats_.push_back(RHIFormat::R8G8B8A8_UNORM);
    }

    // Sample
    DXGI_SAMPLE_DESC sampleDesc = {};
    sampleDesc.Count = static_cast<UINT>(sampleCount_);
    sampleDesc.Quality = 0;

    // PSO desc
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSig.Get();
    psoDesc.VS = vsBytecode;
    psoDesc.PS = psBytecode;
    psoDesc.RasterizerState = rasterizer;
    psoDesc.DepthStencilState = depthStencil;
    psoDesc.BlendState = blendDesc;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = toD3D12TopoType(topology_);
    psoDesc.SampleDesc = sampleDesc;
    psoDesc.InputLayout.NumElements = static_cast<UINT>(inputElements.size());
    psoDesc.InputLayout.pInputElementDescs = inputElements.data();
    psoDesc.NumRenderTargets = static_cast<UINT>(rtvFormats_.size());
    for (size_t i = 0; i < rtvFormats_.size() && i < 8; ++i) {
        psoDesc.RTVFormats[i] = toDXGIFormat(rtvFormats_[i]);
    }
    if (hasDepthStencil_) {
        psoDesc.DSVFormat = toDXGIFormat(dsvFormat_);
    } else {
        psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    }
    psoDesc.NodeMask = 0;
    psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    HRESULT hr = device_->getDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&outPSO));
    if (FAILED(hr)) {
        throw std::runtime_error("[DX12GraphicsPipelineBuilder] Failed to create graphics pipeline state");
    }
}

std::shared_ptr<RHIPipeline> DX12GraphicsPipelineBuilder::build() {
    ComPtr<ID3D12RootSignature> rootSig;
    buildRootSignature(rootSig);

    ComPtr<ID3D12PipelineState> pso;
    buildGraphicsPipelineState(rootSig, pso);

    return std::make_shared<DX12RHIPipeline>(device_, pso, rootSig, RHIPipelineType::Graphics);
}

// =============================================================================
// DX12ComputePipelineBuilder
// =============================================================================

DX12ComputePipelineBuilder::DX12ComputePipelineBuilder(DX12RHIDevice* device)
    : device_(device) {}

RHIComputePipelineBuilder& DX12ComputePipelineBuilder::setComputeShader(const std::string& path) {
    computeShaderPath_ = path; return *this;
}
RHIComputePipelineBuilder& DX12ComputePipelineBuilder::addBindingLayout(RHIBindingLayout* layout) {
    bindingLayouts_.push_back(layout); return *this;
}
RHIComputePipelineBuilder& DX12ComputePipelineBuilder::addPushConstant(
    RHIShaderStage stages, uint32_t offset, uint32_t size) {
    pushConstantRanges_.push_back({ stages, offset, size }); return *this;
}

std::shared_ptr<RHIPipeline> DX12ComputePipelineBuilder::build() {
    // --- Root Signature ---
    std::vector<D3D12_ROOT_PARAMETER> rootParams;
    std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>> allRanges;

    for (const auto* layout : bindingLayouts_) {
        auto* dxLayout = static_cast<const DX12RHIBindingLayout*>(layout);
        for (const auto& entry : dxLayout->getDesc().entries) {
            D3D12_DESCRIPTOR_RANGE_TYPE rangeType = toD3D12RangeType(entry.type);

            std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
            D3D12_DESCRIPTOR_RANGE range = {};
            range.RangeType = rangeType;
            range.NumDescriptors = entry.count;
            range.BaseShaderRegister = entry.binding;
            range.RegisterSpace = 0;
            range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            ranges.push_back(range);

            D3D12_ROOT_PARAMETER param = {};
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            allRanges.push_back(std::move(ranges));
            param.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(allRanges.back().size());
            param.DescriptorTable.pDescriptorRanges = allRanges.back().data();

            rootParams.push_back(param);
        }
    }

    // Push constants as root constants
    for (const auto& pc : pushConstantRanges_) {
        D3D12_ROOT_PARAMETER param = {};
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        param.Constants.Num32BitValues = pc.size / sizeof(uint32_t);
        param.Constants.ShaderRegister = 0;
        param.Constants.RegisterSpace = 0;
        rootParams.push_back(param);
    }

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = static_cast<UINT>(rootParams.size());
    rootDesc.pParameters = rootParams.data();
    rootDesc.NumStaticSamplers = 0;
    rootDesc.pStaticSamplers = nullptr;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                             &signature, &error);
    if (FAILED(hr)) {
        std::string errMsg = "[DX12ComputePipelineBuilder] Failed to serialize root signature";
        if (error) {
            errMsg += ": " + std::string(static_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize());
        }
        throw std::runtime_error(errMsg);
    }

    ComPtr<ID3D12RootSignature> rootSig;
    hr = device_->getDevice()->CreateRootSignature(0,
        signature->GetBufferPointer(), signature->GetBufferSize(),
        IID_PPV_ARGS(&rootSig));
    if (FAILED(hr)) {
        throw std::runtime_error("[DX12ComputePipelineBuilder] Failed to create root signature");
    }

    // --- Compute PSO ---
    DX12RHIShader cs(device_, RHIShaderStage::Compute, computeShaderPath_);
    D3D12_SHADER_BYTECODE csBytecode = cs.getBytecode();

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSig.Get();
    psoDesc.CS = csBytecode;
    psoDesc.NodeMask = 0;
    psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    ComPtr<ID3D12PipelineState> pso;
    hr = device_->getDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pso));
    if (FAILED(hr)) {
        throw std::runtime_error("[DX12ComputePipelineBuilder] Failed to create compute pipeline state");
    }

    return std::make_shared<DX12RHIPipeline>(device_, pso, rootSig, RHIPipelineType::Compute);
}
