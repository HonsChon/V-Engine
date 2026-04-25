#include "SSAOPass.h"
#include "VulkanDevice.h"
#include "GBufferPass.h"
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <cstring>
#include <random>
#include <cmath>

// ============================================================
// Constructor / Destructor
// ============================================================

SSAOPass::SSAOPass(std::shared_ptr<VulkanDevice> deviceIn, uint32_t width, uint32_t height)
    : RenderPassBase(deviceIn, width, height)
{
    passName = "SSAO Pass";
    init();
    std::cout << "[SSAOPass] Created (" << width << "x" << height << ")\n";
}

SSAOPass::~SSAOPass() {
    cleanup();
}

void SSAOPass::init() {
    m_subWidth = (width + DEINTERLEAVE_FACTOR - 1) / DEINTERLEAVE_FACTOR;
    m_subHeight = (height + DEINTERLEAVE_FACTOR - 1) / DEINTERLEAVE_FACTOR;

    generateKernel();
    generateLayerRotations();
    createSamplers();
    createDeinterleavedTextures();
    createAOTextures();
    createDeinterleaveResources();
    createSSAOResources();
    createReinterleaveResources();
    createBlurResources();
}

void SSAOPass::cleanup() {
    VkDevice vkDev = device->getDevice();
    vkDeviceWaitIdle(vkDev);

    // Blur
    if (m_blurPipeline) { vkDestroyPipeline(vkDev, m_blurPipeline, nullptr); m_blurPipeline = VK_NULL_HANDLE; }
    if (m_blurPipelineLayout) { vkDestroyPipelineLayout(vkDev, m_blurPipelineLayout, nullptr); m_blurPipelineLayout = VK_NULL_HANDLE; }
    if (m_blurFramebuffer) { vkDestroyFramebuffer(vkDev, m_blurFramebuffer, nullptr); m_blurFramebuffer = VK_NULL_HANDLE; }
    if (m_blurRenderPass) { vkDestroyRenderPass(vkDev, m_blurRenderPass, nullptr); m_blurRenderPass = VK_NULL_HANDLE; }
    if (m_blurDescPool) { vkDestroyDescriptorPool(vkDev, m_blurDescPool, nullptr); m_blurDescPool = VK_NULL_HANDLE; }
    if (m_blurDescSetLayout) { vkDestroyDescriptorSetLayout(vkDev, m_blurDescSetLayout, nullptr); m_blurDescSetLayout = VK_NULL_HANDLE; }

    // Reinterleave
    if (m_reinterleavePipeline) { vkDestroyPipeline(vkDev, m_reinterleavePipeline, nullptr); m_reinterleavePipeline = VK_NULL_HANDLE; }
    if (m_reinterleavePipelineLayout) { vkDestroyPipelineLayout(vkDev, m_reinterleavePipelineLayout, nullptr); m_reinterleavePipelineLayout = VK_NULL_HANDLE; }
    if (m_reinterleaveDescPool) { vkDestroyDescriptorPool(vkDev, m_reinterleaveDescPool, nullptr); m_reinterleaveDescPool = VK_NULL_HANDLE; }
    if (m_reinterleaveDescSetLayout) { vkDestroyDescriptorSetLayout(vkDev, m_reinterleaveDescSetLayout, nullptr); m_reinterleaveDescSetLayout = VK_NULL_HANDLE; }

    // SSAO
    if (m_ssaoPipeline) { vkDestroyPipeline(vkDev, m_ssaoPipeline, nullptr); m_ssaoPipeline = VK_NULL_HANDLE; }
    if (m_ssaoPipelineLayout) { vkDestroyPipelineLayout(vkDev, m_ssaoPipelineLayout, nullptr); m_ssaoPipelineLayout = VK_NULL_HANDLE; }
    for (auto& fb : m_ssaoFramebuffers) { if (fb) { vkDestroyFramebuffer(vkDev, fb, nullptr); fb = VK_NULL_HANDLE; } }
    if (m_ssaoRenderPass) { vkDestroyRenderPass(vkDev, m_ssaoRenderPass, nullptr); m_ssaoRenderPass = VK_NULL_HANDLE; }
    if (m_ssaoDescPool) { vkDestroyDescriptorPool(vkDev, m_ssaoDescPool, nullptr); m_ssaoDescPool = VK_NULL_HANDLE; }
    if (m_ssaoDescSetLayout) { vkDestroyDescriptorSetLayout(vkDev, m_ssaoDescSetLayout, nullptr); m_ssaoDescSetLayout = VK_NULL_HANDLE; }
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (m_ssaoUBOs[i]) { vkDestroyBuffer(vkDev, m_ssaoUBOs[i], nullptr); m_ssaoUBOs[i] = VK_NULL_HANDLE; }
        if (m_ssaoUBOMemory[i]) { vkFreeMemory(vkDev, m_ssaoUBOMemory[i], nullptr); m_ssaoUBOMemory[i] = VK_NULL_HANDLE; }
    }

    // Deinterleave
    if (m_deinterleavePipeline) { vkDestroyPipeline(vkDev, m_deinterleavePipeline, nullptr); m_deinterleavePipeline = VK_NULL_HANDLE; }
    if (m_deinterleavePipelineLayout) { vkDestroyPipelineLayout(vkDev, m_deinterleavePipelineLayout, nullptr); m_deinterleavePipelineLayout = VK_NULL_HANDLE; }
    if (m_deinterleaveDescPool) { vkDestroyDescriptorPool(vkDev, m_deinterleaveDescPool, nullptr); m_deinterleaveDescPool = VK_NULL_HANDLE; }
    if (m_deinterleaveDescSetLayout) { vkDestroyDescriptorSetLayout(vkDev, m_deinterleaveDescSetLayout, nullptr); m_deinterleaveDescSetLayout = VK_NULL_HANDLE; }

    // AO layer views
    for (auto& v : m_aoArrayLayerViews) { if (v) { vkDestroyImageView(vkDev, v, nullptr); v = VK_NULL_HANDLE; } }

    // Images and views
    auto destroyImg = [&](VkImage& img, VkDeviceMemory& mem, VkImageView& view) {
        if (view) { vkDestroyImageView(vkDev, view, nullptr); view = VK_NULL_HANDLE; }
        if (img) { vkDestroyImage(vkDev, img, nullptr); img = VK_NULL_HANDLE; }
        if (mem) { vkFreeMemory(vkDev, mem, nullptr); mem = VK_NULL_HANDLE; }
    };

    destroyImg(m_deinterleavedPositionImage, m_deinterleavedPositionMemory, m_deinterleavedPositionView);
    destroyImg(m_deinterleavedNormalImage, m_deinterleavedNormalMemory, m_deinterleavedNormalView);
    destroyImg(m_aoArrayImage, m_aoArrayMemory, m_aoArrayView);
    destroyImg(m_fullAOImage, m_fullAOMemory, m_fullAOView);
    destroyImg(m_blurredAOImage, m_blurredAOMemory, m_blurredAOView);

    // Samplers
    if (m_aoSampler) { vkDestroySampler(vkDev, m_aoSampler, nullptr); m_aoSampler = VK_NULL_HANDLE; }
    if (m_deinterleaveSampler) { vkDestroySampler(vkDev, m_deinterleaveSampler, nullptr); m_deinterleaveSampler = VK_NULL_HANDLE; }
}

