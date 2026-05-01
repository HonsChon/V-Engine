#include "VulkanRHIDevice.h"
#include "VulkanTypeConversions.h"
#include "IVulkanNative.h"

#include <iostream>
#include <stdexcept>
#include <set>
#include <algorithm>
#include <cstring>

// =============================================================================
// Debug messenger helpers (static)
// =============================================================================

static VKAPI_ATTR VkBool32 VKAPI_CALL vulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << "[Vulkan Validation] " << pCallbackData->pMessage << std::endl;
    }
    return VK_FALSE;
}

static VkResult CreateDebugUtilsMessengerEXT(VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pDebugMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    return func ? func(instance, pCreateInfo, pAllocator, pDebugMessenger) : VK_ERROR_EXTENSION_NOT_PRESENT;
}

static void DestroyDebugUtilsMessengerEXT(VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func) func(instance, debugMessenger, pAllocator);
}

// =============================================================================
// Constructor / Destructor
// =============================================================================

VulkanRHIDevice::VulkanRHIDevice(GLFWwindow* window)
    : window_(window) {
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createCommandPool();
    createNewDescriptorPool();  // initial descriptor pool
    std::cout << "[VulkanRHIDevice] Initialized successfully (standalone).\n";
}

VulkanRHIDevice::~VulkanRHIDevice() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }

    // Destroy descriptor pools
    for (auto pool : descriptorPools_) {
        vkDestroyDescriptorPool(device_, pool, nullptr);
    }

    // Destroy core Vulkan objects
    vkDestroyCommandPool(device_, commandPool_, nullptr);
    vkDestroyDevice(device_, nullptr);

    if (enableValidationLayers_) {
        DestroyDebugUtilsMessengerEXT(instance_, debugMessenger_, nullptr);
    }
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
    vkDestroyInstance(instance_, nullptr);
}

// =============================================================================
// RHIDevice interface — factory stubs (implemented after backend classes exist)
// =============================================================================

// Forward-declared in separate compilation units; include headers here
#include "VulkanRHIBuffer.h"
#include "VulkanRHITexture.h"
#include "VulkanRHISampler.h"
#include "VulkanRHIShader.h"
#include "VulkanRHIDescriptor.h"
#include "VulkanRHIPipeline.h"
#include "VulkanRHIRenderPass.h"
#include "VulkanRHICommandBuffer.h"

std::unique_ptr<RHIBuffer> VulkanRHIDevice::createBuffer(const RHIBufferDesc& desc) {
    return std::make_unique<VulkanRHIBuffer>(this, desc);
}

std::unique_ptr<RHITexture> VulkanRHIDevice::createTexture(const RHITextureDesc& desc) {
    return std::make_unique<VulkanRHITexture>(this, desc);
}

std::unique_ptr<RHISampler> VulkanRHIDevice::createSampler(const RHISamplerDesc& desc) {
    return std::make_unique<VulkanRHISampler>(this, desc);
}

std::unique_ptr<RHIShader> VulkanRHIDevice::createShader(RHIShaderStage stage, const std::string& filePath) {
    return std::make_unique<VulkanRHIShader>(this, stage, filePath);
}

std::unique_ptr<RHIBindingLayout> VulkanRHIDevice::createBindingLayout(const RHIBindingLayoutDesc& desc) {
    return std::make_unique<VulkanRHIBindingLayout>(this, desc);
}

std::unique_ptr<RHIBindingGroup> VulkanRHIDevice::createBindingGroup(
    RHIBindingLayout* layout, const RHIBindingGroupDesc& desc) {
    auto* vkLayout = static_cast<VulkanRHIBindingLayout*>(layout);
    return std::make_unique<VulkanRHIBindingGroup>(this, vkLayout, desc);
}

std::unique_ptr<RHIGraphicsPipelineBuilder> VulkanRHIDevice::createGraphicsPipelineBuilder() {
    return std::make_unique<VulkanGraphicsPipelineBuilder>(this);
}

std::unique_ptr<RHIComputePipelineBuilder> VulkanRHIDevice::createComputePipelineBuilder() {
    return std::make_unique<VulkanComputePipelineBuilder>(this);
}

