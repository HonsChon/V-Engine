#include "GBufferPass.h"
#include "../core/VulkanDevice.h"
#include <stdexcept>
#include <iostream>
#include <fstream>

GBufferPass::GBufferPass(std::shared_ptr<VulkanDevice> device, uint32_t width, uint32_t height)
    : RenderPassBase(device, width, height)
    , device(device)
    , width(width)
    , height(height) {
    
    passName = "GBuffer Pass";
    
    createAttachments();
    createRenderPass();
    createFramebuffer();
    createSampler();
    createDescriptorSetLayout();
    createPipeline();
    
    std::cout << "GBufferPass created: " << width << "x" << height << std::endl;
}

GBufferPass::~GBufferPass() {
    cleanup();
}

void GBufferPass::resize(uint32_t newWidth, uint32_t newHeight) {
    if (newWidth == width && newHeight == height) {
        return;
    }
    
    vkDeviceWaitIdle(device->getDevice());
    
    cleanup();
    
    width = newWidth;
    height = newHeight;
    
    createAttachments();
    createRenderPass();
    createFramebuffer();
    createSampler();
    
    std::cout << "GBufferPass resized: " << width << "x" << height << std::endl;
}

void GBufferPass::cleanup() {
    VkDevice dev = device->getDevice();
    
    // 销毁 Uniform Buffers
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (uniformBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(dev, uniformBuffers[i], nullptr);
            uniformBuffers[i] = VK_NULL_HANDLE;
        }
        if (uniformBuffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(dev, uniformBuffersMemory[i], nullptr);
            uniformBuffersMemory[i] = VK_NULL_HANDLE;
        }
        uniformBuffersMapped[i] = nullptr;
    }
    
    // 清空材质描述符缓存
    materialDescriptorCache.clear();
    
    // 销毁描述符池（描述符集会自动释放）
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(dev, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
        for (auto& ds : globalDescriptorSets) {
            ds = VK_NULL_HANDLE;
        }
    }
    
    // 销毁 Pipeline
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(dev, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(dev, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    
    if (globalSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(dev, globalSetLayout, nullptr);
        globalSetLayout = VK_NULL_HANDLE;
    }
    
    if (materialSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(dev, materialSetLayout, nullptr);
        materialSetLayout = VK_NULL_HANDLE;
    }
    
    if (sampler != VK_NULL_HANDLE) {
        vkDestroySampler(dev, sampler, nullptr);
        sampler = VK_NULL_HANDLE;
    }
    
    if (framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(dev, framebuffer, nullptr);
        framebuffer = VK_NULL_HANDLE;
    }
    
    if (renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(dev, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }
    
    for (int i = 0; i < COUNT; i++) {
        if (attachmentViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(dev, attachmentViews[i], nullptr);
            attachmentViews[i] = VK_NULL_HANDLE;
        }
        if (attachmentImages[i] != VK_NULL_HANDLE) {
            vkDestroyImage(dev, attachmentImages[i], nullptr);
            attachmentImages[i] = VK_NULL_HANDLE;
        }
        if (attachmentMemories[i] != VK_NULL_HANDLE) {
            vkFreeMemory(dev, attachmentMemories[i], nullptr);
            attachmentMemories[i] = VK_NULL_HANDLE;
        }
    }
}

void GBufferPass::createAttachments() {
    // Position - 世界空间位置
    createImage(attachmentFormats[POSITION], 
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT, POSITION);
    
    // Normal - 世界空间法线
    createImage(attachmentFormats[NORMAL],
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT, NORMAL);
    
    // Albedo - 反照�?+ 金属�?
    createImage(attachmentFormats[ALBEDO],
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT, ALBEDO);
    
    // Depth - 深度缓冲
    createImage(attachmentFormats[DEPTH],
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT, DEPTH);
}

void GBufferPass::createImage(VkFormat format, VkImageUsageFlags usage, 
                          VkImageAspectFlags aspect, uint32_t index) {
    VkDevice dev = device->getDevice();
    
    // 创建图像
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    
    if (vkCreateImage(dev, &imageInfo, nullptr, &attachmentImages[index]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create GBuffer image!");
    }
    
    // 分配内存
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(dev, attachmentImages[index], &memRequirements);
    
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    if (vkAllocateMemory(dev, &allocInfo, nullptr, &attachmentMemories[index]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate GBuffer image memory!");
    }
    
    vkBindImageMemory(dev, attachmentImages[index], attachmentMemories[index], 0);
    
    // 创建图像视图
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = attachmentImages[index];
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspect;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    
    if (vkCreateImageView(dev, &viewInfo, nullptr, &attachmentViews[index]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create GBuffer image view!");
    }
}

void GBufferPass::createRenderPass() {
    // 附件描述
    std::array<VkAttachmentDescription, COUNT> attachments{};
    
    // Position 附件
    attachments[POSITION].format = attachmentFormats[POSITION];
    attachments[POSITION].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[POSITION].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[POSITION].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[POSITION].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[POSITION].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[POSITION].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[POSITION].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    // Normal 附件
    attachments[NORMAL].format = attachmentFormats[NORMAL];
    attachments[NORMAL].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[NORMAL].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[NORMAL].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[NORMAL].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[NORMAL].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[NORMAL].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[NORMAL].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    // Albedo 附件
    attachments[ALBEDO].format = attachmentFormats[ALBEDO];
    attachments[ALBEDO].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[ALBEDO].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[ALBEDO].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[ALBEDO].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[ALBEDO].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[ALBEDO].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[ALBEDO].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    
    // Depth 附件
    attachments[DEPTH].format = attachmentFormats[DEPTH];
    attachments[DEPTH].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[DEPTH].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[DEPTH].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[DEPTH].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[DEPTH].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[DEPTH].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[DEPTH].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    
    // 颜色附件引用
    std::array<VkAttachmentReference, 3> colorRefs{};
    colorRefs[0] = { POSITION, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    colorRefs[1] = { NORMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    colorRefs[2] = { ALBEDO, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    
    // 深度附件引用
    VkAttachmentReference depthRef{};
    depthRef.attachment = DEPTH;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    
    // 子通道
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
    subpass.pColorAttachments = colorRefs.data();
    subpass.pDepthStencilAttachment = &depthRef;
    
    // 子通道依赖
    std::array<VkSubpassDependency, 2> dependencies{};
    
    // 外部 -> 子通道 0
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | 
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | 
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    
    // 子通道 0 -> 外部
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | 
                                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | 
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    
    // 创建渲染通道
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();
    
    if (vkCreateRenderPass(device->getDevice(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create GBuffer render pass!");
    }
}

void GBufferPass::createFramebuffer() {
    std::array<VkImageView, COUNT> attachmentViewsArray = {
        attachmentViews[POSITION],
        attachmentViews[NORMAL],
        attachmentViews[ALBEDO],
        attachmentViews[DEPTH]
    };
    
    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachmentViewsArray.size());
    framebufferInfo.pAttachments = attachmentViewsArray.data();
    framebufferInfo.width = width;
    framebufferInfo.height = height;
    framebufferInfo.layers = 1;
    
    if (vkCreateFramebuffer(device->getDevice(), &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create GBuffer framebuffer!");
    }
}

void GBufferPass::createSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;
    
    if (vkCreateSampler(device->getDevice(), &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create GBuffer sampler!");
    }
}

void GBufferPass::beginRenderPass(VkCommandBuffer cmd) {
    auto clearValues = getClearValues();
    
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea.offset = { 0, 0 };
    renderPassInfo.renderArea.extent = { width, height };
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();
    
    vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    
    // 设置视口和裁�?
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    
    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = { width, height };
    vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void GBufferPass::endRenderPass(VkCommandBuffer cmd) {
    vkCmdEndRenderPass(cmd);
}

std::array<VkClearValue, 4> GBufferPass::getClearValues() const {
    std::array<VkClearValue, 4> clearValues{};
    
    // Position - 清除为黑色（远处�?
    clearValues[0].color = {{ 0.0f, 0.0f, 0.0f, 0.0f }};
    
    // Normal - 清除为黑�?
    clearValues[1].color = {{ 0.0f, 0.0f, 0.0f, 0.0f }};
    
    // Albedo - 清除为黑�?
    clearValues[2].color = {{ 0.0f, 0.0f, 0.0f, 0.0f }};
    
    // Depth - 清除为最�?
    clearValues[3].depthStencil = { 1.0f, 0 };
    
    return clearValues;
}

uint32_t GBufferPass::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(device->getPhysicalDevice(), &memProperties);
    
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && 
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    
    throw std::runtime_error("Failed to find suitable memory type!");
}

// ============================================
// G-Buffer Pipeline 创建
// ============================================

void GBufferPass::createDescriptorSetLayout() {
    VkDevice dev = device->getDevice();
    
    // ========== Set 0: 全局 UBO ==========
    {
        VkDescriptorSetLayoutBinding uboBinding{};
        uboBinding.binding = 0;
        uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboBinding.descriptorCount = 1;
        uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        uboBinding.pImmutableSamplers = nullptr;
        
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &uboBinding;
        
        if (vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &globalSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create GBuffer global descriptor set layout!");
        }
    }
    
    // ========== Set 1: 材质纹理 ==========
    {
        std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
        
        // binding 0: Albedo 贴图
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[0].pImmutableSamplers = nullptr;
        
        // binding 1: Normal 贴图
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].pImmutableSamplers = nullptr;
        
        // binding 2: Specular 贴图
        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[2].pImmutableSamplers = nullptr;
        
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        
        if (vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &materialSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create GBuffer material descriptor set layout!");
        }
    }
    
    std::cout << "GBuffer descriptor set layouts created (Set 0: UBO, Set 1: Material)" << std::endl;
}

void GBufferPass::createPipeline() {
    VkDevice dev = device->getDevice();
    
    // 读取着色器
    auto vertShaderCode = readFile("shaders/gbuffer_vert.spv");
    auto fragShaderCode = readFile("shaders/gbuffer_frag.spv");
    
    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);
    
    // 着色器阶段
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";
    
    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";
    
    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };
    
    // 顶点输入
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(float) * 11; // pos(3) + normal(3) + texCoord(2) + tangent(3)
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    
    std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions{};
    
    // Position
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = 0;
    
    // Normal
    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = sizeof(float) * 3;
    
    // TexCoord
    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = sizeof(float) * 6;
    
    // Tangent
    attributeDescriptions[3].binding = 0;
    attributeDescriptions[3].location = 3;
    attributeDescriptions[3].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[3].offset = sizeof(float) * 8;
    
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
    
    // 输入装配
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    // 视口（动态状态）
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    
    // 光栅�?
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    
    // 多重采样
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    // 深度测试
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    
    // 颜色混合 - 3个颜色附件，无混�?
    std::array<VkPipelineColorBlendAttachmentState, 3> colorBlendAttachments{};
    for (auto& attachment : colorBlendAttachments) {
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        attachment.blendEnable = VK_FALSE;
    }
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
    colorBlending.pAttachments = colorBlendAttachments.data();
    
    // 动态状�?
    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();
    
    // Push Constants 配置
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstantData);  // 2 个 mat4 = 128 bytes
    
    // Pipeline 布局 - 使用双描述符集
    std::array<VkDescriptorSetLayout, 2> setLayouts = { globalSetLayout, materialSetLayout };
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = setLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    
    if (vkCreatePipelineLayout(dev, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create GBuffer pipeline layout!");
    }
    
    // 创建图形管线
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    
    if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create GBuffer graphics pipeline!");
    }
    
    // 销毁着色器模块
    vkDestroyShaderModule(dev, fragShaderModule, nullptr);
    vkDestroyShaderModule(dev, vertShaderModule, nullptr);
    
    std::cout << "GBuffer pipeline created successfully!" << std::endl;
}

VkShaderModule GBufferPass::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device->getDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module!");
    }
    
    return shaderModule;
}

std::vector<char> GBufferPass::readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filename);
    }
    
    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    
    return buffer;
}

// ============================================
// recordCommands 实现
// ============================================

void GBufferPass::recordCommands(VkCommandBuffer cmd, uint32_t frameIndex) {
    // 使用当前存储�?context 录制命令
    recordCommands(cmd, currentContext);
}

void GBufferPass::recordCommands(VkCommandBuffer cmd, const RenderContext& context) {
    if (!enabled) return;
    
    // 开�?G-Buffer 渲染通道
    beginRenderPass(cmd);
    
    // 绑定 G-Buffer Pipeline
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    
    // 绑定描述符集（如果已设置�?
    if (currentDescriptorSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                               pipelineLayout, 0, 1, 
                               &currentDescriptorSet, 0, nullptr);
    }
    
    // 绑定顶点和索引缓冲并绘制
    if (context.sceneVertexBuffer != VK_NULL_HANDLE && 
        context.sceneIndexBuffer != VK_NULL_HANDLE && 
        context.sceneIndexCount > 0) {
        
        VkBuffer vertexBuffers[] = { context.sceneVertexBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, context.sceneIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, context.sceneIndexCount, 1, 0, 0, 0);
    }
    
    // 结束 G-Buffer 渲染通道
    endRenderPass(cmd);
}

