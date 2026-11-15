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

    /// RT 管线：按 export/hit-group 名取 shader identifier（32B，指向管线
    /// 内部存储，生命周期与管线一致）。非 RT 管线返回 false。
    virtual bool getShaderIdentifier(const std::string& name,
                                     const void*& outId, uint32_t& outSize) const {
        (void)name; (void)outId; (void)outSize;
        return false;
    }

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
    /// Stencil test state (front/back face ops). The static `reference` in each
    /// state is applied when the pipeline is bound (D3D12 OMSetStencilRef).
    virtual RHIGraphicsPipelineBuilder& setStencilTest(bool enable,
                                                        const RHIStencilOpState& front = {},
                                                        const RHIStencilOpState& back = {}) = 0;

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

    /// 调试名（RenderDoc / PIX 管线面板显示）
    virtual RHIGraphicsPipelineBuilder& setDebugName(const std::string& name) = 0;

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

    /// 调试名（RenderDoc / PIX 管线面板显示）
    virtual RHIComputePipelineBuilder& setDebugName(const std::string& name) = 0;

    virtual std::shared_ptr<RHIPipeline> build() = 0;

protected:
    RHIComputePipelineBuilder() = default;
};

// =============================================================================
// Ray Tracing Pipeline Builder — Abstract Interface (DXR)
//
// 输入是 DXIL library（.dxil，含 [shader(...)] 入口），而非单 stage 着色器。
// 绑定约定与 graphics/compute 相同：layout i -> space i，binding b -> register b，
// push constants -> 根常量 b0 space=layoutCount（docs/DX12-RHI-Notes.md §11）。
// 只使用全局根签名（无 local root signature / 无 per-instance SBT 参数），
// 实例数据通过 InstanceID() 索引全局实例缓冲获取。
// =============================================================================

class RHIRayTracingPipelineBuilder {
public:
    virtual ~RHIRayTracingPipelineBuilder() = default;

    /// 添加一个 DXIL library（导出全部 entry point；库内非 entry 函数不受影响）
    virtual RHIRayTracingPipelineBuilder& addLibrary(const std::string& path) = 0;

    /// raygen 入口导出名（如 "rgMain"）
    virtual RHIRayTracingPipelineBuilder& setRayGen(const std::string& exportName) = 0;

    /// miss shader（按 miss index 注册，TraceRay 的 rayFlags 后第三个参数）
    virtual RHIRayTracingPipelineBuilder& addMiss(const std::string& exportName,
                                                  uint32_t missIndex) = 0;

    /// hit group：closestHit 必填，anyHit 可为空
    virtual RHIRayTracingPipelineBuilder& addHitGroup(const std::string& groupName,
                                                      const std::string& closestHit,
                                                      const std::string& anyHit = "") = 0;

    virtual RHIRayTracingPipelineBuilder& setMaxPayloadSize(uint32_t bytes) = 0;
    virtual RHIRayTracingPipelineBuilder& setMaxAttributeSize(uint32_t bytes) = 0;
    virtual RHIRayTracingPipelineBuilder& setMaxRecursionDepth(uint32_t depth) = 0;

    // 绑定布局与 push constants（与 graphics/compute 管线同约定）
    virtual RHIRayTracingPipelineBuilder& addBindingLayout(RHIBindingLayout* layout) = 0;
    virtual RHIRayTracingPipelineBuilder& addPushConstant(RHIShaderStage stages, uint32_t offset, uint32_t size) = 0;

    /// 调试名（RenderDoc / PIX 管线面板显示）
    virtual RHIRayTracingPipelineBuilder& setDebugName(const std::string& name) = 0;

    virtual std::shared_ptr<RHIPipeline> build() = 0;

protected:
    RHIRayTracingPipelineBuilder() = default;
};