std::unique_ptr<RHIRenderPass> VulkanRHIDevice::createRenderPass(const RHIRenderPassDesc& desc) {
    return std::make_unique<VulkanRHIRenderPass>(this, desc);
}

std::unique_ptr<RHIFramebuffer> VulkanRHIDevice::createFramebuffer(const RHIFramebufferDesc& desc) {
    return std::make_unique<VulkanRHIFramebuffer>(this, desc);
}

std::unique_ptr<RHIRenderPass> VulkanRHIDevice::wrapExternalRenderPass(void* nativeHandle) {
    return std::make_unique<VulkanRHIRenderPass>(static_cast<VkRenderPass>(nativeHandle));
}

// =============================================================================
// Non-owning wrappers for external resources
// =============================================================================

namespace {

/// Non-owning wrapper around an existing VkBuffer
class VulkanExternalBuffer : public RHIBuffer, public IVulkanNativeBuffer {
public:
    VulkanExternalBuffer(VkBuffer buffer, uint64_t size)
        : buffer_(buffer), size_(size) {}
    ~VulkanExternalBuffer() override = default; // Does NOT destroy the buffer

    void* map() override { return nullptr; } // Not supported for external buffers
    void  unmap() override {}
    void  uploadData(const void*, uint64_t, uint64_t) override {}
    uint64_t       getSize() const override { return size_; }
    RHIBufferUsage getUsage() const override { return RHIBufferUsage::Vertex; }
    RHIMemoryUsage getMemoryUsage() const override { return RHIMemoryUsage::GPUOnly; }

    // IVulkanNativeBuffer implementation
    VkBuffer getVkBuffer() const override { return buffer_; }

private:
    VkBuffer buffer_;
    uint64_t size_;
};

/// Non-owning wrapper around an existing VkImage + VkImageView
class VulkanExternalTexture : public RHITexture, public IVulkanNativeTexture {
public:
    VulkanExternalTexture(VkImage image, VkImageView view,
                          uint32_t w, uint32_t h, RHIFormat format)
        : image_(image), view_(view), width_(w), height_(h), format_(format) {}
    ~VulkanExternalTexture() override = default; // Does NOT destroy resources

    uint32_t        getWidth() const override { return width_; }
    uint32_t        getHeight() const override { return height_; }
    uint32_t        getDepth() const override { return 1; }
    uint32_t        getMipLevels() const override { return 1; }
    uint32_t        getArrayLayers() const override { return 1; }
    RHIFormat       getFormat() const override { return format_; }
    RHITextureUsage getUsage() const override { return RHITextureUsage::Sampled; }

    // IVulkanNativeTexture implementation
    VkImage     getVkImage() const override { return image_; }
    VkImageView getVkImageView() const override { return view_; }

private:
    VkImage     image_;
    VkImageView view_;
    uint32_t    width_, height_;
    RHIFormat   format_;
};

/// Non-owning wrapper around an existing VkSampler
class VulkanExternalSampler : public RHISampler, public IVulkanNativeSampler {
public:
    explicit VulkanExternalSampler(VkSampler sampler) : sampler_(sampler) {}
    ~VulkanExternalSampler() override = default; // Does NOT destroy the sampler

    // IVulkanNativeSampler implementation
    VkSampler getVkSampler() const override { return sampler_; }

private:
    VkSampler sampler_;
};

} // anonymous namespace

std::unique_ptr<RHIBuffer> VulkanRHIDevice::wrapExternalBuffer(void* nativeBuffer, uint64_t size) {
    return std::make_unique<VulkanExternalBuffer>(static_cast<VkBuffer>(nativeBuffer), size);
}

std::unique_ptr<RHITexture> VulkanRHIDevice::wrapExternalTexture(void* nativeImage, void* nativeImageView,
                                                                   uint32_t width, uint32_t height,
                                                                   RHIFormat format) {
    return std::make_unique<VulkanExternalTexture>(
        static_cast<VkImage>(nativeImage),
        static_cast<VkImageView>(nativeImageView),
        width, height, format);
}

std::unique_ptr<RHISampler> VulkanRHIDevice::wrapExternalSampler(void* nativeSampler) {
    return std::make_unique<VulkanExternalSampler>(static_cast<VkSampler>(nativeSampler));
}