// ============================================================
// Kernel and Rotation Generation
// ============================================================

void SSAOPass::generateKernel() {
    std::default_random_engine rng(42);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    for (int i = 0; i < KERNEL_SIZE; ++i) {
        glm::vec3 sample(
            dist(rng) * 2.0f - 1.0f,
            dist(rng) * 2.0f - 1.0f,
            dist(rng)  // z in [0,1] for hemisphere
        );
        sample = glm::normalize(sample);
        sample *= dist(rng);

        // Accelerating interpolation: more samples near origin
        float scale = float(i) / float(KERNEL_SIZE);
        scale = 0.1f + scale * scale * (1.0f - 0.1f); // lerp(0.1, 1.0, scale^2)
        sample *= scale;

        m_kernel[i] = glm::vec4(sample, 0.0f);
    }
}

void SSAOPass::generateLayerRotations() {
    std::default_random_engine rng(123);
    std::uniform_real_distribution<float> dist(0.0f, 2.0f * 3.14159265358979f);

    for (int i = 0; i < NUM_LAYERS; ++i) {
        m_layerRotations[i] = dist(rng);
    }
}

// ============================================================
// Sampler Creation
// ============================================================

void SSAOPass::createSamplers() {
    VkDevice vkDev = device->getDevice();

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    if (vkCreateSampler(vkDev, &samplerInfo, nullptr, &m_aoSampler) != VK_SUCCESS) {
        throw std::runtime_error("[SSAOPass] Failed to create AO sampler");
    }

    // Linear sampler for deinterleaved textures
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    if (vkCreateSampler(vkDev, &samplerInfo, nullptr, &m_deinterleaveSampler) != VK_SUCCESS) {
        throw std::runtime_error("[SSAOPass] Failed to create deinterleave sampler");
    }
}

