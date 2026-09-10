#pragma once

#include "RHITypes.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Forward declarations
class RHIBindingLayout;
class RHIRenderPass;
class RHIShader;

// =============================================================================
// RHI Pipeline — Abstract Interface
// =============================================================================

class RHIPipeline {
public:
    virtual ~RHIPipeline() = default;

    virtual RHIPipelineType getType() const = 0;

    RHIPipeline(const RHIPipeline&) = delete;
    RHIPipeline& operator=(const RHIPipeline&) = delete;

protected:
    RHIPipeline() = default;
};

// =============================================================================
// Color Blend Attachment Description
// =============================================================================

struct RHIColorBlendAttachment {
    bool              blendEnable    = false;
    RHIBlendFactor    srcColorFactor = RHIBlendFactor::One;
    RHIBlendFactor    dstColorFactor = RHIBlendFactor::Zero;
    RHIBlendOp        colorBlendOp   = RHIBlendOp::Add;
    RHIBlendFactor    srcAlphaFactor = RHIBlendFactor::One;
    RHIBlendFactor    dstAlphaFactor = RHIBlendFactor::Zero;
    RHIBlendOp        alphaBlendOp   = RHIBlendOp::Add;
    RHIColorComponent colorWriteMask = RHIColorComponent::All;
};

// =============================================================================
// Graphics Pipeline Builder — Abstract Interface (Fluent / Chain API)
// =============================================================================

class RHIGraphicsPipelineBuilder {
public:
    virtual ~RHIGraphicsPipelineBuilder() = default;

    // Shaders — accept file paths to SPIR-V .spv files
    virtual RHIGraphicsPipelineBuilder& setVertexShader(const std::string& path) = 0;
    virtual RHIGraphicsPipelineBuilder& setFragmentShader(const std::string& path) = 0;

    // Vertex input
    virtual RHIGraphicsPipelineBuilder& addVertexBinding(uint32_t binding, uint32_t stride,
                                                          RHIVertexInputRate inputRate = RHIVertexInputRate::Vertex) = 0;
    virtual RHIGraphicsPipelineBuilder& addVertexAttribute(uint32_t binding, uint32_t location,
                                                            RHIFormat format, uint32_t offset) = 0;

    // Input assembly
    virtual RHIGraphicsPipelineBuilder& setTopology(RHIPrimitiveTopology topology) = 0;

    // Rasterization
    virtual RHIGraphicsPipelineBuilder& setCullMode(RHICullMode mode) = 0;
    virtual RHIGraphicsPipelineBuilder& setFrontFace(RHIFrontFace face) = 0;
    virtual RHIGraphicsPipelineBuilder& setPolygonMode(RHIPolygonMode mode) = 0;
    virtual RHIGraphicsPipelineBuilder& setLineWidth(float width) = 0;
    virtual RHIGraphicsPipelineBuilder& setDepthBias(bool enable, float constantFactor = 0.0f,
                                                      float slopeFactor = 0.0f, float clamp = 0.0f) = 0;

    // Depth / stencil
    virtual RHIGraphicsPipelineBuilder& setDepthTest(bool enable, bool writeEnable,
                                                      RHICompareOp compareOp = RHICompareOp::Less) = 0;
    virtual RHIGraphicsPipelineBuilder& setStencilTest(bool enable) = 0;

    // Multisampling
    virtual RHIGraphicsPipelineBuilder& setSampleCount(RHISampleCount count) = 0;

    // Color blend — per attachment
    virtual RHIGraphicsPipelineBuilder& addColorBlendAttachment(const RHIColorBlendAttachment& attachment = {}) = 0;

    // Set number of color attachments (uses default blend for each if not explicitly added)
    virtual RHIGraphicsPipelineBuilder& setColorAttachmentCount(uint32_t count) = 0;

    // Dynamic states
    virtual RHIGraphicsPipelineBuilder& addDynamicState(RHIDynamicState state) = 0;

    // Binding layouts (descriptor set layouts)
    virtual RHIGraphicsPipelineBuilder& addBindingLayout(RHIBindingLayout* layout) = 0;

    // Push constants
    virtual RHIGraphicsPipelineBuilder& addPushConstant(RHIShaderStage stages, uint32_t offset, uint32_t size) = 0;

    // RenderPass
    virtual RHIGraphicsPipelineBuilder& setRenderPass(RHIRenderPass* renderPass, uint32_t subpass = 0) = 0;

    // Build!
    virtual std::shared_ptr<RHIPipeline> build() = 0;

protected:
    RHIGraphicsPipelineBuilder() = default;
};

// =============================================================================
// Compute Pipeline Builder — Abstract Interface
// =============================================================================

class RHIComputePipelineBuilder {
public:
    virtual ~RHIComputePipelineBuilder() = default;

    virtual RHIComputePipelineBuilder& setComputeShader(const std::string& path) = 0;
    virtual RHIComputePipelineBuilder& addBindingLayout(RHIBindingLayout* layout) = 0;
    virtual RHIComputePipelineBuilder& addPushConstant(RHIShaderStage stages, uint32_t offset, uint32_t size) = 0;

    virtual std::shared_ptr<RHIPipeline> build() = 0;

protected:
    RHIComputePipelineBuilder() = default;
};
