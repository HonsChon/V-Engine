#include "VulkanRHIDescriptor.h"
#include "VulkanRHIDevice.h"
#include "VulkanRHIBuffer.h"
#include "VulkanRHITexture.h"
#include "VulkanRHISampler.h"
#include "VulkanTypeConversions.h"
#include <stdexcept>

using namespace VulkanTypeConversions;

// =============================================================================
// VulkanRHIBindingLayout
// =============================================================================

VulkanRHIBindingLayout::VulkanRHIBindingLayout(VulkanRHIDevice* device,
                                               const RHIBindingLayoutDesc& desc)
    : device_(device), desc_(desc)
{
    std::vector<VkDescriptorSetLayoutBinding> vkBindings;
    vkBindings.reserve(desc_.entries.size());

    for (const auto& entry : desc_.entries) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = entry.binding;
        b.descriptorType = toVkDescriptorType(entry.type);
        b.descriptorCount = entry.count;
        b.stageFlags = toVkShaderStage(entry.stageFlags);
        b.pImmutableSamplers = nullptr;
        vkBindings.push_back(b);
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(vkBindings.size());
    layoutInfo.pBindings = vkBindings.data();

    if (vkCreateDescriptorSetLayout(device_->getVkDevice(), &layoutInfo, nullptr, &layout_) != VK_SUCCESS) {
        throw std::runtime_error("[VulkanRHIBindingLayout] Failed to create descriptor set layout!");
    }
}

VulkanRHIBindingLayout::~VulkanRHIBindingLayout() {
    if (layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_->getVkDevice(), layout_, nullptr);
    }
}

// =============================================================================
// VulkanRHIBindingGroup
// =============================================================================

VulkanRHIBindingGroup::VulkanRHIBindingGroup(VulkanRHIDevice* device,
                                             VulkanRHIBindingLayout* layout,
                                             const RHIBindingGroupDesc& desc)
    : device_(device)
{
    // Allocate a descriptor set from the device's auto-growing pool
    descriptorSet_ = device_->allocateDescriptorSet(layout->getVkDescriptorSetLayout());

    // Write actual resource bindings
    writeDescriptors(desc, layout);
}

void VulkanRHIBindingGroup::writeDescriptors(const RHIBindingGroupDesc& desc,
                                              const VulkanRHIBindingLayout* layout)
{
    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(desc.entries.size());

    // Keep alive until vkUpdateDescriptorSets completes
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo>  imageInfos;
    bufferInfos.reserve(desc.entries.size());
    imageInfos.reserve(desc.entries.size());

    const auto& layoutEntries = layout->getDesc().entries;

    for (const auto& entry : desc.entries) {
        // Find matching layout entry to determine descriptor type
        RHIDescriptorType descType = RHIDescriptorType::UniformBuffer;
        for (const auto& le : layoutEntries) {
            if (le.binding == entry.binding) {
                descType = le.type;
                break;
            }
        }

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptorSet_;
        write.dstBinding = entry.binding;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = toVkDescriptorType(descType);

        if (entry.kind == RHIBindingGroupEntry::Kind::Buffer) {
            auto* vkBuffer = static_cast<VulkanRHIBuffer*>(entry.bufferBinding.buffer);
            VkDescriptorBufferInfo bufInfo{};
            bufInfo.buffer = vkBuffer->getVkBuffer();
            bufInfo.offset = entry.bufferBinding.offset;
            bufInfo.range = (entry.bufferBinding.range == 0) ? VK_WHOLE_SIZE : entry.bufferBinding.range;
            bufferInfos.push_back(bufInfo);
            write.pBufferInfo = &bufferInfos.back();
        } else {
            auto* vkTexture = static_cast<VulkanRHITexture*>(entry.textureBinding.texture);
            VkDescriptorImageInfo imgInfo{};
            imgInfo.imageView = vkTexture->getVkImageView();
            imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            if (entry.textureBinding.sampler) {
                auto* vkSampler = static_cast<VulkanRHISampler*>(entry.textureBinding.sampler);
                imgInfo.sampler = vkSampler->getVkSampler();
            }
            imageInfos.push_back(imgInfo);
            write.pImageInfo = &imageInfos.back();
        }

        writes.push_back(write);
    }

    vkUpdateDescriptorSets(device_->getVkDevice(),
                           static_cast<uint32_t>(writes.size()),
                           writes.data(), 0, nullptr);
}
