#pragma once

#include "RHISampler.h"
#include "IVulkanNative.h"

class VulkanRHIDevice;

class VulkanRHISampler : public RHISampler, public IVulkanNativeSampler {
public:
    VulkanRHISampler(VulkanRHIDevice* device, const RHISamplerDesc& desc);
    ~VulkanRHISampler() override;

    RHIFilter      getMinFilter() const { return desc_.minFilter; }
    RHIFilter      getMagFilter() const { return desc_.magFilter; }
    RHIAddressMode getAddressModeU() const { return desc_.addressModeU; }
    RHIAddressMode getAddressModeV() const { return desc_.addressModeV; }
    RHIAddressMode getAddressModeW() const { return desc_.addressModeW; }

    // IVulkanNativeSampler implementation
    VkSampler getVkSampler() const override { return sampler_; }

private:
    VulkanRHIDevice* device_;
    RHISamplerDesc   desc_;
    VkSampler        sampler_ = VK_NULL_HANDLE;
};
