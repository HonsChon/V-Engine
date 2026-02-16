#include "ForwardPass.h"
#include "../core/VulkanDevice.h"
#include "../resources/Mesh.h"
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <cstring>

ForwardPass::ForwardPass(std::shared_ptr<VulkanDevice> device,
                         VkRenderPass renderPass,
                         uint32_t width, uint32_t height,
                         uint32_t maxFramesInFlight)
    : RenderPassBase(device, width, height)
    , device(device)
    , renderPass(renderPass)
    , width(width)
    , height(height)
    , maxFramesInFlight(maxFramesInFlight) {
    
    passName = "Forward Pass";
    
    createDescriptorSetLayouts();
    createPipeline();
    createUniformBuffers();
    createDescriptorPools();
    createGlobalDescriptorSets();
    
    std::cout << "ForwardPass created: " << width << "x" << height << std::endl;
}

ForwardPass::~ForwardPass() {
    cleanup();
}

void ForwardPass::recreate(VkRenderPass newRenderPass, uint32_t newWidth, uint32_t newHeight) {
    vkDeviceWaitIdle(device->getDevice());
    
    // 只需要重建 Pipeline
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device->getDevice(), pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device->getDevice(), pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    
    renderPass = newRenderPass;
    width = newWidth;
    height = newHeight;
    
    createPipeline();
}

void ForwardPass::cleanup() {
    VkDevice dev = device->getDevice();
    
    // 销毁 Uniform Buffers
    for (size_t i = 0; i < uniformBuffers.size(); i++) {
        if (uniformBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(dev, uniformBuffers[i], nullptr);
        }
        if (uniformBuffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(dev, uniformBuffersMemory[i], nullptr);
        }
    }
    uniformBuffers.clear();
    uniformBuffersMemory.clear();
    uniformBuffersMapped.clear();
    
    // 清除材质描述符缓存
    materialDescriptorCache.clear();
    
    // 销毁材质描述符池
    for (auto pool : materialDescriptorPools) {
        if (pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(dev, pool, nullptr);
        }
    }
    materialDescriptorPools.clear();
    
    // 销毁全局描述符池
    if (globalDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(dev, globalDescriptorPool, nullptr);
        globalDescriptorPool = VK_NULL_HANDLE;
    }
    
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
}

