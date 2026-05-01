#include "VulkanRHIDescriptor.h"
#include "VulkanRHIDevice.h"
#include "VulkanRHIBuffer.h"
#include "VulkanRHITexture.h"
#include "VulkanRHISampler.h"
#include "VulkanTypeConversions.h"
#include "IVulkanNative.h"
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
    : device_(device), layout_(layout)
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
            auto* native = dynamic_cast<IVulkanNativeBuffer*>(entry.bufferBinding.buffer);
            VkDescriptorBufferInfo bufInfo{};
            bufInfo.buffer = native->getVkBuffer();
            bufInfo.offset = entry.bufferBinding.offset;
            bufInfo.range = (entry.bufferBinding.range == 0) ? VK_WHOLE_SIZE : entry.bufferBinding.range;
            bufferInfos.push_back(bufInfo);
            write.pBufferInfo = &bufferInfos.back();
        } else {
            auto* nativeTex = dynamic_cast<IVulkanNativeTexture*>(entry.textureBinding.texture);
            VkDescriptorImageInfo imgInfo{};
            imgInfo.imageView = nativeTex->getVkImageView();

            // Use correct layout based on descriptor type and texture format
            if (descType == RHIDescriptorType::StorageImage) {
                imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            } else {
                RHIFormat fmt = entry.textureBinding.texture->getFormat();
                bool isDepth = (fmt == RHIFormat::D16_UNORM || fmt == RHIFormat::D32_SFLOAT ||
                                fmt == RHIFormat::D24_UNORM_S8_UINT || fmt == RHIFormat::D32_SFLOAT_S8_UINT);
                imgInfo.imageLayout = isDepth
                    ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
                    : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }

            if (entry.textureBinding.sampler) {
                auto* nativeSampler = dynamic_cast<IVulkanNativeSampler*>(entry.textureBinding.sampler);
                imgInfo.sampler = nativeSampler->getVkSampler();
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

void VulkanRHIBindingGroup::updateBuffer(uint32_t binding, RHIBuffer* buffer,
                                          uint64_t offset, uint64_t range) {
    auto* native = dynamic_cast<IVulkanNativeBuffer*>(buffer);
    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = native->getVkBuffer();
    bufInfo.offset = offset;
    bufInfo.range = (range == 0) ? VK_WHOLE_SIZE : range;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet_;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &bufInfo;

    vkUpdateDescriptorSets(device_->getVkDevice(), 1, &write, 0, nullptr);
}

void VulkanRHIBindingGroup::updateTexture(uint32_t binding, RHITexture* texture,
                                           RHISampler* sampler) {
    // Skip update if texture is null or doesn't implement the native interface
    // (prevents Vulkan validation error for VK_NULL_HANDLE imageView)
    auto* nativeTex = texture ? dynamic_cast<IVulkanNativeTexture*>(texture) : nullptr;
    if (!nativeTex || nativeTex->getVkImageView() == VK_NULL_HANDLE) {
        return;  // Cannot bind a null texture — caller must provide a valid one
    }

    // Look up the actual descriptor type from the layout
    VkDescriptorType vkDescType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    if (layout_) {
        for (const auto& entry : layout_->getDesc().entries) {
            if (entry.binding == binding) {
                vkDescType = toVkDescriptorType(entry.type);
                break;
            }
        }
    }

    VkDescriptorImageInfo imgInfo{};
    imgInfo.imageView = nativeTex->getVkImageView();

    if (vkDescType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
        // Storage images use GENERAL layout and don't need a sampler
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    } else {
        // Determine correct layout based on texture format
        RHIFormat fmt = texture->getFormat();
        bool isDepth = (fmt == RHIFormat::D16_UNORM || fmt == RHIFormat::D32_SFLOAT ||
                        fmt == RHIFormat::D24_UNORM_S8_UINT || fmt == RHIFormat::D32_SFLOAT_S8_UINT);
        imgInfo.imageLayout = isDepth
            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
            : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        if (sampler) {
            auto* nativeSampler = dynamic_cast<IVulkanNativeSampler*>(sampler);
            imgInfo.sampler = nativeSampler ? nativeSampler->getVkSampler() : VK_NULL_HANDLE;
        }
    }

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet_;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = vkDescType;
    write.pImageInfo = &imgInfo;

    vkUpdateDescriptorSets(device_->getVkDevice(), 1, &write, 0, nullptr);
}
