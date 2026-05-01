#pragma once

#include "RHIDevice.h"
#include "RHIBuffer.h"
#include "RHITexture.h"
#include "RHISampler.h"
#include "RHIShader.h"
#include "RHIDescriptor.h"
#include "RHIPipeline.h"
#include "RHIRenderPass.h"
#include "RHISwapChain.h"
#include "RHICommandBuffer.h"

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <vector>
#include <memory>
#include <optional>
#include <set>
#include <string>


// =============================================================================
// VulkanRHIDevice — Vulkan implementation of RHIDevice
//
// Wraps VkInstance, VkDevice, VkPhysicalDevice, VkQueue, VkCommandPool.
// Acts as the central factory for all Vulkan RHI objects.
// =============================================================================

struct VulkanQueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;
    bool isComplete() const { return graphicsFamily.has_value() && presentFamily.has_value(); }
};

class VulkanRHIDevice : public RHIDevice {
public:
    /// Create a new standalone device (owns all Vulkan objects)
    explicit VulkanRHIDevice(GLFWwindow* window);

    ~VulkanRHIDevice() override;

    // ---- RHIDevice factory methods ----
    std::unique_ptr<RHIBuffer>    createBuffer(const RHIBufferDesc& desc) override;
    std::unique_ptr<RHITexture>   createTexture(const RHITextureDesc& desc) override;
    std::unique_ptr<RHISampler>   createSampler(const RHISamplerDesc& desc) override;
    std::unique_ptr<RHIShader>    createShader(RHIShaderStage stage, const std::string& filePath) override;

    std::unique_ptr<RHIBindingLayout> createBindingLayout(const RHIBindingLayoutDesc& desc) override;
    std::unique_ptr<RHIBindingGroup>  createBindingGroup(
        RHIBindingLayout* layout, const RHIBindingGroupDesc& desc) override;

    std::unique_ptr<RHIGraphicsPipelineBuilder> createGraphicsPipelineBuilder() override;
    std::unique_ptr<RHIComputePipelineBuilder>  createComputePipelineBuilder() override;

    std::unique_ptr<RHIRenderPass>  createRenderPass(const RHIRenderPassDesc& desc) override;
    std::unique_ptr<RHIFramebuffer> createFramebuffer(const RHIFramebufferDesc& desc) override;

    std::unique_ptr<RHIRenderPass>   wrapExternalRenderPass(void* nativeHandle) override;
    std::unique_ptr<RHIBuffer>       wrapExternalBuffer(void* nativeBuffer, uint64_t size) override;
    std::unique_ptr<RHITexture>      wrapExternalTexture(void* nativeImage, void* nativeImageView,
                                                          uint32_t width, uint32_t height,
                                                          RHIFormat format) override;
    std::unique_ptr<RHISampler>      wrapExternalSampler(void* nativeSampler) override;
    std::unique_ptr<RHIBindingGroup> allocateBindingGroup(RHIBindingLayout* layout) override;
    std::unique_ptr<RHICommandBuffer> wrapCommandBuffer(void* nativeCmd) override;

    void waitIdle() override;
    uint32_t findMemoryType(uint32_t typeFilter, uint32_t propertyFlags) override;

    // ---- SwapChain factory ----
    std::unique_ptr<RHISwapChain> createSwapChain(uint32_t width, uint32_t height) override;

    // ---- Format query ----
    RHIFormat findSupportedFormat(const std::vector<RHIFormat>& candidates,
                                  uint32_t tiling, uint32_t features) override;
    RHIFormat findDepthFormat() override;

    // ---- Raw buffer/image creation ----
    void createRawBuffer(uint64_t size, uint32_t usage, uint32_t memoryProperties,
                          void* outBuffer, void* outMemory) override;
    void createRawImage(uint32_t width, uint32_t height, RHIFormat format,
                         uint32_t tiling, uint32_t usage, uint32_t memoryProperties,
                         void* outImage, void* outMemory) override;
    void copyBuffer(void* srcBuffer, void* dstBuffer, uint64_t size) override;

    // ---- Single-time commands (RHIDevice interface) ----
    void* beginSingleTimeCommands() override;
    void endSingleTimeCommands(void* commandBuffer) override;

    // ---- Debug labels ----
    void beginDebugLabel(void* commandBuffer, const char* name,
                          float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f) override;
    void endDebugLabel(void* commandBuffer) override;
    void insertDebugLabel(void* commandBuffer, const char* name,
                           float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f) override;

    // ---- Sync objects ----
    void* createSemaphore() override;
    void* createFence(bool signaled = false) override;
    void destroySemaphore(void* semaphore) override;
    void destroyFence(void* fence) override;
    void waitForFence(void* fence) override;
    void resetFence(void* fence) override;

    // ---- Command buffer allocation ----
    std::vector<void*> allocateCommandBuffers(uint32_t count) override;

    // ---- Queue submission ----
    void submitGraphicsQueue(const std::vector<void*>& waitSemaphores,
                              const std::vector<uint32_t>& waitStages,
                              const std::vector<void*>& commandBuffers,
                              const std::vector<void*>& signalSemaphores,
                              void* fence) override;

    // ---- Native handle access ----
    void*    getNativeDevice() const override { return (void*)device_; }
    void*    getNativeInstance() const override { return (void*)instance_; }
    void*    getNativePhysicalDevice() const override { return (void*)physicalDevice_; }
    uint32_t getGraphicsQueueFamilyIndex() const override { return graphicsQueueFamily_; }
    void*    getNativeGraphicsQueue() const override { return (void*)graphicsQueue_; }
    void*    getNativeCommandPool() const override { return (void*)commandPool_; }

    // ---- Vulkan-specific accessors (internal use / legacy interop) ----
    VkDevice         getVkDevice() const { return device_; }
    VkPhysicalDevice getVkPhysicalDevice() const { return physicalDevice_; }
    VkInstance        getVkInstance() const { return instance_; }
    VkQueue          getGraphicsQueue() const { return graphicsQueue_; }
    VkQueue          getPresentQueue() const { return presentQueue_; }
    VkCommandPool    getCommandPool() const { return commandPool_; }
    VkSurfaceKHR     getSurface() const { return surface_; }
    uint32_t         getGraphicsQueueFamily() const { return graphicsQueueFamily_; }

    // ---- Internal Vulkan helpers ----
    VkCommandBuffer beginSingleTimeCommandsVk();
    void endSingleTimeCommandsVk(VkCommandBuffer cmd);

    // ---- Descriptor Pool management (auto-grow) ----
    VkDescriptorSet allocateDescriptorSet(VkDescriptorSetLayout layout);

private:
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createCommandPool();

    bool checkValidationLayerSupport();
    std::vector<const char*> getRequiredExtensions();
    bool isDeviceSuitable(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    VulkanQueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);

    // ---- Descriptor pool auto-grow ----
    void createNewDescriptorPool();

    GLFWwindow* window_ = nullptr;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily_ = 0;

    // Descriptor pool management
    std::vector<VkDescriptorPool> descriptorPools_;
    static constexpr uint32_t POOL_MAX_SETS = 1024;

    const std::vector<const char*> validationLayers_ = {
        "VK_LAYER_KHRONOS_validation"
    };

    const std::vector<const char*> deviceExtensions_ = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
#ifdef __APPLE__
        , "VK_KHR_portability_subset"
#endif
    };

#ifdef NDEBUG
    static constexpr bool enableValidationLayers_ = false;
#else
    static constexpr bool enableValidationLayers_ = true;
#endif
};
