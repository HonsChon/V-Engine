#pragma once

#include "RHITypes.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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
    virtual std::shared_ptr<RHIBuffer>    createBuffer(const RHIBufferDesc& desc) = 0;
    virtual std::shared_ptr<RHITexture>   createTexture(const RHITextureDesc& desc) = 0;
    virtual std::shared_ptr<RHISampler>   createSampler(const RHISamplerDesc& desc) = 0;
    virtual std::shared_ptr<RHIShader>    createShader(RHIShaderStage stage, const std::string& filePath) = 0;

    // ---- Descriptor / Binding ----
    virtual std::shared_ptr<RHIBindingLayout> createBindingLayout(const RHIBindingLayoutDesc& desc) = 0;
    virtual std::shared_ptr<RHIBindingGroup>  createBindingGroup(
        RHIBindingLayout* layout, const RHIBindingGroupDesc& desc) = 0;

    // ---- Pipeline builders ----
    virtual std::shared_ptr<RHIGraphicsPipelineBuilder> createGraphicsPipelineBuilder() = 0;
    virtual std::shared_ptr<RHIComputePipelineBuilder>  createComputePipelineBuilder() = 0;

    // ---- RenderPass / Framebuffer ----
    virtual std::shared_ptr<RHIRenderPass>  createRenderPass(const RHIRenderPassDesc& desc) = 0;
    virtual std::shared_ptr<RHIFramebuffer> createFramebuffer(const RHIFramebufferDesc& desc) = 0;

    // ---- SwapChain factory ----
    virtual std::shared_ptr<RHISwapChain> createSwapChain(const RHISwapChainDesc& desc) = 0;

    // ---- Wrapping external resources ----
    virtual std::shared_ptr<RHIRenderPass> wrapExternalRenderPass(void* nativeHandle) = 0;
    virtual std::shared_ptr<RHIBuffer> wrapExternalBuffer(void* nativeBuffer, uint64_t size) = 0;
    virtual std::shared_ptr<RHITexture> wrapExternalTexture(void* nativeImage, void* nativeImageView,
                                                             uint32_t width, uint32_t height,
                                                             RHIFormat format) = 0;
    virtual std::shared_ptr<RHISampler> wrapExternalSampler(void* nativeSampler) = 0;
    virtual std::shared_ptr<RHIBindingGroup> allocateBindingGroup(RHIBindingLayout* layout) = 0;
    virtual std::shared_ptr<RHICommandBuffer> wrapCommandBuffer(void* nativeCmd) = 0;

    // ---- Queue operations ----
    virtual void waitIdle() = 0;

    // ---- Memory helpers ----
    virtual uint32_t findMemoryType(uint32_t typeFilter, uint32_t propertyFlags) = 0;

    // ---- Format query ----
    /// Find the first supported format from candidates that supports the given tiling/features.
    /// @param candidates  List of RHIFormat candidates (priority order)
    /// @param tiling      0 = linear, 1 = optimal (maps to backend tiling modes)
    /// @param features    Backend-specific format feature flags (cast to uint32_t)
    virtual RHIFormat findSupportedFormat(const std::vector<RHIFormat>& candidates,
                                          uint32_t tiling, uint32_t features) = 0;

    /// Find the best available depth format
    virtual RHIFormat findDepthFormat() = 0;

    // ---- Raw buffer/image creation (low-level, for legacy interop) ----
    /// Create a raw GPU buffer. Handles are returned as void* for backend-agnostic usage.
    virtual void createRawBuffer(uint64_t size, uint32_t usage, uint32_t memoryProperties,
                                  void* outBuffer, void* outMemory) = 0;

    /// Create a raw GPU image.
    virtual void createRawImage(uint32_t width, uint32_t height, RHIFormat format,
                                 uint32_t tiling, uint32_t usage, uint32_t memoryProperties,
                                 void* outImage, void* outMemory) = 0;

    /// Copy data between two raw buffers
    virtual void copyBuffer(void* srcBuffer, void* dstBuffer, uint64_t size) = 0;

    // ---- Single-time command helpers ----
    /// Begin a single-time-use command buffer. Returns native cmd handle as void*.
    virtual void* beginSingleTimeCommands() = 0;

    /// End and submit single-time command buffer, wait for completion.
    virtual void endSingleTimeCommands(void* commandBuffer) = 0;

    // ---- Debug labels (RenderDoc support) ----
    virtual void beginDebugLabel(void* commandBuffer, const char* name,
                                  float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f) = 0;
    virtual void endDebugLabel(void* commandBuffer) = 0;
    virtual void insertDebugLabel(void* commandBuffer, const char* name,
                                   float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f) = 0;

    // ---- Sync objects ----
    /// Create a semaphore. Returns native handle as void*.
    virtual void* createSemaphore() = 0;

    /// Create a fence. Returns native handle as void*.
    /// @param signaled  If true, fence starts in signaled state.
    virtual void* createFence(bool signaled = false) = 0;

    virtual void destroySemaphore(void* semaphore) = 0;
    virtual void destroyFence(void* fence) = 0;

    /// Wait for a fence to be signaled (blocks).
    virtual void waitForFence(void* fence) = 0;

    /// Reset a fence to unsignaled state.
    virtual void resetFence(void* fence) = 0;

    // ---- Command buffer allocation ----
    /// Allocate primary command buffers from the device command pool.
    /// Returns a vector of native command buffer handles.
    virtual std::vector<void*> allocateCommandBuffers(uint32_t count) = 0;

    // ---- Queue submission ----
    /// Submit command buffers to the graphics queue.
    /// All parameters are arrays of native handles (void*).
    virtual void submitGraphicsQueue(const std::vector<void*>& waitSemaphores,
                                      const std::vector<uint32_t>& waitStages,
                                      const std::vector<void*>& commandBuffers,
                                      const std::vector<void*>& signalSemaphores,
                                      void* fence) = 0;

    // ---- Native handle access (for ImGui and low-level integrations) ----
    virtual void*    getNativeDevice() const = 0;
    virtual void*    getNativeInstance() const = 0;
    virtual void*    getNativePhysicalDevice() const = 0;
    virtual uint32_t getGraphicsQueueFamilyIndex() const = 0;
    virtual void*    getNativeGraphicsQueue() const = 0;
    virtual void*    getNativeCommandPool() const = 0;

    // Non-copyable
    RHIDevice(const RHIDevice&) = delete;
    RHIDevice& operator=(const RHIDevice&) = delete;

protected:
    RHIDevice() = default;
};