std::unique_ptr<RHIBindingGroup> VulkanRHIDevice::allocateBindingGroup(RHIBindingLayout* layout) {
    // Create an empty binding group (no initial writes)
    RHIBindingGroupDesc emptyDesc;
    return createBindingGroup(layout, emptyDesc);
}

std::unique_ptr<RHICommandBuffer> VulkanRHIDevice::wrapCommandBuffer(void* nativeCmd) {
    return std::make_unique<VulkanRHICommandBuffer>(this, static_cast<VkCommandBuffer>(nativeCmd));
}

void VulkanRHIDevice::waitIdle() {
    vkDeviceWaitIdle(device_);
}

uint32_t VulkanRHIDevice::findMemoryType(uint32_t typeFilter, uint32_t propertyFlags) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & propertyFlags) == propertyFlags) {
            return i;
        }
    }
    throw std::runtime_error("[VulkanRHIDevice] Failed to find suitable memory type!");
}

// =============================================================================
// Internal helpers
// =============================================================================

VkCommandBuffer VulkanRHIDevice::beginSingleTimeCommandsVk() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool_;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device_, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void VulkanRHIDevice::endSingleTimeCommandsVk(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue_);
    vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
}

// =============================================================================
// RHIDevice interface — Single-time commands (void* wrapper)
// =============================================================================

void* VulkanRHIDevice::beginSingleTimeCommands() {
    VkCommandBuffer cmd = beginSingleTimeCommandsVk();
    return (void*)cmd;
}

void VulkanRHIDevice::endSingleTimeCommands(void* commandBuffer) {
    endSingleTimeCommandsVk(static_cast<VkCommandBuffer>(commandBuffer));
}

// =============================================================================
// RHIDevice interface — SwapChain factory
// =============================================================================

#include "VulkanRHISwapChain.h"

std::unique_ptr<RHISwapChain> VulkanRHIDevice::createSwapChain(uint32_t width, uint32_t height) {
    return std::make_unique<VulkanRHISwapChain>(this, width, height);
}

// =============================================================================
// RHIDevice interface — Format query
// =============================================================================

RHIFormat VulkanRHIDevice::findSupportedFormat(const std::vector<RHIFormat>& candidates,
                                               uint32_t tiling, uint32_t features) {
    using namespace VulkanTypeConversions;
    VkImageTiling vkTiling = (tiling == 0) ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;

    for (auto fmt : candidates) {
        VkFormat vkFmt = toVkFormat(fmt);
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice_, vkFmt, &props);

        if (vkTiling == VK_IMAGE_TILING_LINEAR &&
            (props.linearTilingFeatures & features) == features) {
            return fmt;
        } else if (vkTiling == VK_IMAGE_TILING_OPTIMAL &&
                   (props.optimalTilingFeatures & features) == features) {
            return fmt;
        }
    }
    throw std::runtime_error("[VulkanRHIDevice] Failed to find supported format!");
}

RHIFormat VulkanRHIDevice::findDepthFormat() {
    // Try formats in priority order
    for (VkFormat fmt : { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT }) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(physicalDevice_, fmt, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            using namespace VulkanTypeConversions;
            return fromVkFormat(fmt);
        }
    }
    throw std::runtime_error("[VulkanRHIDevice] Failed to find depth format!");
}

// =============================================================================
// RHIDevice interface — Raw buffer/image creation
// =============================================================================

void VulkanRHIDevice::createRawBuffer(uint64_t size, uint32_t usage, uint32_t memoryProperties,
                                       void* outBuffer, void* outMemory) {
    VkBuffer buffer;
    VkDeviceMemory memory;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = static_cast<VkBufferUsageFlags>(usage);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanRHIDevice] Failed to create raw buffer!");
    }

    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device_, buffer, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, memoryProperties);

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanRHIDevice] Failed to allocate raw buffer memory!");
    }
    vkBindBufferMemory(device_, buffer, memory, 0);

    *static_cast<VkBuffer*>(outBuffer) = buffer;
    *static_cast<VkDeviceMemory*>(outMemory) = memory;
}

