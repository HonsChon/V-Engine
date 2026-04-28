#pragma once

#include "RHICommandBuffer.h"
#include <vulkan/vulkan.h>

class VulkanRHIDevice;

// =============================================================================
// VulkanRHICommandBuffer — wraps VkCommandBuffer
//
// Note: This is a thin wrapper. The actual VkCommandBuffer is provided
// externally (allocated by the frame/swapchain system). This class translates
// RHI calls into Vulkan command recording.
// =============================================================================

class VulkanRHICommandBuffer : public RHICommandBuffer {
public:
    VulkanRHICommandBuffer(VulkanRHIDevice* device, VkCommandBuffer cmd);
    ~VulkanRHICommandBuffer() override = default;

    // Replace the underlying command buffer (for per-frame reuse)
    void reset(VkCommandBuffer cmd);
    VkCommandBuffer getVkCommandBuffer() const { return cmd_; }

    // ---- RenderPass ----
    void beginRenderPass(RHIRenderPass* renderPass, RHIFramebuffer* framebuffer,
                         const std::vector<RHIClearValue>& clearValues) override;
    void endRenderPass() override;

    // ---- Pipeline binding ----
    void bindGraphicsPipeline(RHIPipeline* pipeline) override;
    void bindComputePipeline(RHIPipeline* pipeline) override;

    // ---- Descriptor / Binding Group ----
    void setBindingGroup(uint32_t set, RHIBindingGroup* group) override;

    // ---- Viewport / Scissor ----
    void setViewport(float x, float y, float width, float height,
                     float minDepth, float maxDepth) override;
    void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) override;

    // ---- Vertex / Index buffer binding ----
    void bindVertexBuffer(uint32_t binding, RHIBuffer* buffer, uint64_t offset) override;
    void bindIndexBuffer(RHIBuffer* buffer, uint64_t offset, RHIIndexType indexType) override;

    // ---- Draw commands ----
    void draw(uint32_t vertexCount, uint32_t instanceCount,
              uint32_t firstVertex, uint32_t firstInstance) override;
    void drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                     uint32_t firstIndex, int32_t vertexOffset,
                     uint32_t firstInstance) override;
    void drawIndexedIndirect(RHIBuffer* buffer, uint64_t offset,
                             uint32_t drawCount, uint32_t stride) override;

    // ---- Compute commands ----
    void dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;
    void dispatchIndirect(RHIBuffer* buffer, uint64_t offset) override;

    // ---- Push constants ----
    void pushConstants(RHIShaderStage stages, uint32_t offset,
                       uint32_t size, const void* data) override;

    // ---- Barriers / Transitions ----
    void pipelineBarrier(RHIPipelineStage srcStage, RHIPipelineStage dstStage,
                         RHIAccessFlags srcAccess, RHIAccessFlags dstAccess) override;
    void transitionImageLayout(RHITexture* texture,
                               RHIImageLayout oldLayout, RHIImageLayout newLayout,
                               RHIPipelineStage srcStage, RHIPipelineStage dstStage) override;

    // ---- Transfer ----
    void copyBuffer(RHIBuffer* src, RHIBuffer* dst, uint64_t size,
                    uint64_t srcOffset, uint64_t dstOffset) override;

private:
    VulkanRHIDevice* device_;
    VkCommandBuffer  cmd_ = VK_NULL_HANDLE;

    // Track current pipeline layout for binding group / push constants
    VkPipelineLayout currentPipelineLayout_ = VK_NULL_HANDLE;
    VkPipelineBindPoint currentBindPoint_ = VK_PIPELINE_BIND_POINT_GRAPHICS;
};
