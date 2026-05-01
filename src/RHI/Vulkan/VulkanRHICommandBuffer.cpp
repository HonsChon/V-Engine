#include "VulkanRHICommandBuffer.h"
#include "VulkanRHIDevice.h"
#include "VulkanRHIBuffer.h"
#include "VulkanRHITexture.h"
#include "VulkanRHIPipeline.h"
#include "VulkanRHIDescriptor.h"
#include "VulkanRHIRenderPass.h"
#include "VulkanTypeConversions.h"
#include "IVulkanNative.h"

using namespace VulkanTypeConversions;

VulkanRHICommandBuffer::VulkanRHICommandBuffer(VulkanRHIDevice* device, VkCommandBuffer cmd)
    : device_(device), cmd_(cmd) {}

void VulkanRHICommandBuffer::reset(VkCommandBuffer cmd) {
    cmd_ = cmd;
    currentPipelineLayout_ = VK_NULL_HANDLE;
    currentBindPoint_ = VK_PIPELINE_BIND_POINT_GRAPHICS;
}

// ---- RenderPass ----

void VulkanRHICommandBuffer::beginRenderPass(RHIRenderPass* renderPass,
                                              RHIFramebuffer* framebuffer,
                                              const std::vector<RHIClearValue>& clearValues) {
    auto* vkRP = static_cast<VulkanRHIRenderPass*>(renderPass);
    auto* vkFB = static_cast<VulkanRHIFramebuffer*>(framebuffer);

    std::vector<VkClearValue> vkClearValues;
    vkClearValues.reserve(clearValues.size());
    for (const auto& cv : clearValues) {
        vkClearValues.push_back(toVkClearValue(cv));
    }

    VkRenderPassBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    beginInfo.renderPass = vkRP->getVkRenderPass();
    beginInfo.framebuffer = vkFB->getVkFramebuffer();
    beginInfo.renderArea.offset = { 0, 0 };
    beginInfo.renderArea.extent = { vkFB->getWidth(), vkFB->getHeight() };
    beginInfo.clearValueCount = static_cast<uint32_t>(vkClearValues.size());
    beginInfo.pClearValues = vkClearValues.data();

    vkCmdBeginRenderPass(cmd_, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanRHICommandBuffer::endRenderPass() {
    vkCmdEndRenderPass(cmd_);
}

// ---- Pipeline binding ----

void VulkanRHICommandBuffer::bindGraphicsPipeline(RHIPipeline* pipeline) {
    auto* vkPipeline = static_cast<VulkanRHIPipeline*>(pipeline);
    vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline->getVkPipeline());
    currentPipelineLayout_ = vkPipeline->getVkPipelineLayout();
    currentBindPoint_ = VK_PIPELINE_BIND_POINT_GRAPHICS;
}

void VulkanRHICommandBuffer::bindComputePipeline(RHIPipeline* pipeline) {
    auto* vkPipeline = static_cast<VulkanRHIPipeline*>(pipeline);
    vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_COMPUTE, vkPipeline->getVkPipeline());
    currentPipelineLayout_ = vkPipeline->getVkPipelineLayout();
    currentBindPoint_ = VK_PIPELINE_BIND_POINT_COMPUTE;
}

// ---- Descriptor / Binding Group ----

void VulkanRHICommandBuffer::setBindingGroup(uint32_t set, RHIBindingGroup* group) {
    auto* vkGroup = static_cast<VulkanRHIBindingGroup*>(group);
    VkDescriptorSet ds = vkGroup->getVkDescriptorSet();
    vkCmdBindDescriptorSets(cmd_, currentBindPoint_, currentPipelineLayout_,
                            set, 1, &ds, 0, nullptr);
}

// ---- Viewport / Scissor ----