void VulkanRHIDevice::createRawImage(uint32_t width, uint32_t height, RHIFormat format,
                                      uint32_t tiling, uint32_t usage, uint32_t memoryProperties,
                                      void* outImage, void* outMemory) {
    using namespace VulkanTypeConversions;
    VkImage image;
    VkDeviceMemory memory;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = toVkFormat(format);
    imageInfo.tiling = (tiling == 0) ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = static_cast<VkImageUsageFlags>(usage);
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device_, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanRHIDevice] Failed to create raw image!");
    }

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(device_, image, &memReq);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReq.memoryTypeBits, memoryProperties);

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanRHIDevice] Failed to allocate raw image memory!");
    }
    vkBindImageMemory(device_, image, memory, 0);

    *static_cast<VkImage*>(outImage) = image;
    *static_cast<VkDeviceMemory*>(outMemory) = memory;
}

void VulkanRHIDevice::copyBuffer(void* srcBuffer, void* dstBuffer, uint64_t size) {
    VkCommandBuffer cmd = beginSingleTimeCommandsVk();

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(cmd, static_cast<VkBuffer>(srcBuffer),
                    static_cast<VkBuffer>(dstBuffer), 1, &copyRegion);

    endSingleTimeCommandsVk(cmd);
}

// =============================================================================
// RHIDevice interface — Debug labels
// =============================================================================

void VulkanRHIDevice::beginDebugLabel(void* commandBuffer, const char* name,
                                       float r, float g, float b, float a) {
    auto func = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(
        instance_, "vkCmdBeginDebugUtilsLabelEXT");
    if (func) {
        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pLabelName = name;
        label.color[0] = r; label.color[1] = g; label.color[2] = b; label.color[3] = a;
        func(static_cast<VkCommandBuffer>(commandBuffer), &label);
    }
}

void VulkanRHIDevice::endDebugLabel(void* commandBuffer) {
    auto func = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(
        instance_, "vkCmdEndDebugUtilsLabelEXT");
    if (func) {
        func(static_cast<VkCommandBuffer>(commandBuffer));
    }
}

void VulkanRHIDevice::insertDebugLabel(void* commandBuffer, const char* name,
                                        float r, float g, float b, float a) {
    auto func = (PFN_vkCmdInsertDebugUtilsLabelEXT)vkGetInstanceProcAddr(
        instance_, "vkCmdInsertDebugUtilsLabelEXT");
    if (func) {
        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pLabelName = name;
        label.color[0] = r; label.color[1] = g; label.color[2] = b; label.color[3] = a;
        func(static_cast<VkCommandBuffer>(commandBuffer), &label);
    }
}

// =============================================================================
// RHIDevice interface — Sync objects
// =============================================================================

void* VulkanRHIDevice::createSemaphore() {
    VkSemaphoreCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkSemaphore semaphore;
    if (vkCreateSemaphore(device_, &info, nullptr, &semaphore) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanRHIDevice] Failed to create semaphore!");
    }
    return (void*)semaphore;
}

void* VulkanRHIDevice::createFence(bool signaled) {
    VkFenceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (signaled) info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkFence fence;
    if (vkCreateFence(device_, &info, nullptr, &fence) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanRHIDevice] Failed to create fence!");
    }
    return (void*)fence;
}

void VulkanRHIDevice::destroySemaphore(void* semaphore) {
    if (semaphore) vkDestroySemaphore(device_, static_cast<VkSemaphore>(semaphore), nullptr);
}

void VulkanRHIDevice::destroyFence(void* fence) {
    if (fence) vkDestroyFence(device_, static_cast<VkFence>(fence), nullptr);
}

void VulkanRHIDevice::waitForFence(void* fence) {
    VkFence vkFence = static_cast<VkFence>(fence);
    vkWaitForFences(device_, 1, &vkFence, VK_TRUE, UINT64_MAX);
}

void VulkanRHIDevice::resetFence(void* fence) {
    VkFence vkFence = static_cast<VkFence>(fence);
    vkResetFences(device_, 1, &vkFence);
}

// =============================================================================
// RHIDevice interface — Command buffer allocation
// =============================================================================

std::vector<void*> VulkanRHIDevice::allocateCommandBuffers(uint32_t count) {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = count;

    std::vector<VkCommandBuffer> cmdBuffers(count);
    if (vkAllocateCommandBuffers(device_, &allocInfo, cmdBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanRHIDevice] Failed to allocate command buffers!");
    }

    std::vector<void*> result(count);
    for (uint32_t i = 0; i < count; i++) {
        result[i] = (void*)cmdBuffers[i];
    }
    return result;
}