// ============================================
// 描述符集创建和更�?
// ============================================

void GBufferPass::createUniformBuffers() {
    VkDevice dev = device->getDevice();
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);
    
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        // 创建 Buffer
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateBuffer(dev, &bufferInfo, nullptr, &uniformBuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create GBuffer uniform buffer!");
        }
        
        // 分配内存
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(dev, uniformBuffers[i], &memRequirements);
        
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        
        if (vkAllocateMemory(dev, &allocInfo, nullptr, &uniformBuffersMemory[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate GBuffer uniform buffer memory!");
        }
        
        vkBindBufferMemory(dev, uniformBuffers[i], uniformBuffersMemory[i], 0);
        
        // 映射内存
        vkMapMemory(dev, uniformBuffersMemory[i], 0, bufferSize, 0, &uniformBuffersMapped[i]);
    }
    
    std::cout << "GBuffer uniform buffers created" << std::endl;
}

void GBufferPass::createDescriptorSets() {
    VkDevice dev = device->getDevice();
    
    // 先创建 Uniform Buffers
    createUniformBuffers();
    
    // 如果已存在描述符池，先销毁
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(dev, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
        for (auto& ds : globalDescriptorSets) {
            ds = VK_NULL_HANDLE;
        }
        materialDescriptorCache.clear();
    }
    
    // 创建描述符池
    // Set 0: MAX_FRAMES_IN_FLIGHT 个 UBO 描述符集
    // Set 1: 每个材质需要 MAX_FRAMES_IN_FLIGHT 个描述符集，每个有 3 个纹理
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = MAX_MATERIALS * MAX_FRAMES_IN_FLIGHT * 3; // 每材质3个纹理
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT + MAX_MATERIALS * MAX_FRAMES_IN_FLIGHT;
    
    if (vkCreateDescriptorPool(dev, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create GBuffer descriptor pool!");
    }
    
    // ========== 分配全局描述符集 (Set 0) ==========
    std::vector<VkDescriptorSetLayout> globalLayouts(MAX_FRAMES_IN_FLIGHT, globalSetLayout);
    
    VkDescriptorSetAllocateInfo globalAllocInfo{};
    globalAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    globalAllocInfo.descriptorPool = descriptorPool;
    globalAllocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    globalAllocInfo.pSetLayouts = globalLayouts.data();
    
    if (vkAllocateDescriptorSets(dev, &globalAllocInfo, globalDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate GBuffer global descriptor sets!");
    }
    
    // 更新全局描述符集 - 绑定 UBO
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);
        
        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = globalDescriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;
        
        vkUpdateDescriptorSets(dev, 1, &descriptorWrite, 0, nullptr);
    }
    
    std::cout << "GBuffer global descriptor sets created and UBO bound" << std::endl;
}