void ForwardPass::createDescriptorSetLayouts() {
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
        
        if (vkCreateDescriptorSetLayout(device->getDevice(), &layoutInfo, nullptr, &globalSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create global descriptor set layout!");
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
        
        if (vkCreateDescriptorSetLayout(device->getDevice(), &layoutInfo, nullptr, &materialSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create material descriptor set layout!");
        }
    }
    
    std::cout << "ForwardPass descriptor set layouts created (Set 0: Global UBO, Set 1: Material)" << std::endl;
}

void ForwardPass::createPipeline() {
    VkDevice dev = device->getDevice();
    
    // 读取 PBR 着色器
    auto vertShaderCode = readFile("shaders/pbr_vert.spv");
    auto fragShaderCode = readFile("shaders/pbr_frag.spv");
    
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
    
    // 顶点输入 - 与 Vertex 结构体匹配
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
    
    // 光栅化
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
    
    // 颜色混合
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | 
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    // 动态状态
    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();
    
    // Push Constants 范围定义
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstantData);  // 2 个 mat4 = 128 bytes
    
    // Pipeline 布局 - 使用两个描述符集
    std::array<VkDescriptorSetLayout, 2> setLayouts = { globalSetLayout, materialSetLayout };
    
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = setLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
    
    if (vkCreatePipelineLayout(dev, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create ForwardPass pipeline layout!");
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
        throw std::runtime_error("Failed to create ForwardPass graphics pipeline!");
    }
    
    // 销毁着色器模块
    vkDestroyShaderModule(dev, fragShaderModule, nullptr);
    vkDestroyShaderModule(dev, vertShaderModule, nullptr);
    
    std::cout << "ForwardPass pipeline created (2 descriptor sets: Global + Material)" << std::endl;
}

void ForwardPass::createUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);
    
    uniformBuffers.resize(maxFramesInFlight);
    uniformBuffersMemory.resize(maxFramesInFlight);
    uniformBuffersMapped.resize(maxFramesInFlight);
    
    for (size_t i = 0; i < maxFramesInFlight; i++) {
        // 创建 Buffer
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateBuffer(device->getDevice(), &bufferInfo, nullptr, &uniformBuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create uniform buffer!");
        }
        
        // 分配内存
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(device->getDevice(), uniformBuffers[i], &memRequirements);
        
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        
        if (vkAllocateMemory(device->getDevice(), &allocInfo, nullptr, &uniformBuffersMemory[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate uniform buffer memory!");
        }
        
        vkBindBufferMemory(device->getDevice(), uniformBuffers[i], uniformBuffersMemory[i], 0);
        
        // 持久映射
        vkMapMemory(device->getDevice(), uniformBuffersMemory[i], 0, bufferSize, 0, &uniformBuffersMapped[i]);
    }
}

void ForwardPass::createDescriptorPools() {
    // ========== 全局描述符池 (UBO) ==========
    {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize.descriptorCount = maxFramesInFlight;
        
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = maxFramesInFlight;
        
        if (vkCreateDescriptorPool(device->getDevice(), &poolInfo, nullptr, &globalDescriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create global descriptor pool!");
        }
    }
    
    // ========== 材质描述符池 (纹理) ==========
    ensureMaterialPoolCapacity();
}

void ForwardPass::ensureMaterialPoolCapacity() {
    // 检查是否需要创建新的材质描述符池
    if (materialDescriptorPools.empty() || 
        allocatedMaterialSets >= MATERIALS_PER_POOL * maxFramesInFlight) {
        
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = MATERIALS_PER_POOL * 3 * maxFramesInFlight;  // 每个材质 3 个纹理
        
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = MATERIALS_PER_POOL * maxFramesInFlight;
        
        VkDescriptorPool newPool;
        if (vkCreateDescriptorPool(device->getDevice(), &poolInfo, nullptr, &newPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create material descriptor pool!");
        }
        
        materialDescriptorPools.push_back(newPool);
        currentMaterialPoolIndex = static_cast<uint32_t>(materialDescriptorPools.size() - 1);
        allocatedMaterialSets = 0;
        
        std::cout << "Created material descriptor pool #" << materialDescriptorPools.size() << std::endl;
    }
}

void ForwardPass::createGlobalDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, globalSetLayout);
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = globalDescriptorPool;
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();
    
    globalDescriptorSets.resize(maxFramesInFlight);
    if (vkAllocateDescriptorSets(device->getDevice(), &allocInfo, globalDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate global descriptor sets!");
    }
    
    // 更新 UBO 绑定
    for (size_t i = 0; i < maxFramesInFlight; i++) {
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
        
        vkUpdateDescriptorSets(device->getDevice(), 1, &descriptorWrite, 0, nullptr);
    }
    
    std::cout << "Global descriptor sets created and bound to UBO" << std::endl;
}

void ForwardPass::updateUniformBuffer(uint32_t currentFrame, const UniformBufferObject& ubo) {
    memcpy(uniformBuffersMapped[currentFrame], &ubo, sizeof(ubo));
}

// ========== 材质描述符管理 ==========

ForwardPass::MaterialDescriptor* ForwardPass::allocateMaterialDescriptor(const std::string& materialId) {
    // 检查是否已存在
    auto it = materialDescriptorCache.find(materialId);
    if (it != materialDescriptorCache.end()) {
        return &it->second;
    }
    
    // 确保池有容量
    ensureMaterialPoolCapacity();
    
    // 创建新的材质描述符
    MaterialDescriptor descriptor;
    descriptor.sets.resize(maxFramesInFlight);
    
    std::vector<VkDescriptorSetLayout> layouts(maxFramesInFlight, materialSetLayout);
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = materialDescriptorPools[currentMaterialPoolIndex];
    allocInfo.descriptorSetCount = maxFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();
    
    if (vkAllocateDescriptorSets(device->getDevice(), &allocInfo, descriptor.sets.data()) != VK_SUCCESS) {
        std::cerr << "Failed to allocate material descriptor sets for: " << materialId << std::endl;
        return nullptr;
    }
    
    allocatedMaterialSets += maxFramesInFlight;
    descriptor.valid = true;
    
    materialDescriptorCache[materialId] = std::move(descriptor);
    
    std::cout << "Allocated material descriptor: " << materialId << std::endl;
    
    return &materialDescriptorCache[materialId];
}

void ForwardPass::updateMaterialTextures(MaterialDescriptor* material,
                                          VkImageView albedoView, VkSampler albedoSampler,
                                          VkImageView normalView, VkSampler normalSampler,
                                          VkImageView specularView, VkSampler specularSampler) {
    if (!material || !material->valid) return;
    
    for (uint32_t i = 0; i < maxFramesInFlight; i++) {
        std::array<VkDescriptorImageInfo, 3> imageInfos{};
        
        // Albedo (binding 0)
        imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[0].imageView = albedoView;
        imageInfos[0].sampler = albedoSampler;
        
        // Normal (binding 1)
        imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[1].imageView = normalView;
        imageInfos[1].sampler = normalSampler;
        
        // Specular (binding 2)
        imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[2].imageView = specularView;
        imageInfos[2].sampler = specularSampler;
        
        std::array<VkWriteDescriptorSet, 3> descriptorWrites{};
        
        for (int j = 0; j < 3; j++) {
            descriptorWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[j].dstSet = material->sets[i];
            descriptorWrites[j].dstBinding = j;
            descriptorWrites[j].dstArrayElement = 0;
            descriptorWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[j].descriptorCount = 1;
            descriptorWrites[j].pImageInfo = &imageInfos[j];
        }
        
        vkUpdateDescriptorSets(device->getDevice(), 
                               static_cast<uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(), 0, nullptr);
    }
}

ForwardPass::MaterialDescriptor* ForwardPass::getMaterialDescriptor(const std::string& materialId) {
    auto it = materialDescriptorCache.find(materialId);
    if (it != materialDescriptorCache.end() && it->second.valid) {
        return &it->second;
    }
    return nullptr;
}

// ========== 渲染命令 ==========

void ForwardPass::begin(VkCommandBuffer cmd) {
    // 设置视口和裁剪
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

void ForwardPass::bindPipeline(VkCommandBuffer cmd) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
}

void ForwardPass::bindGlobalDescriptorSet(VkCommandBuffer cmd, uint32_t frameIndex) {
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                            0, 1, &globalDescriptorSets[frameIndex], 0, nullptr);
}

void ForwardPass::bindMaterialDescriptorSet(VkCommandBuffer cmd, uint32_t frameIndex, MaterialDescriptor* material) {
    if (!material || !material->valid || frameIndex >= material->sets.size()) return;
    
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                            1, 1, &material->sets[frameIndex], 0, nullptr);
}

void ForwardPass::pushModelMatrix(VkCommandBuffer cmd, const glm::mat4& model) {
    PushConstantData pushData{};
    pushData.model = model;
    pushData.normalMatrix = glm::transpose(glm::inverse(model));
    
    vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 
                       0, sizeof(PushConstantData), &pushData);
}

void ForwardPass::drawMesh(VkCommandBuffer cmd, VkBuffer vertexBuffer, VkBuffer indexBuffer, uint32_t indexCount) {
    if (vertexBuffer == VK_NULL_HANDLE || indexBuffer == VK_NULL_HANDLE) return;
    
    VkBuffer vertexBuffers[] = { vertexBuffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
}

VkShaderModule ForwardPass::createShaderModule(const std::vector<char>& code) {
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

std::vector<char> ForwardPass::readFile(const std::string& filename) {
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

uint32_t ForwardPass::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
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