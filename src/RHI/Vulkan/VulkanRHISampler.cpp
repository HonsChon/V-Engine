#include "VulkanRHISampler.h"
#include "VulkanRHIDevice.h"
#include "VulkanTypeConversions.h"
#include <stdexcept>

using namespace VulkanTypeConversions;

VulkanRHISampler::VulkanRHISampler(VulkanRHIDevice* device, const RHISamplerDesc& desc)
    : device_(device), desc_(desc)
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = toVkFilter(desc_.magFilter);
    samplerInfo.minFilter = toVkFilter(desc_.minFilter);
    samplerInfo.mipmapMode = (desc_.mipMapFilter == RHIFilter::Linear)
                             ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                             : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = toVkAddressMode(desc_.addressModeU);
    samplerInfo.addressModeV = toVkAddressMode(desc_.addressModeV);
    samplerInfo.addressModeW = toVkAddressMode(desc_.addressModeW);
    samplerInfo.mipLodBias = desc_.mipLodBias;
    samplerInfo.anisotropyEnable = desc_.anisotropyEnable ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = desc_.maxAnisotropy;
    samplerInfo.compareEnable = desc_.compareEnable ? VK_TRUE : VK_FALSE;
    samplerInfo.compareOp = toVkCompareOp(desc_.compareOp);
    samplerInfo.minLod = desc_.minLod;
    samplerInfo.maxLod = desc_.maxLod;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    if (vkCreateSampler(device_->getVkDevice(), &samplerInfo, nullptr, &sampler_) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanRHISampler] Failed to create sampler!");
    }
}

VulkanRHISampler::~VulkanRHISampler() {
    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_->getVkDevice(), sampler_, nullptr);
    }
}