void GBufferPass::updateUniformBuffer(uint32_t frameIndex, const UniformBufferObject& ubo) {
    if (frameIndex < MAX_FRAMES_IN_FLIGHT && uniformBuffersMapped[frameIndex] != nullptr) {
        memcpy(uniformBuffersMapped[frameIndex], &ubo, sizeof(ubo));
    }
}

// ============================================
// Pipeline 绑定和绘制方法
// ============================================

void GBufferPass::bindPipeline(VkCommandBuffer cmd) const {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
}

void GBufferPass::drawMesh(VkCommandBuffer cmd, VkBuffer vertexBuffer, VkBuffer indexBuffer, uint32_t indexCount) const {
    VkBuffer vertexBuffers[] = { vertexBuffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
}

void GBufferPass::pushModelMatrix(VkCommandBuffer cmd, const glm::mat4& model) {
    PushConstantData pushData{};
    pushData.model = model;
    pushData.normalMatrix = glm::transpose(glm::inverse(model));
    
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 
                       0, sizeof(PushConstantData), &pushData);
}

// ============================================
// 材质描述符管理
// ============================================

GBufferPass::MaterialDescriptor* GBufferPass::allocateMaterialDescriptor(const std::string& materialId) {
    // 检查是否已存在
    auto it = materialDescriptorCache.find(materialId);
    if (it != materialDescriptorCache.end()) {
        return &it->second;
    }
    
    if (descriptorPool == VK_NULL_HANDLE) {
        std::cerr << "GBuffer: Cannot allocate material descriptor - descriptor pool not created!" << std::endl;
        return nullptr;
    }
    
    VkDevice dev = device->getDevice();
    
    // 分配新的描述符集
    MaterialDescriptor material;
    material.sets.resize(MAX_FRAMES_IN_FLIGHT);
    
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, materialSetLayout);
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts = layouts.data();
    
    if (vkAllocateDescriptorSets(dev, &allocInfo, material.sets.data()) != VK_SUCCESS) {
        std::cerr << "GBuffer: Failed to allocate material descriptor sets for: " << materialId << std::endl;
        return nullptr;
    }
    
    material.valid = true;
    materialDescriptorCache[materialId] = std::move(material);
    
    std::cout << "GBuffer: Allocated material descriptor for: " << materialId << std::endl;
    return &materialDescriptorCache[materialId];
}

