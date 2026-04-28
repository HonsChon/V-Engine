#pragma once

#include "RHITypes.h"
#include <cstdint>
#include <memory>
#include <vector>

// Forward declarations
class RHIRenderPass;
class RHIFramebuffer;
class RHIPipeline;
class RHIBindingGroup;
class RHIBuffer;
class RHITexture;

// =============================================================================
// RHI CommandBuffer — Abstract Interface
// =============================================================================

class RHICommandBuffer {
public:
    virtual ~RHICommandBuffer() = default;

    // ---- RenderPass ----
    virtual void beginRenderPass(RHIRenderPass* renderPass, RHIFramebuffer* framebuffer,
                                  const std::vector<RHIClearValue>& clearValues) = 0;
    virtual void endRenderPass() = 0;

    // ---- Pipeline binding ----
    virtual void bindGraphicsPipeline(RHIPipeline* pipeline) = 0;
    virtual void bindComputePipeline(RHIPipeline* pipeline) = 0;

    // ---- Descriptor / Binding Group ----
    virtual void setBindingGroup(uint32_t set, RHIBindingGroup* group) = 0;

    // ---- Viewport / Scissor ----
    virtual void setViewport(float x, float y, float width, float height,
                              float minDepth = 0.0f, float maxDepth = 1.0f) = 0;
    virtual void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) = 0;

    // ---- Vertex / Index buffer binding ----
    virtual void bindVertexBuffer(uint32_t binding, RHIBuffer* buffer, uint64_t offset = 0) = 0;
    virtual void bindIndexBuffer(RHIBuffer* buffer, uint64_t offset, RHIIndexType indexType) = 0;

    // ---- Draw commands ----
    virtual void draw(uint32_t vertexCount, uint32_t instanceCount = 1,
                       uint32_t firstVertex = 0, uint32_t firstInstance = 0) = 0;
    virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
                              uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                              uint32_t firstInstance = 0) = 0;
    virtual void drawIndexedIndirect(RHIBuffer* buffer, uint64_t offset,
                                      uint32_t drawCount, uint32_t stride) = 0;

    // ---- Compute commands ----
    virtual void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) = 0;
    virtual void dispatchIndirect(RHIBuffer* buffer, uint64_t offset) = 0;

    // ---- Push constants ----
    virtual void pushConstants(RHIShaderStage stages, uint32_t offset,
                                uint32_t size, const void* data) = 0;

    // ---- Barriers / Transitions ----
    virtual void pipelineBarrier(
        RHIPipelineStage srcStage, RHIPipelineStage dstStage,
        RHIAccessFlags srcAccess, RHIAccessFlags dstAccess) = 0;

    virtual void transitionImageLayout(RHITexture* texture,
                                        RHIImageLayout oldLayout, RHIImageLayout newLayout,
                                        RHIPipelineStage srcStage = RHIPipelineStage::AllCommands,
                                        RHIPipelineStage dstStage = RHIPipelineStage::AllCommands) = 0;

    // ---- Transfer ----
    virtual void copyBuffer(RHIBuffer* src, RHIBuffer* dst, uint64_t size,
                             uint64_t srcOffset = 0, uint64_t dstOffset = 0) = 0;

    // Non-copyable
    RHICommandBuffer(const RHICommandBuffer&) = delete;
    RHICommandBuffer& operator=(const RHICommandBuffer&) = delete;

protected:
    RHICommandBuffer() = default;
};