// =============================================================================
// RHIDevice interface — Queue submission
// =============================================================================

void VulkanRHIDevice::submitGraphicsQueue(const std::vector<void*>& waitSemaphores,
                                           const std::vector<uint32_t>& waitStages,
                                           const std::vector<void*>& commandBuffers,
                                           const std::vector<void*>& signalSemaphores,
                                           void* fence) {
    // Convert void* arrays to Vulkan types
    std::vector<VkSemaphore> vkWaitSems(waitSemaphores.size());
    for (size_t i = 0; i < waitSemaphores.size(); i++)
        vkWaitSems[i] = static_cast<VkSemaphore>(waitSemaphores[i]);

    std::vector<VkPipelineStageFlags> vkWaitStages(waitStages.begin(), waitStages.end());

    std::vector<VkCommandBuffer> vkCmds(commandBuffers.size());
    for (size_t i = 0; i < commandBuffers.size(); i++)
        vkCmds[i] = static_cast<VkCommandBuffer>(commandBuffers[i]);

    std::vector<VkSemaphore> vkSignalSems(signalSemaphores.size());
    for (size_t i = 0; i < signalSemaphores.size(); i++)
        vkSignalSems[i] = static_cast<VkSemaphore>(signalSemaphores[i]);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = static_cast<uint32_t>(vkWaitSems.size());
    submitInfo.pWaitSemaphores = vkWaitSems.data();
    submitInfo.pWaitDstStageMask = vkWaitStages.data();
    submitInfo.commandBufferCount = static_cast<uint32_t>(vkCmds.size());
    submitInfo.pCommandBuffers = vkCmds.data();
    submitInfo.signalSemaphoreCount = static_cast<uint32_t>(vkSignalSems.size());
    submitInfo.pSignalSemaphores = vkSignalSems.data();

    VkFence vkFence = fence ? static_cast<VkFence>(fence) : VK_NULL_HANDLE;

    if (vkQueueSubmit(graphicsQueue_, 1, &submitInfo, vkFence) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanRHIDevice] Failed to submit to graphics queue!");
    }
}

// =============================================================================
// Descriptor Pool auto-grow
// =============================================================================

void VulkanRHIDevice::createNewDescriptorPool() {
    std::vector<VkDescriptorPoolSize> poolSizes = {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         POOL_MAX_SETS },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         POOL_MAX_SETS },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, POOL_MAX_SETS },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          POOL_MAX_SETS / 2 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          POOL_MAX_SETS / 4 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,        POOL_MAX_SETS / 4 },
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = POOL_MAX_SETS;

    VkDescriptorPool pool;
    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanRHIDevice] Failed to create descriptor pool!");
    }
    descriptorPools_.push_back(pool);
}

VkDescriptorSet VulkanRHIDevice::allocateDescriptorSet(VkDescriptorSetLayout layout) {
    // Try to allocate from the last pool
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    for (int i = static_cast<int>(descriptorPools_.size()) - 1; i >= 0; i--) {
        allocInfo.descriptorPool = descriptorPools_[i];
        VkDescriptorSet set;
        VkResult result = vkAllocateDescriptorSets(device_, &allocInfo, &set);
        if (result == VK_SUCCESS) {
            return set;
        }
    }

    // All pools full — create a new one and retry
    createNewDescriptorPool();
    allocInfo.descriptorPool = descriptorPools_.back();
    VkDescriptorSet set;
    if (vkAllocateDescriptorSets(device_, &allocInfo, &set) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanRHIDevice] Failed to allocate descriptor set after pool expansion!");
    }
    return set;
}

// =============================================================================
// Vulkan initialization (mirrored from existing VulkanDevice)
// =============================================================================