GBufferPass::MaterialDescriptor* GBufferPass::getMaterialDescriptor(const std::string& materialId) {
    auto it = materialDescriptorCache.find(materialId);
    if (it != materialDescriptorCache.end() && it->second.valid) {
        return &it->second;
    }
    return nullptr;
}

void GBufferPass::updateMaterialTextures(MaterialDescriptor* material,
                                         VkImageView albedoView, VkSampler albedoSampler,
                                         VkImageView normalView, VkSampler normalSampler,
                                         VkImageView specularView, VkSampler specularSampler) {
    if (!material || !material->valid) {
        std::cerr << "GBuffer: Cannot update textures - invalid material descriptor!" << std::endl;
        return;
    }
    
    VkDevice dev = device->getDevice();
    
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        std::array<VkDescriptorImageInfo, 3> imageInfos{};
        
        // Albedo 贴图 (binding 0)
        imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[0].imageView = albedoView;
        imageInfos[0].sampler = albedoSampler;
        
        // Normal 贴图 (binding 1)
        imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[1].imageView = normalView;
        imageInfos[1].sampler = normalSampler;
        
        // Specular 贴图 (binding 2)
        imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[2].imageView = specularView;
        imageInfos[2].sampler = specularSampler;
        
        std::array<VkWriteDescriptorSet, 3> descriptorWrites{};
        
        for (uint32_t j = 0; j < 3; j++) {
            descriptorWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[j].dstSet = material->sets[i];
            descriptorWrites[j].dstBinding = j;  // binding 0, 1, 2
            descriptorWrites[j].dstArrayElement = 0;
            descriptorWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[j].descriptorCount = 1;
            descriptorWrites[j].pImageInfo = &imageInfos[j];
        }
        
        vkUpdateDescriptorSets(dev, static_cast<uint32_t>(descriptorWrites.size()), 
                              descriptorWrites.data(), 0, nullptr);
    }
}

// ============================================
// 新的绑定函数
// ============================================

void GBufferPass::bindGlobalDescriptorSet(VkCommandBuffer cmd, uint32_t frameIndex) const {
    if (frameIndex < MAX_FRAMES_IN_FLIGHT && globalDescriptorSets[frameIndex] != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                                pipelineLayout, 0, 1, &globalDescriptorSets[frameIndex], 0, nullptr);
    }
}

void GBufferPass::bindMaterialDescriptorSet(VkCommandBuffer cmd, uint32_t frameIndex, MaterialDescriptor* material) const {
    if (material && material->valid && frameIndex < MAX_FRAMES_IN_FLIGHT) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 
                                pipelineLayout, 1, 1, &material->sets[frameIndex], 0, nullptr);
    }
}
