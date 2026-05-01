#pragma once

#include <vulkan/vulkan.h>

/**
 * IVulkanNativeBuffer — Vulkan backend internal interface
 * 
 * This is NOT part of the public RHI API. It exists solely within the Vulkan
 * backend to allow uniform access to VkBuffer from different buffer
 * implementations (owned VulkanRHIBuffer and non-owning VulkanExternalBuffer).
 * 
 * Any class holding a VkBuffer and participating in Vulkan command recording
 * should implement this interface via multiple inheritance.
 */
class IVulkanNativeBuffer {
public:
    virtual ~IVulkanNativeBuffer() = default;
    virtual VkBuffer getVkBuffer() const = 0;
};

/**
 * IVulkanNativeTexture — Vulkan backend internal interface
 * 
 * Same pattern for textures: uniform access to VkImage + VkImageView.
 */
class IVulkanNativeTexture {
public:
    virtual ~IVulkanNativeTexture() = default;
    virtual VkImage     getVkImage() const = 0;
    virtual VkImageView getVkImageView() const = 0;
};

/**
 * IVulkanNativeSampler — Vulkan backend internal interface
 */
class IVulkanNativeSampler {
public:
    virtual ~IVulkanNativeSampler() = default;
    virtual VkSampler getVkSampler() const = 0;
};
