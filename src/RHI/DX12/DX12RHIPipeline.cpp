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
                                 Microsoft::WRL::ComPtr<ID3D12PipelineState> pso,
                                 Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig,
                                 RHIPipelineType type,
                                 std::vector<LayoutTables> layoutTables,
                                 int pushConstantRootParam,
                                 std::vector<std::pair<uint32_t, uint32_t>> vertexBindingStrides,
                                 RHIPrimitiveTopology topology)
    : device_(device)
    , pso_(std::move(pso))
    , rootSig_(std::move(rootSig))
    , type_(type)
    , layoutTables_(std::move(layoutTables))
    , pushConstantRootParam_(pushConstantRootParam)
    , vertexBindingStrides_(std::move(vertexBindingStrides))
    , topology_(topology)
{
}

int DX12RHIPipeline::getVertexBindingStride(uint32_t binding) const {
    for (const auto& slot : vertexBindingStrides_) {
        if (slot.first == binding) {
            return static_cast<int>(slot.second);
        }
    }
    return -1;
}

// =============================================================================
// Static helpers: RHI -> DX12 type conversions for PSO building
// =============================================================================

static D3D12_FILL_MODE toD3D12FillMode(RHIPolygonMode mode) {
    switch (mode) {
        case RHIPolygonMode::Fill:  return D3D12_FILL_MODE_SOLID;
        case RHIPolygonMode::Line:
        case RHIPolygonMode::Point: return D3D12_FILL_MODE_WIREFRAME; // Point not supported; closest legal fallback
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
        case RHIDescriptorType::StorageImage:
            return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        case RHIDescriptorType::SampledImage:
        case RHIDescriptorType::CombinedImageSampler:
        case RHIDescriptorType::InputAttachment:
            return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        case RHIDescriptorType::Sampler:
            return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        default: return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    }
}