// ============================================================
// Utility Methods
// ============================================================

uint32_t SSAOPass::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(device->getPhysicalDevice(), &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("[SSAOPass] Failed to find suitable memory type");
}

void SSAOPass::createImage2D(uint32_t w, uint32_t h, VkFormat format,
                              VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& memory) {
    VkDevice vkDev = device->getDevice();
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {w, h, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(vkDev, &imageInfo, nullptr, &image) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to create 2D image");

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(vkDev, image, &memReqs);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(vkDev, &allocInfo, nullptr, &memory) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to allocate 2D image memory");
    vkBindImageMemory(vkDev, image, memory, 0);
}

void SSAOPass::createImage2DArray(uint32_t w, uint32_t h, uint32_t layers, VkFormat format,
                                   VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& memory) {
    VkDevice vkDev = device->getDevice();
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {w, h, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = layers;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(vkDev, &imageInfo, nullptr, &image) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to create 2D array image");

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(vkDev, image, &memReqs);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(vkDev, &allocInfo, nullptr, &memory) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to allocate 2D array image memory");
    vkBindImageMemory(vkDev, image, memory, 0);
}

VkImageView SSAOPass::createImageView2D(VkImage image, VkFormat format, VkImageAspectFlags aspect) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspect;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView view;
    if (vkCreateImageView(device->getDevice(), &viewInfo, nullptr, &view) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to create 2D image view");
    return view;
}

VkImageView SSAOPass::createImageView2DArray(VkImage image, VkFormat format, uint32_t layers, VkImageAspectFlags aspect) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspect;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = layers;

    VkImageView view;
    if (vkCreateImageView(device->getDevice(), &viewInfo, nullptr, &view) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to create 2D array image view");
    return view;
}

VkImageView SSAOPass::createImageView2DArraySingleLayer(VkImage image, VkFormat format, uint32_t layer, VkImageAspectFlags aspect) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspect;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = layer;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView view;
    if (vkCreateImageView(device->getDevice(), &viewInfo, nullptr, &view) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to create single layer image view");
    return view;
}

void SSAOPass::transitionImageLayout(VkCommandBuffer cmd, VkImage image,
    VkImageLayout oldLayout, VkImageLayout newLayout,
    VkImageAspectFlags aspect, uint32_t layerCount) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = layerCount;

    VkPipelineStageFlags srcStage, dstStage;
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    } else {
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

VkShaderModule SSAOPass::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module;
    if (vkCreateShaderModule(device->getDevice(), &createInfo, nullptr, &module) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to create shader module");
    return module;
}

std::vector<char> SSAOPass::readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("[SSAOPass] Failed to open file: " + filename);
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

// ============================================================
// Texture Creation
// ============================================================

void SSAOPass::createDeinterleavedTextures() {
    VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    // Position array (16 layers)
    createImage2DArray(m_subWidth, m_subHeight, NUM_LAYERS, VK_FORMAT_R16G16B16A16_SFLOAT,
                       usage, m_deinterleavedPositionImage, m_deinterleavedPositionMemory);
    m_deinterleavedPositionView = createImageView2DArray(m_deinterleavedPositionImage,
        VK_FORMAT_R16G16B16A16_SFLOAT, NUM_LAYERS, VK_IMAGE_ASPECT_COLOR_BIT);

    // Normal array (16 layers)
    createImage2DArray(m_subWidth, m_subHeight, NUM_LAYERS, VK_FORMAT_R16G16B16A16_SFLOAT,
                       usage, m_deinterleavedNormalImage, m_deinterleavedNormalMemory);
    m_deinterleavedNormalView = createImageView2DArray(m_deinterleavedNormalImage,
        VK_FORMAT_R16G16B16A16_SFLOAT, NUM_LAYERS, VK_IMAGE_ASPECT_COLOR_BIT);
}

void SSAOPass::createAOTextures() {
    // AO array (16 layers, R8_UNORM) — SSAO output per layer
    VkImageUsageFlags aoArrayUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    createImage2DArray(m_subWidth, m_subHeight, NUM_LAYERS, VK_FORMAT_R8_UNORM,
                       aoArrayUsage, m_aoArrayImage, m_aoArrayMemory);
    m_aoArrayView = createImageView2DArray(m_aoArrayImage, VK_FORMAT_R8_UNORM, NUM_LAYERS, VK_IMAGE_ASPECT_COLOR_BIT);

    // Per-layer views for framebuffers
    for (uint32_t i = 0; i < NUM_LAYERS; ++i) {
        m_aoArrayLayerViews[i] = createImageView2DArraySingleLayer(
            m_aoArrayImage, VK_FORMAT_R8_UNORM, i, VK_IMAGE_ASPECT_COLOR_BIT);
    }

    // Full resolution AO (R8_UNORM) — reinterleave output
    VkImageUsageFlags fullAOUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    createImage2D(width, height, VK_FORMAT_R8_UNORM, fullAOUsage, m_fullAOImage, m_fullAOMemory);
    m_fullAOView = createImageView2D(m_fullAOImage, VK_FORMAT_R8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);

    // Blurred AO (R8_UNORM) — final output (TRANSFER_DST needed for vkCmdClearColorImage when SSAO disabled)
    VkImageUsageFlags blurUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    createImage2D(width, height, VK_FORMAT_R8_UNORM, blurUsage, m_blurredAOImage, m_blurredAOMemory);
    m_blurredAOView = createImageView2D(m_blurredAOImage, VK_FORMAT_R8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
}

// ============================================================
// Deinterleave Resources
// ============================================================

void SSAOPass::createDeinterleaveResources() {
    createDeinterleaveDescriptorSetLayout();
    createDeinterleaveDescriptorPool();
    createDeinterleaveDescriptorSets();
    createDeinterleavePipeline();
}

void SSAOPass::createDeinterleaveDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
    // binding 0: gPosition (sampler2D)
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    // binding 1: gNormal (sampler2D)
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    // binding 2: outPosition (storage image array)
    bindings[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    // binding 3: outNormal (storage image array)
    bindings[3] = {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device->getDevice(), &layoutInfo, nullptr, &m_deinterleaveDescSetLayout) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to create deinterleave desc set layout");
}

void SSAOPass::createDeinterleaveDescriptorPool() {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2};
    poolSizes[1] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;
    if (vkCreateDescriptorPool(device->getDevice(), &poolInfo, nullptr, &m_deinterleaveDescPool) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to create deinterleave desc pool");
}

void SSAOPass::createDeinterleaveDescriptorSets() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_deinterleaveDescPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_deinterleaveDescSetLayout;
    if (vkAllocateDescriptorSets(device->getDevice(), &allocInfo, &m_deinterleaveDescSet) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to allocate deinterleave desc set");
    // Note: will be updated in executeDeinterleave with actual GBuffer views
}

void SSAOPass::createDeinterleavePipeline() {
    VkDevice vkDev = device->getDevice();
    auto compCode = readFile("shaders/ssao/ssao_deinterleave_comp.spv");
    VkShaderModule compModule = createShaderModule(compCode);

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = compModule;
    stageInfo.pName = "main";

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(DeinterleavePushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_deinterleaveDescSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(vkDev, &layoutInfo, nullptr, &m_deinterleavePipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to create deinterleave pipeline layout");

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = m_deinterleavePipelineLayout;
    if (vkCreateComputePipelines(vkDev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_deinterleavePipeline) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to create deinterleave pipeline");

    vkDestroyShaderModule(vkDev, compModule, nullptr);
}

// ============================================================
// SSAO Resources
// ============================================================

void SSAOPass::createSSAOResources() {
    createSSAORenderPass();
    createSSAOFramebuffers();
    createSSAODescriptorSetLayout();
    createSSAODescriptorPool();
    createSSAODescriptorSets();
    createSSAOUniformBuffers();
    createSSAOPipeline();
}

void SSAOPass::createSSAORenderPass() {
    VkAttachmentDescription aoAttachment{};
    aoAttachment.format = VK_FORMAT_R8_UNORM;
    aoAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    aoAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    aoAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    aoAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    aoAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    aoAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    aoAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference aoRef{};
    aoRef.attachment = 0;
    aoRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &aoRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    dep.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &aoAttachment;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dep;
    if (vkCreateRenderPass(device->getDevice(), &rpInfo, nullptr, &m_ssaoRenderPass) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to create SSAO render pass");
}

void SSAOPass::createSSAOFramebuffers() {
    for (uint32_t i = 0; i < NUM_LAYERS; ++i) {
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = m_ssaoRenderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &m_aoArrayLayerViews[i];
        fbInfo.width = m_subWidth;
        fbInfo.height = m_subHeight;
        fbInfo.layers = 1;
        if (vkCreateFramebuffer(device->getDevice(), &fbInfo, nullptr, &m_ssaoFramebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("[SSAOPass] Failed to create SSAO framebuffer for layer " + std::to_string(i));
    }
}

void SSAOPass::createSSAODescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
    // binding 0: UBO (SSAOParams)
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    // binding 1: positionArray (sampler2DArray)
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    // binding 2: normalArray (sampler2DArray)
    bindings[2] = {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device->getDevice(), &layoutInfo, nullptr, &m_ssaoDescSetLayout) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to create SSAO desc set layout");
}

void SSAOPass::createSSAODescriptorPool() {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MAX_FRAMES_IN_FLIGHT};
    poolSizes[1] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_FRAMES_IN_FLIGHT * 2};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT;
    if (vkCreateDescriptorPool(device->getDevice(), &poolInfo, nullptr, &m_ssaoDescPool) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to create SSAO desc pool");
}

void SSAOPass::createSSAODescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, m_ssaoDescSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_ssaoDescPool;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(device->getDevice(), &allocInfo, m_ssaoDescSets.data()) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to allocate SSAO desc sets");

    // Write deinterleaved texture array views (these don't change per frame)
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        VkDescriptorImageInfo posInfo{};
        posInfo.sampler = m_deinterleaveSampler;
        posInfo.imageView = m_deinterleavedPositionView;
        posInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo norInfo{};
        norInfo.sampler = m_deinterleaveSampler;
        norInfo.imageView = m_deinterleavedNormalView;
        norInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = m_ssaoDescSets[i];
        writes[0].dstBinding = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &posInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = m_ssaoDescSets[i];
        writes[1].dstBinding = 2;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &norInfo;

        vkUpdateDescriptorSets(device->getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void SSAOPass::createSSAOUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(SSAOParamsUBO);
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        device->createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_ssaoUBOs[i], m_ssaoUBOMemory[i]);
        vkMapMemory(device->getDevice(), m_ssaoUBOMemory[i], 0, bufferSize, 0, &m_ssaoUBOMapped[i]);

        // Write UBO descriptor
        VkDescriptorBufferInfo bufInfo{};
        bufInfo.buffer = m_ssaoUBOs[i];
        bufInfo.offset = 0;
        bufInfo.range = sizeof(SSAOParamsUBO);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_ssaoDescSets[i];
        write.dstBinding = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &bufInfo;
        vkUpdateDescriptorSets(device->getDevice(), 1, &write, 0, nullptr);
    }
}

void SSAOPass::createSSAOPipeline() {
    VkDevice vkDev = device->getDevice();
    auto vertCode = readFile("shaders/ssao/ssao_vert.spv");
    auto fragCode = readFile("shaders/ssao/ssao_frag.spv");
    VkShaderModule vertModule = createShaderModule(vertCode);
    VkShaderModule fragModule = createShaderModule(fragCode);

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    // No vertex input (fullscreen triangle from gl_VertexIndex)
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    blendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttachment;

    std::vector<VkDynamicState> dynStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
    dynamicState.pDynamicStates = dynStates.data();

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(SSAOPushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_ssaoDescSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(vkDev, &layoutInfo, nullptr, &m_ssaoPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to create SSAO pipeline layout");

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_ssaoPipelineLayout;
    pipelineInfo.renderPass = m_ssaoRenderPass;
    pipelineInfo.subpass = 0;
    if (vkCreateGraphicsPipelines(vkDev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_ssaoPipeline) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to create SSAO pipeline");

    vkDestroyShaderModule(vkDev, fragModule, nullptr);
    vkDestroyShaderModule(vkDev, vertModule, nullptr);
}

// ============================================================
// Reinterleave Resources
// ============================================================

void SSAOPass::createReinterleaveResources() {
    createReinterleaveDescriptorSetLayout();
    createReinterleaveDescriptorPool();
    createReinterleaveDescriptorSets();
    createReinterleavePipeline();
}

void SSAOPass::createReinterleaveDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(device->getDevice(), &layoutInfo, nullptr, &m_reinterleaveDescSetLayout) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to create reinterleave desc set layout");
}

void SSAOPass::createReinterleaveDescriptorPool() {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    poolSizes[1] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;
    if (vkCreateDescriptorPool(device->getDevice(), &poolInfo, nullptr, &m_reinterleaveDescPool) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to create reinterleave desc pool");
}

void SSAOPass::createReinterleaveDescriptorSets() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_reinterleaveDescPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_reinterleaveDescSetLayout;
    if (vkAllocateDescriptorSets(device->getDevice(), &allocInfo, &m_reinterleaveDescSet) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to allocate reinterleave desc set");

    // Write descriptors
    VkDescriptorImageInfo aoArrayInfo{};
    aoArrayInfo.sampler = m_aoSampler;
    aoArrayInfo.imageView = m_aoArrayView;
    aoArrayInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo fullAOInfo{};
    fullAOInfo.imageView = m_fullAOView;
    fullAOInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = m_reinterleaveDescSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &aoArrayInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = m_reinterleaveDescSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &fullAOInfo;

    vkUpdateDescriptorSets(device->getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void SSAOPass::createReinterleavePipeline() {
    VkDevice vkDev = device->getDevice();
    auto compCode = readFile("shaders/ssao/ssao_reinterleave_comp.spv");
    VkShaderModule compModule = createShaderModule(compCode);

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = compModule;
    stageInfo.pName = "main";

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(ReinterleavePushConstants);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_reinterleaveDescSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    if (vkCreatePipelineLayout(vkDev, &layoutInfo, nullptr, &m_reinterleavePipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to create reinterleave pipeline layout");

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = m_reinterleavePipelineLayout;
    if (vkCreateComputePipelines(vkDev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_reinterleavePipeline) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to create reinterleave pipeline");

    vkDestroyShaderModule(vkDev, compModule, nullptr);
}

// ============================================================
// Blur Resources
// ============================================================

void SSAOPass::createBlurResources() {
    createBlurRenderPass();
    createBlurFramebuffer();
    createBlurDescriptorSetLayout();
    createBlurDescriptorPool();
    createBlurDescriptorSets();
    createBlurPipeline();
}

void SSAOPass::createBlurRenderPass() {
    VkAttachmentDescription att{};
    att.format = VK_FORMAT_R8_UNORM;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &ref;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    dep.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &att;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dep;
    if (vkCreateRenderPass(device->getDevice(), &rpInfo, nullptr, &m_blurRenderPass) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to create blur render pass");
}

void SSAOPass::createBlurFramebuffer() {
    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = m_blurRenderPass;
    fbInfo.attachmentCount = 1;
    fbInfo.pAttachments = &m_blurredAOView;
    fbInfo.width = width;
    fbInfo.height = height;
    fbInfo.layers = 1;
    if (vkCreateFramebuffer(device->getDevice(), &fbInfo, nullptr, &m_blurFramebuffer) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to create blur framebuffer");
}

void SSAOPass::createBlurDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device->getDevice(), &layoutInfo, nullptr, &m_blurDescSetLayout) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to create blur desc set layout");
}

void SSAOPass::createBlurDescriptorPool() {
    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;
    if (vkCreateDescriptorPool(device->getDevice(), &poolInfo, nullptr, &m_blurDescPool) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to create blur desc pool");
}

void SSAOPass::createBlurDescriptorSets() {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_blurDescPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_blurDescSetLayout;
    if (vkAllocateDescriptorSets(device->getDevice(), &allocInfo, &m_blurDescSet) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to allocate blur desc set");

    VkDescriptorImageInfo imgInfo{};
    imgInfo.sampler = m_aoSampler;
    imgInfo.imageView = m_fullAOView;
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_blurDescSet;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imgInfo;
    vkUpdateDescriptorSets(device->getDevice(), 1, &write, 0, nullptr);
}

void SSAOPass::createBlurPipeline() {
    VkDevice vkDev = device->getDevice();
    auto vertCode = readFile("shaders/ssao/ssao_blur_vert.spv");
    auto fragCode = readFile("shaders/ssao/ssao_blur_frag.spv");
    VkShaderModule vertModule = createShaderModule(vertCode);
    VkShaderModule fragModule = createShaderModule(fragCode);

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragModule;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

    VkPipelineColorBlendAttachmentState blendAtt{};
    blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAtt;

    std::vector<VkDynamicState> dynStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
    dynamicState.pDynamicStates = dynStates.data();

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_blurDescSetLayout;
    if (vkCreatePipelineLayout(vkDev, &layoutInfo, nullptr, &m_blurPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to create blur pipeline layout");

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_blurPipelineLayout;
    pipelineInfo.renderPass = m_blurRenderPass;
    pipelineInfo.subpass = 0;
    if (vkCreateGraphicsPipelines(vkDev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_blurPipeline) != VK_SUCCESS)
        throw std::runtime_error("[SSAOPass] Failed to create blur pipeline");

    vkDestroyShaderModule(vkDev, fragModule, nullptr);
    vkDestroyShaderModule(vkDev, vertModule, nullptr);
}

// ============================================================
// Execute
// ============================================================

void SSAOPass::execute(VkCommandBuffer cmd, GBufferPass* gbuffer, uint32_t frameIndex,
                        const glm::mat4& projection, const glm::mat4& view) {
    if (!gbuffer) return;

    // Update UBO
    SSAOParamsUBO ubo{};
    for (int i = 0; i < KERNEL_SIZE; ++i) ubo.samples[i] = m_kernel[i];
    ubo.projection = projection;
    ubo.view = view;
    ubo.radius = m_settings.radius;
    ubo.bias = m_settings.bias;
    ubo.power = m_settings.power;
    ubo.kernelSize = m_settings.kernelSize;
    memcpy(m_ssaoUBOMapped[frameIndex], &ubo, sizeof(ubo));

    device->beginDebugLabel(cmd, "SSAO Pass", 0.6f, 0.8f, 0.2f);

    // Stage 1: Deinterleave
    device->beginDebugLabel(cmd, "SSAO Deinterleave", 0.6f, 0.6f, 0.2f);
    executeDeinterleave(cmd, gbuffer);
    device->endDebugLabel(cmd);

    // Stage 2: SSAO Compute (16 layers)
    device->beginDebugLabel(cmd, "SSAO Compute", 0.6f, 0.4f, 0.2f);
    executeSSAO(cmd, frameIndex);
    device->endDebugLabel(cmd);

    // Stage 3: Reinterleave
    device->beginDebugLabel(cmd, "SSAO Reinterleave", 0.6f, 0.2f, 0.4f);
    executeReinterleave(cmd);
    device->endDebugLabel(cmd);

    // Stage 4: Blur
    device->beginDebugLabel(cmd, "SSAO Blur", 0.6f, 0.2f, 0.6f);
    executeBlur(cmd);
    device->endDebugLabel(cmd);

    device->endDebugLabel(cmd);
}

void SSAOPass::executeDeinterleave(VkCommandBuffer cmd, GBufferPass* gbuffer) {
    // Update descriptor set with current GBuffer views
    VkDescriptorImageInfo posInfo{};
    posInfo.sampler = m_aoSampler;
    posInfo.imageView = gbuffer->getPositionView();
    posInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo norInfo{};
    norInfo.sampler = m_aoSampler;
    norInfo.imageView = gbuffer->getNormalView();
    norInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo outPosInfo{};
    outPosInfo.imageView = m_deinterleavedPositionView;
    outPosInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo outNorInfo{};
    outNorInfo.imageView = m_deinterleavedNormalView;
    outNorInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    std::array<VkWriteDescriptorSet, 4> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = m_deinterleaveDescSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &posInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = m_deinterleaveDescSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &norInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = m_deinterleaveDescSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].descriptorCount = 1;
    writes[2].pImageInfo = &outPosInfo;

    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = m_deinterleaveDescSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[3].descriptorCount = 1;
    writes[3].pImageInfo = &outNorInfo;

    vkUpdateDescriptorSets(device->getDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    // Transition deinterleaved images to GENERAL for compute write
    transitionImageLayout(cmd, m_deinterleavedPositionImage, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT, NUM_LAYERS);
    transitionImageLayout(cmd, m_deinterleavedNormalImage, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT, NUM_LAYERS);

    // Dispatch
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_deinterleavePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_deinterleavePipelineLayout,
                            0, 1, &m_deinterleaveDescSet, 0, nullptr);

    DeinterleavePushConstants pc{};
    pc.fullWidth = static_cast<int>(width);
    pc.fullHeight = static_cast<int>(height);
    vkCmdPushConstants(cmd, m_deinterleavePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(pc), &pc);

    uint32_t groupsX = (m_subWidth + 7) / 8;
    uint32_t groupsY = (m_subHeight + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, NUM_LAYERS);

    // Transition deinterleaved to SHADER_READ_ONLY for SSAO fragment shader
    transitionImageLayout(cmd, m_deinterleavedPositionImage, VK_IMAGE_LAYOUT_GENERAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, NUM_LAYERS);
    transitionImageLayout(cmd, m_deinterleavedNormalImage, VK_IMAGE_LAYOUT_GENERAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, NUM_LAYERS);
}

void SSAOPass::executeSSAO(VkCommandBuffer cmd, uint32_t frameIndex) {
    VkViewport viewport{};
    viewport.width = static_cast<float>(m_subWidth);
    viewport.height = static_cast<float>(m_subHeight);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.extent = {m_subWidth, m_subHeight};

    VkClearValue clearValue{};
    clearValue.color = {{1.0f, 1.0f, 1.0f, 1.0f}};  // Default: no occlusion

    for (uint32_t layer = 0; layer < NUM_LAYERS; ++layer) {
        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = m_ssaoRenderPass;
        rpBegin.framebuffer = m_ssaoFramebuffers[layer];
        rpBegin.renderArea.extent = {m_subWidth, m_subHeight};
        rpBegin.clearValueCount = 1;
        rpBegin.pClearValues = &clearValue;

        vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ssaoPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ssaoPipelineLayout,
                                0, 1, &m_ssaoDescSets[frameIndex], 0, nullptr);

        SSAOPushConstants pc{};
        pc.layerIndex = static_cast<int>(layer);
        pc.rotationAngle = m_layerRotations[layer];
        pc.subWidth = static_cast<int>(m_subWidth);
        pc.subHeight = static_cast<int>(m_subHeight);
        vkCmdPushConstants(cmd, m_ssaoPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pc), &pc);

        // Draw fullscreen triangle (3 vertices, no vertex buffer)
        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRenderPass(cmd);
    }
}

void SSAOPass::executeReinterleave(VkCommandBuffer cmd) {
    // Transition AO array to SHADER_READ_ONLY (already done by render pass finalLayout)
    // Transition fullAO to GENERAL for compute write
    transitionImageLayout(cmd, m_fullAOImage, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT, 1);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_reinterleavePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_reinterleavePipelineLayout,
                            0, 1, &m_reinterleaveDescSet, 0, nullptr);

    ReinterleavePushConstants pc{};
    pc.fullWidth = static_cast<int>(width);
    pc.fullHeight = static_cast<int>(height);
    vkCmdPushConstants(cmd, m_reinterleavePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(pc), &pc);

    uint32_t groupsX = (width + 7) / 8;
    uint32_t groupsY = (height + 7) / 8;
    vkCmdDispatch(cmd, groupsX, groupsY, 1);

    // Transition fullAO to SHADER_READ_ONLY for blur fragment shader
    transitionImageLayout(cmd, m_fullAOImage, VK_IMAGE_LAYOUT_GENERAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, 1);
}

void SSAOPass::executeBlur(VkCommandBuffer cmd) {
    VkClearValue clearValue{};
    clearValue.color = {{1.0f, 1.0f, 1.0f, 1.0f}};

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = m_blurRenderPass;
    rpBegin.framebuffer = m_blurFramebuffer;
    rpBegin.renderArea.extent = {width, height};
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = {width, height};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_blurPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_blurPipelineLayout,
                            0, 1, &m_blurDescSet, 0, nullptr);

    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmd);
}

// ============================================================
// Resize
// ============================================================

void SSAOPass::resize(uint32_t newWidth, uint32_t newHeight) {
    RenderPassBase::resize(newWidth, newHeight);
    cleanup();
    init();
    std::cout << "[SSAOPass] Resized to " << newWidth << "x" << newHeight << "\n";
}

// ============================================================
// Settings
// ============================================================

void SSAOPass::updateSettings(const SSAOSettings& settings) {
    m_settings = settings;
}