void VulkanRHICommandBuffer::setViewport(float x, float y, float width, float height,
                                          float minDepth, float maxDepth) {
    VkViewport viewport{};
    viewport.x = x;
    viewport.y = y;
    viewport.width = width;
    viewport.height = height;
    viewport.minDepth = minDepth;
    viewport.maxDepth = maxDepth;
    vkCmdSetViewport(cmd_, 0, 1, &viewport);
}

void VulkanRHICommandBuffer::setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) {
    VkRect2D scissor{};
    scissor.offset = { x, y };
    scissor.extent = { width, height };
    vkCmdSetScissor(cmd_, 0, 1, &scissor);
}

// ---- Vertex / Index buffer binding ----

void VulkanRHICommandBuffer::bindVertexBuffer(uint32_t binding, RHIBuffer* buffer, uint64_t offset) {
    auto* vkBuf = static_cast<VulkanRHIBuffer*>(buffer);
    VkBuffer buf = vkBuf->getVkBuffer();
    VkDeviceSize offsets[] = { offset };
    vkCmdBindVertexBuffers(cmd_, binding, 1, &buf, offsets);
}

void VulkanRHICommandBuffer::bindIndexBuffer(RHIBuffer* buffer, uint64_t offset,
                                              RHIIndexType indexType) {
    auto* vkBuf = static_cast<VulkanRHIBuffer*>(buffer);
    vkCmdBindIndexBuffer(cmd_, vkBuf->getVkBuffer(), offset, toVkIndexType(indexType));
}

// ---- Draw commands ----

