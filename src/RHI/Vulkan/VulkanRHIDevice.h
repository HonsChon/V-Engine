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

class VulkanDevice; // Forward declaration for wrapping constructor

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

    /// Wrap an existing VulkanDevice (borrows handles — does NOT destroy them)
    explicit VulkanRHIDevice(std::shared_ptr<VulkanDevice> existingDevice);

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

    void waitIdle() override;
    uint32_t findMemoryType(uint32_t typeFilter, uint32_t propertyFlags) override;

    // ---- Vulkan-specific accessors (native handle backdoor) ----
    VkDevice         getVkDevice() const { return device_; }
    VkPhysicalDevice getVkPhysicalDevice() const { return physicalDevice_; }
    VkInstance        getVkInstance() const { return instance_; }
    VkQueue          getGraphicsQueue() const { return graphicsQueue_; }
    VkQueue          getPresentQueue() const { return presentQueue_; }
    VkCommandPool    getCommandPool() const { return commandPool_; }
    VkSurfaceKHR     getSurface() const { return surface_; }
    uint32_t         getGraphicsQueueFamily() const { return graphicsQueueFamily_; }

    // ---- Internal helpers ----
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer cmd);

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
    bool ownsDevice_ = true;  // false when wrapping an existing VulkanDevice

    // Keep a shared_ptr to the wrapped device to ensure it stays alive
    std::shared_ptr<VulkanDevice> wrappedDevice_;

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