void VulkanRHIDevice::createInstance() {
    if (enableValidationLayers_ && !checkValidationLayerSupport()) {
        throw std::runtime_error("Validation layers requested but not available!");
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "V-Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "V-Engine RHI";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    auto extensions = getRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enableValidationLayers_) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers_.size());
        createInfo.ppEnabledLayerNames = validationLayers_.data();

        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = vulkanDebugCallback;
        createInfo.pNext = &debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

#ifdef __APPLE__
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    if (vkCreateInstance(&createInfo, nullptr, &instance_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance!");
    }
}

void VulkanRHIDevice::setupDebugMessenger() {
    if (!enableValidationLayers_) return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                  VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = vulkanDebugCallback;

    if (CreateDebugUtilsMessengerEXT(instance_, &createInfo, nullptr, &debugMessenger_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to setup debug messenger!");
    }
}

void VulkanRHIDevice::createSurface() {
    if (glfwCreateWindowSurface(instance_, window_, nullptr, &surface_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create window surface!");
    }
}

void VulkanRHIDevice::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
    if (deviceCount == 0) throw std::runtime_error("No GPUs with Vulkan support!");

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());

    for (const auto& dev : devices) {
        if (isDeviceSuitable(dev)) { physicalDevice_ = dev; break; }
    }
    if (physicalDevice_ == VK_NULL_HANDLE) throw std::runtime_error("No suitable GPU found!");

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physicalDevice_, &props);
    std::cout << "[VulkanRHIDevice] Using GPU: " << props.deviceName << std::endl;
}

void VulkanRHIDevice::createLogicalDevice() {
    auto indices = findQueueFamilies(physicalDevice_);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueFamilies = { indices.graphicsFamily.value(), indices.presentFamily.value() };
    float queuePriority = 1.0f;

    for (uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueInfo);
    }

    VkPhysicalDeviceFeatures supportedFeatures;
    vkGetPhysicalDeviceFeatures(physicalDevice_, &supportedFeatures);

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = supportedFeatures.samplerAnisotropy;
    deviceFeatures.fillModeNonSolid = supportedFeatures.fillModeNonSolid;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions_.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions_.data();

    if (enableValidationLayers_) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers_.size());
        createInfo.ppEnabledLayerNames = validationLayers_.data();
    }

    if (vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create logical device!");
    }

    vkGetDeviceQueue(device_, indices.graphicsFamily.value(), 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, indices.presentFamily.value(), 0, &presentQueue_);
    graphicsQueueFamily_ = indices.graphicsFamily.value();
}

void VulkanRHIDevice::createCommandPool() {
    auto indices = findQueueFamilies(physicalDevice_);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = indices.graphicsFamily.value();

    if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool!");
    }
}

bool VulkanRHIDevice::checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : validationLayers_) {
        bool found = false;
        for (const auto& props : availableLayers) {
            if (strcmp(layerName, props.layerName) == 0) { found = true; break; }
        }
        if (!found) return false;
    }
    return true;
}

std::vector<const char*> VulkanRHIDevice::getRequiredExtensions() {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (enableValidationLayers_) extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

#ifdef __APPLE__
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#endif
    return extensions;
}

bool VulkanRHIDevice::isDeviceSuitable(VkPhysicalDevice device) {
    auto indices = findQueueFamilies(device);
    bool extOk = checkDeviceExtensionSupport(device);

    bool swapChainOk = false;
    if (extOk) {
        VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface_, &caps);
        uint32_t fmtCount = 0, modeCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &fmtCount, nullptr);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &modeCount, nullptr);
        swapChainOk = (fmtCount > 0 && modeCount > 0);
    }

#ifdef __APPLE__
    return indices.isComplete() && extOk && swapChainOk;
#else
    VkPhysicalDeviceFeatures feat;
    vkGetPhysicalDeviceFeatures(device, &feat);
    return indices.isComplete() && extOk && swapChainOk && feat.samplerAnisotropy;
#endif
}

bool VulkanRHIDevice::checkDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t count;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available.data());

    std::set<std::string> required(deviceExtensions_.begin(), deviceExtensions_.end());
    for (const auto& ext : available) required.erase(ext.extensionName);
    return required.empty();
}

VulkanQueueFamilyIndices VulkanRHIDevice::findQueueFamilies(VkPhysicalDevice device) {
    VulkanQueueFamilyIndices indices;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    for (uint32_t i = 0; i < count; i++) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) indices.graphicsFamily = i;
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupport);
        if (presentSupport) indices.presentFamily = i;
        if (indices.isComplete()) break;
    }
    return indices;
}