void VulkanRHICommandBuffer::draw(uint32_t vertexCount, uint32_t instanceCount,
                                   uint32_t firstVertex, uint32_t firstInstance) {
    vkCmdDraw(cmd_, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanRHICommandBuffer::drawIndexed(uint32_t indexCount, uint32_t instanceCount,
                                          uint32_t firstIndex, int32_t vertexOffset,
                                          uint32_t firstInstance) {
    vkCmdDrawIndexed(cmd_, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VulkanRHICommandBuffer::drawIndexedIndirect(RHIBuffer* buffer, uint64_t offset,
                                                  uint32_t drawCount, uint32_t stride) {
    auto* vkBuf = static_cast<VulkanRHIBuffer*>(buffer);
    vkCmdDrawIndexedIndirect(cmd_, vkBuf->getVkBuffer(), offset, drawCount, stride);
}

// ---- Compute commands ----

void VulkanRHICommandBuffer::dispatch(uint32_t groupCountX, uint32_t groupCountY,
                                       uint32_t groupCountZ) {
    vkCmdDispatch(cmd_, groupCountX, groupCountY, groupCountZ);
}

void VulkanRHICommandBuffer::dispatchIndirect(RHIBuffer* buffer, uint64_t offset) {
    auto* vkBuf = static_cast<VulkanRHIBuffer*>(buffer);
    vkCmdDispatchIndirect(cmd_, vkBuf->getVkBuffer(), offset);
}

// ---- Push constants ----

void VulkanRHICommandBuffer::pushConstants(RHIShaderStage stages, uint32_t offset,
                                            uint32_t size, const void* data) {
    vkCmdPushConstants(cmd_, currentPipelineLayout_, toVkShaderStage(stages), offset, size, data);
}

// ---- Barriers / Transitions ----

void VulkanRHICommandBuffer::pipelineBarrier(RHIPipelineStage srcStage, RHIPipelineStage dstStage,
                                              RHIAccessFlags srcAccess, RHIAccessFlags dstAccess) {
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = toVkAccessFlags(srcAccess);
    barrier.dstAccessMask = toVkAccessFlags(dstAccess);

    vkCmdPipelineBarrier(cmd_,
                         toVkPipelineStage(srcStage), toVkPipelineStage(dstStage),
                         0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void VulkanRHICommandBuffer::transitionImageLayout(RHITexture* texture,
                                                    RHIImageLayout oldLayout,
                                                    RHIImageLayout newLayout,
                                                    RHIPipelineStage srcStage,
                                                    RHIPipelineStage dstStage) {
    auto* nativeTex = dynamic_cast<IVulkanNativeTexture*>(texture);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = toVkImageLayout(oldLayout);
    barrier.newLayout = toVkImageLayout(newLayout);
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = nativeTex->getVkImage();
    barrier.subresourceRange.aspectMask = getAspectFlags(texture->getFormat());
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = texture->getMipLevels();
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = texture->getArrayLayers();

    // Infer access masks from layouts
    switch (barrier.oldLayout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            barrier.srcAccessMask = 0; break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; break;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; break;
        default:
            barrier.srcAccessMask = 0; break;
    }

    switch (barrier.newLayout) {
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT; break;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT
                                  | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT; break;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            barrier.dstAccessMask = 0; break;
        default:
            barrier.dstAccessMask = 0; break;
    }

    vkCmdPipelineBarrier(cmd_,
                         toVkPipelineStage(srcStage), toVkPipelineStage(dstStage),
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
}

// ---- Transfer ----

void VulkanRHICommandBuffer::copyBuffer(RHIBuffer* src, RHIBuffer* dst, uint64_t size,
                                         uint64_t srcOffset, uint64_t dstOffset) {
    auto* vkSrc = static_cast<VulkanRHIBuffer*>(src);
    auto* vkDst = static_cast<VulkanRHIBuffer*>(dst);

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;
    vkCmdCopyBuffer(cmd_, vkSrc->getVkBuffer(), vkDst->getVkBuffer(), 1, &copyRegion);
}

void VulkanRHICommandBuffer::fillBuffer(RHIBuffer* buffer, uint64_t offset, uint64_t size, uint32_t data) {
    auto* vkBuf = static_cast<VulkanRHIBuffer*>(buffer);
    vkCmdFillBuffer(cmd_, vkBuf->getVkBuffer(), offset, size, data);
}

void VulkanRHICommandBuffer::blitImage(RHITexture* src, RHIImageLayout srcLayout,
                                        RHITexture* dst, RHIImageLayout dstLayout,
                                        uint32_t srcWidth, uint32_t srcHeight,
                                        uint32_t dstWidth, uint32_t dstHeight,
                                        RHIFilter filter) {
    auto* nativeSrc = dynamic_cast<IVulkanNativeTexture*>(src);
    auto* nativeDst = dynamic_cast<IVulkanNativeTexture*>(dst);

    VkImageBlit region{};
    region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.srcOffsets[0] = { 0, 0, 0 };
    region.srcOffsets[1] = { static_cast<int32_t>(srcWidth), static_cast<int32_t>(srcHeight), 1 };
    region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstOffsets[0] = { 0, 0, 0 };
    region.dstOffsets[1] = { static_cast<int32_t>(dstWidth), static_cast<int32_t>(dstHeight), 1 };

    VkFilter vkFilter = (filter == RHIFilter::Nearest) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;

    vkCmdBlitImage(cmd_,
                   nativeSrc->getVkImage(), toVkImageLayout(srcLayout),
                   nativeDst->getVkImage(), toVkImageLayout(dstLayout),
                   1, &region, vkFilter);
}

// ---- Buffer Barrier ----

void VulkanRHICommandBuffer::bufferBarrier(RHIBuffer* buffer, uint64_t size,
                                            RHIPipelineStage srcStage, RHIPipelineStage dstStage,
                                            RHIAccessFlags srcAccess, RHIAccessFlags dstAccess) {
    auto* vkBuf = static_cast<VulkanRHIBuffer*>(buffer);

    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = toVkAccessFlags(srcAccess);
    barrier.dstAccessMask = toVkAccessFlags(dstAccess);
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = vkBuf->getVkBuffer();
    barrier.offset = 0;
    barrier.size = size;

    vkCmdPipelineBarrier(cmd_,
                         toVkPipelineStage(srcStage), toVkPipelineStage(dstStage),
                         0, 0, nullptr, 1, &barrier, 0, nullptr);
}
