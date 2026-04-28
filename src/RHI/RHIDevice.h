#pragma once

#include "RHITypes.h"
#include <cstdint>
#include <memory>
#include <string>

// Forward declarations — all RHI abstract types
class RHIBuffer;
class RHITexture;
class RHISampler;
class RHIShader;
class RHIBindingLayout;
class RHIBindingGroup;
class RHIPipeline;
class RHIRenderPass;
class RHIFramebuffer;
class RHICommandBuffer;
class RHISwapChain;
class RHIGraphicsPipelineBuilder;
class RHIComputePipelineBuilder;

struct RHIBufferDesc;
struct RHITextureDesc;
struct RHISamplerDesc;
struct RHIBindingLayoutDesc;
struct RHIBindingGroupDesc;
struct RHIRenderPassDesc;
struct RHIFramebufferDesc;

// =============================================================================
// RHI Device — Abstract Core Factory Interface
// =============================================================================

class RHIDevice {
public:
    virtual ~RHIDevice() = default;

    // ---- Resource creation ----
    virtual std::unique_ptr<RHIBuffer>    createBuffer(const RHIBufferDesc& desc) = 0;
    virtual std::unique_ptr<RHITexture>   createTexture(const RHITextureDesc& desc) = 0;
    virtual std::unique_ptr<RHISampler>   createSampler(const RHISamplerDesc& desc) = 0;
    virtual std::unique_ptr<RHIShader>    createShader(RHIShaderStage stage, const std::string& filePath) = 0;

    // ---- Descriptor / Binding ----
    virtual std::unique_ptr<RHIBindingLayout> createBindingLayout(const RHIBindingLayoutDesc& desc) = 0;
    virtual std::unique_ptr<RHIBindingGroup>  createBindingGroup(
        RHIBindingLayout* layout, const RHIBindingGroupDesc& desc) = 0;

    // ---- Pipeline builders ----
    virtual std::unique_ptr<RHIGraphicsPipelineBuilder> createGraphicsPipelineBuilder() = 0;
    virtual std::unique_ptr<RHIComputePipelineBuilder>  createComputePipelineBuilder() = 0;

    // ---- RenderPass / Framebuffer ----
    virtual std::unique_ptr<RHIRenderPass>  createRenderPass(const RHIRenderPassDesc& desc) = 0;
    virtual std::unique_ptr<RHIFramebuffer> createFramebuffer(const RHIFramebufferDesc& desc) = 0;

    // ---- Queue operations ----
    virtual void waitIdle() = 0;

    // ---- Memory helpers ----
    virtual uint32_t findMemoryType(uint32_t typeFilter, uint32_t propertyFlags) = 0;

    // Non-copyable
    RHIDevice(const RHIDevice&) = delete;
    RHIDevice& operator=(const RHIDevice&) = delete;

protected:
    RHIDevice() = default;
};
