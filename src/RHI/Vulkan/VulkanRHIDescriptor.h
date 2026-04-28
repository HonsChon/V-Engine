#pragma once

#include "RHIDescriptor.h"
#include <vulkan/vulkan.h>

class VulkanRHIDevice;

// =============================================================================
// VulkanRHIBindingLayout — wraps VkDescriptorSetLayout
// =============================================================================

class VulkanRHIBindingLayout : public RHIBindingLayout {
public:
    VulkanRHIBindingLayout(VulkanRHIDevice* device, const RHIBindingLayoutDesc& desc);
    ~VulkanRHIBindingLayout() override;

    VkDescriptorSetLayout getVkDescriptorSetLayout() const { return layout_; }
    const RHIBindingLayoutDesc& getDesc() const { return desc_; }

private:
    VulkanRHIDevice*      device_;
    RHIBindingLayoutDesc  desc_;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
};

// =============================================================================
// VulkanRHIBindingGroup — wraps VkDescriptorSet
// =============================================================================

class VulkanRHIBindingGroup : public RHIBindingGroup {
public:
    VulkanRHIBindingGroup(VulkanRHIDevice* device,
                          VulkanRHIBindingLayout* layout,
                          const RHIBindingGroupDesc& desc);
    ~VulkanRHIBindingGroup() override = default; // pools handle lifetime

    VkDescriptorSet getVkDescriptorSet() const { return descriptorSet_; }

private:
    void writeDescriptors(const RHIBindingGroupDesc& desc,
                          const VulkanRHIBindingLayout* layout);

    VulkanRHIDevice* device_;
    VkDescriptorSet  descriptorSet_ = VK_NULL_HANDLE;
};