static D3D12_SHADER_VISIBILITY toD3D12ShaderVisibility(RHIShaderStage stages) {
    const uint32_t s = static_cast<uint32_t>(stages);
    const uint32_t vert = static_cast<uint32_t>(RHIShaderStage::Vertex);
    const uint32_t frag = static_cast<uint32_t>(RHIShaderStage::Fragment);

    if (s == vert)        return D3D12_SHADER_VISIBILITY_VERTEX;
    if (s == frag)        return D3D12_SHADER_VISIBILITY_PIXEL;
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

static D3D12_RENDER_TARGET_BLEND_DESC defaultBlendAttachment() {
    D3D12_RENDER_TARGET_BLEND_DESC desc = {};
    desc.BlendEnable = FALSE;
    desc.LogicOpEnable = FALSE;
    desc.SrcBlend = D3D12_BLEND_ONE;
    desc.DestBlend = D3D12_BLEND_ZERO;
    desc.BlendOp = D3D12_BLEND_OP_ADD;
    desc.SrcBlendAlpha = D3D12_BLEND_ONE;
    desc.DestBlendAlpha = D3D12_BLEND_ZERO;
    desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    desc.LogicOp = D3D12_LOGIC_OP_NOOP;
    desc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    return desc;
}

static D3D12_RENDER_TARGET_BLEND_DESC toD3D12RenderTargetBlendDesc(const RHIColorBlendAttachment& att) {
    D3D12_RENDER_TARGET_BLEND_DESC desc = {};
    desc.BlendEnable = att.blendEnable ? TRUE : FALSE;
    desc.LogicOpEnable = FALSE;
    desc.SrcBlend = toD3D12Blend(att.srcColorFactor);
    desc.DestBlend = toD3D12Blend(att.dstColorFactor);
    desc.BlendOp = toD3D12BlendOp(att.colorBlendOp);
    desc.SrcBlendAlpha = toD3D12Blend(att.srcAlphaFactor);
    desc.DestBlendAlpha = toD3D12Blend(att.dstAlphaFactor);
    desc.BlendOpAlpha = toD3D12BlendOp(att.alphaBlendOp);
    desc.LogicOp = D3D12_LOGIC_OP_NOOP;

    UINT8 m = 0;
    const uint32_t mask = static_cast<uint32_t>(att.colorWriteMask);
    if (mask & static_cast<uint32_t>(RHIColorComponent::R)) m |= D3D12_COLOR_WRITE_ENABLE_RED;
    if (mask & static_cast<uint32_t>(RHIColorComponent::G)) m |= D3D12_COLOR_WRITE_ENABLE_GREEN;
    if (mask & static_cast<uint32_t>(RHIColorComponent::B)) m |= D3D12_COLOR_WRITE_ENABLE_BLUE;
    if (mask & static_cast<uint32_t>(RHIColorComponent::A)) m |= D3D12_COLOR_WRITE_ENABLE_ALPHA;
    desc.RenderTargetWriteMask = m ? m : D3D12_COLOR_WRITE_ENABLE_ALL;
    return desc;
}

// =============================================================================
// Shared root signature builder
// =============================================================================

DX12RootSignatureResult DX12BuildRootSignature(
    DX12RHIDevice* device,
    const std::vector<const RHIBindingLayout*>& bindingLayouts,
    const std::vector<RHIPushConstantRange>& pushConstantRanges,
    bool allowInputAssembler)
{
    DX12RootSignatureResult result;

    // One or two descriptor tables per layout + one 32-bit-constants parameter
    // for the (single) push constant range. A combined image sampler consumes
    // BOTH a resource range and a sampler range (D3D12 forbids mixing sampler
    // ranges into a resource table), so it counts towards two tables.
    std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
    std::vector<D3D12_ROOT_PARAMETER>   rootParams;
    size_t numTables = 0;
    for (const auto* layout : bindingLayouts) {
        const auto& entries = static_cast<const DX12RHIBindingLayout*>(layout)->getDesc().entries;
        bool hasResource = false;
        bool hasSampler = false;
        for (const auto& e : entries) {
            if (e.type == RHIDescriptorType::Sampler ||
                e.type == RHIDescriptorType::CombinedImageSampler) {
                hasSampler = true;
            }
            if (e.type != RHIDescriptorType::Sampler) {
                hasResource = true;   // includes combined image samplers (SRV side)
            }
        }
        if (hasResource) ++numTables;
        if (hasSampler) ++numTables;
    }

    // Count total ranges up-front so pointers remain stable (two-pass fix).
    // A combined-image-sampler consumes two ranges (SRV + SAMPLER).
    size_t totalRanges = 0;
    for (const auto* layout : bindingLayouts) {
        for (const auto& e : static_cast<const DX12RHIBindingLayout*>(layout)->getDesc().entries) {
            if (e.type != RHIDescriptorType::Sampler) ++totalRanges;      // resource range
            if (e.type == RHIDescriptorType::Sampler ||
                e.type == RHIDescriptorType::CombinedImageSampler) {
                ++totalRanges;                                            // sampler range
            }
        }
    }
    ranges.resize(totalRanges);
    rootParams.resize(numTables + (pushConstantRanges.empty() ? 0 : 1));

    const size_t layoutCount = bindingLayouts.size();
    result.layoutTables.resize(layoutCount);
    if (layoutCount > UINT_MAX) {
        throw std::runtime_error("[DX12RootSignature] too many binding layouts");
    }

    size_t rangeIdx = 0;
    size_t paramIdx = 0;
    for (size_t set = 0; set < layoutCount; ++set) {
        const auto* layout = bindingLayouts[set];
        const UINT space = static_cast<UINT>(set);
        const auto& entries = static_cast<const DX12RHIBindingLayout*>(layout)->getDesc().entries;

        // Split entries into resource (CBV/SRV/UAV) and sampler groups, keeping
        // the original order within each group (binding-group blocks are laid out
        // the same way).
        std::vector<const RHIBindingEntry*> resourceEntries;
        std::vector<const RHIBindingEntry*> samplerEntries;
        for (const auto& e : entries) {
            if (e.type == RHIDescriptorType::Sampler ||
                e.type == RHIDescriptorType::CombinedImageSampler) {
                samplerEntries.push_back(&e);
                if (e.type == RHIDescriptorType::Sampler) continue;
            }
            resourceEntries.push_back(&e);
        }

        // Resource table (CBV/SRV/UAV).
        if (!resourceEntries.empty()) {
            D3D12_ROOT_PARAMETER& param = rootParams[paramIdx];
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.ShaderVisibility = toD3D12ShaderVisibility(resourceEntries.front()->stageFlags);
            param.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(resourceEntries.size());
            param.DescriptorTable.pDescriptorRanges = &ranges[rangeIdx];
            for (const auto* e : resourceEntries) {
                D3D12_DESCRIPTOR_RANGE& range = ranges[rangeIdx++];
                range.RangeType = toD3D12RangeType(e->type);
                range.NumDescriptors = e->count;
                range.BaseShaderRegister = e->binding;
                range.RegisterSpace = space;
                range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            }
            result.layoutTables[set].resourceParam = static_cast<int>(paramIdx);
            ++paramIdx;
        }

        // Sampler table (D3D12 requires sampler ranges in their own table).
        if (!samplerEntries.empty()) {
            D3D12_ROOT_PARAMETER& param = rootParams[paramIdx];
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.ShaderVisibility = toD3D12ShaderVisibility(samplerEntries.front()->stageFlags);
            param.DescriptorTable.NumDescriptorRanges = static_cast<UINT>(samplerEntries.size());
            param.DescriptorTable.pDescriptorRanges = &ranges[rangeIdx];
            for (const auto* e : samplerEntries) {
                D3D12_DESCRIPTOR_RANGE& range = ranges[rangeIdx++];
                range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
                range.NumDescriptors = e->count;
                range.BaseShaderRegister = e->binding;
                range.RegisterSpace = space;
                range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
            }
            result.layoutTables[set].samplerParam = static_cast<int>(paramIdx);
            ++paramIdx;
        }
    }

    // Push constants: a single root 32-bit-constants parameter at b0 in a
    // dedicated space (== layout count) to keep it clear of the descriptor spaces.
    if (!pushConstantRanges.empty()) {
        if (pushConstantRanges.size() > 1) {
            throw std::runtime_error("[DX12RootSignature] multiple push constant ranges are not supported yet");
        }
        const RHIPushConstantRange& pc = pushConstantRanges.front();
        if ((pc.offset & 3) != 0 || (pc.size & 3) != 0 || pc.offset != 0) {
            throw std::runtime_error("[DX12RootSignature] push constant range must be 4-byte aligned starting at 0");
        }
        D3D12_ROOT_PARAMETER& param = rootParams[paramIdx];
        param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        param.ShaderVisibility = toD3D12ShaderVisibility(pc.stageFlags);
        param.Constants.ShaderRegister = 0;
        param.Constants.RegisterSpace = static_cast<UINT>(layoutCount);
        param.Constants.Num32BitValues = pc.size / sizeof(uint32_t);
        result.pushConstantRootParam = static_cast<int>(paramIdx);
        ++paramIdx;
    }
    rootParams.resize(paramIdx);
    ranges.resize(rangeIdx);

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = static_cast<UINT>(rootParams.size());
    rootDesc.pParameters = rootParams.empty() ? nullptr : rootParams.data();
    rootDesc.NumStaticSamplers = 0;
    rootDesc.pStaticSamplers = nullptr;
    rootDesc.Flags = allowInputAssembler
        ? D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
        : D3D12_ROOT_SIGNATURE_FLAG_NONE;

    Microsoft::WRL::ComPtr<ID3DBlob> signature;
    Microsoft::WRL::ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                             &signature, &error);
    if (FAILED(hr)) {
        std::string errMsg = "[DX12RootSignature] failed to serialize root signature";
        if (error) {
            errMsg += ": " + std::string(static_cast<const char*>(error->GetBufferPointer()),
                                         error->GetBufferSize());
        }
        throw std::runtime_error(errMsg);
    }

    hr = device->getDevice()->CreateRootSignature(0,
        signature->GetBufferPointer(), signature->GetBufferSize(),
        IID_PPV_ARGS(&result.rootSig));
    if (FAILED(hr)) {
        throw std::runtime_error("[DX12RootSignature] failed to create root signature");
    }
    return result;
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

// ---- Graphics Pipeline State ----

void DX12GraphicsPipelineBuilder::buildGraphicsPipelineState(
    ID3D12RootSignature* rootSig,
    ComPtr<ID3D12PipelineState>& outPSO,
    const DX12RootSignatureResult& rootResult)
{
    // Shaders (compiled blobs loaded through DX12RHIShader).
    DX12RHIShader vs(device_, RHIShaderStage::Vertex, vertShaderPath_);
    DX12RHIShader ps(device_, RHIShaderStage::Fragment, fragShaderPath_);

    D3D12_SHADER_BYTECODE vsBytecode = vs.getBytecode();
    D3D12_SHADER_BYTECODE psBytecode = ps.getBytecode();

    // Input layout (semantic convention: TEXCOORD + location, see header).
    std::vector<D3D12_INPUT_ELEMENT_DESC> inputElements;
    for (const auto& attr : vertexAttributes_) {
        D3D12_INPUT_ELEMENT_DESC elem = {};
        elem.SemanticName = kDX12VertexInputSemantic;
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
    depthStencil.DepthWriteMask = depthWriteEnable_ ? D3D12_DEPTH_WRITE_MASK_ALL
                                                    : D3D12_DEPTH_WRITE_MASK_ZERO;
    depthStencil.DepthFunc = toD3D12CompareFunc(depthCompareOp_);
    depthStencil.StencilEnable = stencilTestEnable_ ? TRUE : FALSE;
    depthStencil.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    depthStencil.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    depthStencil.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    depthStencil.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    depthStencil.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    depthStencil.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    depthStencil.BackFace = depthStencil.FrontFace;

    // Render targets: formats must be known up-front; pad the blend state with
    // the engine-visible number of color attachments.
    const UINT numRTVs = static_cast<UINT>(rtvFormats_.size());
    if (numRTVs == 0) {
        throw std::runtime_error("[DX12GraphicsPipelineBuilder] no RTV formats: call setRenderPass() "
                                 "(DX12 cannot infer render target formats)");
    }
    if (numRTVs > 8) {
        throw std::runtime_error("[DX12GraphicsPipelineBuilder] more than 8 render targets");
    }

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = (numRTVs > 1) ? TRUE : FALSE;
    for (UINT i = 0; i < numRTVs; ++i) {
        if (i < colorBlendAttachments_.size()) {
            blendDesc.RenderTarget[i] = toD3D12RenderTargetBlendDesc(colorBlendAttachments_[i]);
        } else {
            blendDesc.RenderTarget[i] = defaultBlendAttachment();
        }
    }

    // Samples
    DXGI_SAMPLE_DESC sampleDesc = {};
    sampleDesc.Count = static_cast<UINT>(sampleCount_);
    sampleDesc.Quality = 0;

    // PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSig;
    psoDesc.VS = vsBytecode;
    psoDesc.PS = psBytecode;
    psoDesc.RasterizerState = rasterizer;
    psoDesc.DepthStencilState = depthStencil;
    psoDesc.BlendState = blendDesc;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = toD3D12TopoType(topology_);
    psoDesc.SampleDesc = sampleDesc;
    psoDesc.InputLayout.NumElements = static_cast<UINT>(inputElements.size());
    psoDesc.InputLayout.pInputElementDescs = inputElements.empty() ? nullptr : inputElements.data();
    psoDesc.NumRenderTargets = numRTVs;
    for (UINT i = 0; i < numRTVs; ++i) {
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
        throw std::runtime_error("[DX12GraphicsPipelineBuilder] failed to create graphics pipeline state");
    }
}

std::shared_ptr<RHIPipeline> DX12GraphicsPipelineBuilder::build() {
    DX12RootSignatureResult rootResult = DX12BuildRootSignature(device_, bindingLayouts_,
                                                                pushConstantRanges_,
                                                                /*allowInputAssembler=*/true);

    ComPtr<ID3D12PipelineState> pso;
    buildGraphicsPipelineState(rootResult.rootSig.Get(), pso, rootResult);

    std::vector<std::pair<uint32_t, uint32_t>> strides;
    for (const auto& vb : vertexBindings_) {
        strides.emplace_back(vb.binding, vb.stride);
    }

    return std::make_shared<DX12RHIPipeline>(device_, pso, rootResult.rootSig,
                                             RHIPipelineType::Graphics,
                                             std::move(rootResult.layoutTables),
                                             rootResult.pushConstantRootParam,
                                             std::move(strides),
                                             topology_);
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
    DX12RootSignatureResult rootResult = DX12BuildRootSignature(device_, bindingLayouts_,
                                                                pushConstantRanges_,
                                                                /*allowInputAssembler=*/false);

    DX12RHIShader cs(device_, RHIShaderStage::Compute, computeShaderPath_);
    D3D12_SHADER_BYTECODE csBytecode = cs.getBytecode();

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootResult.rootSig.Get();
    psoDesc.CS = csBytecode;
    psoDesc.NodeMask = 0;
    psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    ComPtr<ID3D12PipelineState> pso;
    HRESULT hr = device_->getDevice()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pso));
    if (FAILED(hr)) {
        throw std::runtime_error("[DX12ComputePipelineBuilder] failed to create compute pipeline state");
    }

    return std::make_shared<DX12RHIPipeline>(device_, pso, rootResult.rootSig,
                                             RHIPipelineType::Compute,
                                             std::move(rootResult.layoutTables),
                                             rootResult.pushConstantRootParam,
                                             std::vector<std::pair<uint32_t, uint32_t>>{},
                                             RHIPrimitiveTopology::TriangleList);
}
