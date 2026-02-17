#include "WaterPass.h"
#include "GBufferPass.h"
#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include "VulkanPipeline.h"
#include "Mesh.h"
#include "MeshManager.h"
#include "Utils.h"
#include "../scene/Entity.h"
#include "../scene/Components.h"
#include <stdexcept>
#include <iostream>
#include <array>
#include <glm/gtc/matrix_transform.hpp>

WaterPass::WaterPass(std::shared_ptr<VulkanDevice> device, uint32_t width, uint32_t height,
                     VkRenderPass renderPass)
    : RenderPassBase(device, width, height)
    , device(device)
    , width(width)
    , height(height)
    , renderPass(renderPass) {
    
    passName = "Water Pass (Integrated SSR)";
    
    createWaterMesh();
    createVertexBuffer();
    createIndexBuffer();
    createDescriptorSetLayout();
    createDescriptorPool();
    createUniformBuffers();
    createDescriptorSets();
    createPipeline();
    
    std::cout << "WaterPass created with integrated SSR: " << width << "x" << height << std::endl;
}

WaterPass::~WaterPass() {
    cleanup();
}

void WaterPass::cleanup() {
    VkDevice dev = device->getDevice();
    
    vkDeviceWaitIdle(dev);
    
    if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(dev, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    
    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(dev, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    
    uniformBuffers.clear();
    
    if (descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(dev, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    
    if (descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(dev, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }
    
    indexBuffer.reset();
    vertexBuffer.reset();
    waterMesh.reset();
}

void WaterPass::resize(uint32_t newWidth, uint32_t newHeight) {
    width = newWidth;
    height = newHeight;
}

void WaterPass::setWaterColor(const glm::vec3& color, float alpha) {
    waterColor = color;
    waterAlpha = alpha;
}

void WaterPass::createWaterMesh() {
    // 创建一个大的水平面
    waterMesh = std::make_unique<Mesh>();
    
    float size = 50.0f;  // 水面大小
    int resolution = 32; // 网格细分程度
    
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    float step = size / resolution;
    float halfSize = size / 2.0f;
    
    // 生成顶点
    for (int z = 0; z <= resolution; z++) {
        for (int x = 0; x <= resolution; x++) {
            Vertex vertex;
            vertex.pos = glm::vec3(
                -halfSize + x * step,
                waterHeight,
                -halfSize + z * step
            );
            vertex.tangent = glm::vec3(1.0f, 0.0f, 0.0f);  // 切线方向
            vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);  // 向上的法线
            vertex.texCoord = glm::vec2(
                static_cast<float>(x) / resolution,
                static_cast<float>(z) / resolution
            );
            vertices.push_back(vertex);
        }
    }
    
    // 生成索引
    for (int z = 0; z < resolution; z++) {
        for (int x = 0; x < resolution; x++) {
            int topLeft = z * (resolution + 1) + x;
            int topRight = topLeft + 1;
            int bottomLeft = (z + 1) * (resolution + 1) + x;
            int bottomRight = bottomLeft + 1;
            
            // 第一个三角形
            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);
            
            // 第二个三角形
            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }
    
    waterMesh->setVertices(vertices);
    waterMesh->setIndices(indices);
}

void WaterPass::createVertexBuffer() {
    const auto& vertices = waterMesh->getVertices();
    VkDeviceSize bufferSize = sizeof(Vertex) * vertices.size();
    
    // 创建暂存缓冲区
    auto stagingBuffer = std::make_unique<VulkanBuffer>(
        device,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    
    void* data;
    vkMapMemory(device->getDevice(), stagingBuffer->getMemory(), 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), bufferSize);
    vkUnmapMemory(device->getDevice(), stagingBuffer->getMemory());
    
    // 创建设备本地缓冲区
    vertexBuffer = std::make_unique<VulkanBuffer>(
        device,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    // 复制数据
    VkCommandBuffer commandBuffer = device->beginSingleTimeCommands();
    
    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(commandBuffer, stagingBuffer->getBuffer(), vertexBuffer->getBuffer(), 1, &copyRegion);
    
    device->endSingleTimeCommands(commandBuffer);
}

void WaterPass::createIndexBuffer() {
    const auto& indices = waterMesh->getIndices();
    VkDeviceSize bufferSize = sizeof(uint32_t) * indices.size();
    
    auto stagingBuffer = std::make_unique<VulkanBuffer>(
        device,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    
    void* data;
    vkMapMemory(device->getDevice(), stagingBuffer->getMemory(), 0, bufferSize, 0, &data);
    memcpy(data, indices.data(), bufferSize);
    vkUnmapMemory(device->getDevice(), stagingBuffer->getMemory());
    
    indexBuffer = std::make_unique<VulkanBuffer>(
        device,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    VkCommandBuffer commandBuffer = device->beginSingleTimeCommands();
    
    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(commandBuffer, stagingBuffer->getBuffer(), indexBuffer->getBuffer(), 1, &copyRegion);
    
    device->endSingleTimeCommands(commandBuffer);
}

void WaterPass::createDescriptorSetLayout() {
    // 新布局：5 个绑定点
    // binding 0: Water UBO
    // binding 1: G-Buffer Position (用于 SSR)
    // binding 2: G-Buffer Normal (用于 SSR)
    // binding 3: G-Buffer Depth (用于 SSR)
    // binding 4: Scene Color (用于反射和折射)
    
    std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
    
    // Binding 0: Water UBO
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 1: G-Buffer Position
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 2: G-Buffer Normal
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 3: G-Buffer Depth
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    // Binding 4: Scene Color
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    
    if (vkCreateDescriptorSetLayout(device->getDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create water descriptor set layout!");
    }
}

void WaterPass::createDescriptorPool() {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1 * MAX_FRAMES_IN_FLIGHT;
    
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 4 * MAX_FRAMES_IN_FLIGHT;  // 4 textures per frame
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    
    if (vkCreateDescriptorPool(device->getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create water descriptor pool!");
    }
}

void WaterPass::createUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(WaterUBO);
    
    uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);
    
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        uniformBuffers[i] = std::make_unique<VulkanBuffer>(
            device,
            bufferSize,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        
        vkMapMemory(device->getDevice(), uniformBuffers[i]->getMemory(), 0, bufferSize, 0, &uniformBuffersMapped[i]);
    }
}

void WaterPass::createDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
    allocInfo.pSetLayouts = layouts.data();
    
    descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(device->getDevice(), &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate water descriptor sets!");
    }
    
    // 更新 UBO 绑定
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers[i]->getBuffer();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(WaterUBO);
        
        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;
        
        vkUpdateDescriptorSets(device->getDevice(), 1, &descriptorWrite, 0, nullptr);
    }
}

void WaterPass::createPipeline() {
    // 加载着色器
    auto vertShaderCode = Utils::readFile("shaders/water_vert.spv");
    auto fragShaderCode = Utils::readFile("shaders/water_frag.spv");
    
    VkShaderModule vertShaderModule = VulkanPipeline::createShaderModule(device->getDevice(), vertShaderCode);
    VkShaderModule fragShaderModule = VulkanPipeline::createShaderModule(device->getDevice(), fragShaderCode);
    
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
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();
    
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();
    
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // 双面渲染水面
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    // 深度测试 - 禁用，使用边缘软化（edgeSoftness）逻辑基于 G-Buffer 深度处理遮挡
    // 因为 Final Pass 的深度缓冲在开始时被清除为 1.0，无法与场景进行正确的深度比较
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;  // 禁用深度测试
    depthStencil.depthWriteEnable = VK_FALSE; // 禁用深度写入
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;
    
    // Alpha 混合
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();
    
    // Pipeline Layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    
    if (vkCreatePipelineLayout(device->getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create water pipeline layout!");
    }
    
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
    
    if (vkCreateGraphicsPipelines(device->getDevice(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create water graphics pipeline!");
    }
    
    vkDestroyShaderModule(device->getDevice(), fragShaderModule, nullptr);
    vkDestroyShaderModule(device->getDevice(), vertShaderModule, nullptr);
}

void WaterPass::updateUniforms(const glm::mat4& view, const glm::mat4& projection,
                                const glm::vec3& cameraPos, float time, uint32_t frameIndex) {
    WaterUBO ubo{};
    
    // 水面模型矩阵 - 放置在水面高度
    ubo.model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, waterHeight, 0.0f));
    ubo.view = view;
    ubo.projection = projection;
    ubo.invView = glm::inverse(view);
    ubo.invProjection = glm::inverse(projection);
    ubo.cameraPos = glm::vec4(cameraPos, 1.0f);
    ubo.waterColor = glm::vec4(waterColor, waterAlpha);
    ubo.waterParams = glm::vec4(waveSpeed, waveStrength, time, refractionStrength);
    // screenSize: xy=屏幕尺寸, zw=近平面/远平面（用于线性深度计算）
    ubo.screenSize = glm::vec4(width, height, 0.1f, 100.0f);
    // ssrParams: x=maxDistance, y=maxSteps, z=thickness（线性深度空间，单位：世界空间距离）
    ubo.ssrParams = glm::vec4(ssrMaxDistance, ssrMaxSteps, ssrThickness, 0.0f);
    
    memcpy(uniformBuffersMapped[frameIndex], &ubo, sizeof(WaterUBO));
}

void WaterPass::updateDescriptorSets(GBufferPass* gbuffer, VkImageView sceneColorView, VkSampler sampler) {
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        std::array<VkDescriptorImageInfo, 4> imageInfos{};
        
        // Binding 1: G-Buffer Position
        imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[0].imageView = gbuffer->getPositionView();
        imageInfos[0].sampler = sampler;
        
        // Binding 2: G-Buffer Normal
        imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[1].imageView = gbuffer->getNormalView();
        imageInfos[1].sampler = sampler;
        
        // Binding 3: G-Buffer Depth
        imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        imageInfos[2].imageView = gbuffer->getDepthView();
        imageInfos[2].sampler = sampler;
        
        // Binding 4: Scene Color
        imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfos[3].imageView = sceneColorView;
        imageInfos[3].sampler = sampler;
        
        std::array<VkWriteDescriptorSet, 4> descriptorWrites{};
        
        for (int j = 0; j < 4; j++) {
            descriptorWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[j].dstSet = descriptorSets[i];
            descriptorWrites[j].dstBinding = j + 1;  // 从 binding 1 开始
            descriptorWrites[j].dstArrayElement = 0;
            descriptorWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[j].descriptorCount = 1;
            descriptorWrites[j].pImageInfo = &imageInfos[j];
        }
        
        vkUpdateDescriptorSets(device->getDevice(), static_cast<uint32_t>(descriptorWrites.size()),
                               descriptorWrites.data(), 0, nullptr);
    }
}

void WaterPass::render(VkCommandBuffer cmd, uint32_t frameIndex) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    
    VkBuffer vb;
    VkBuffer ib;
    uint32_t indexCount;
    
    if (useExternalMesh && externalMesh && externalMesh->isValid()) {
        // 使用外部网格
        vb = externalMesh->getVertexBufferHandle();
        ib = externalMesh->getIndexBufferHandle();
        indexCount = externalMesh->getIndexCount();
    } else {
        // 使用内置网格
        vb = vertexBuffer->getBuffer();
        ib = indexBuffer->getBuffer();
        indexCount = static_cast<uint32_t>(waterMesh->getIndices().size());
    }
    
    VkBuffer vertexBuffers[] = { vb };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, ib, 0, VK_INDEX_TYPE_UINT32);
    
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                            &descriptorSets[frameIndex], 0, nullptr);
    
    vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
}

// ============================================================
// 外部网格设置接口
// ============================================================

bool WaterPass::setWaterEntity(const VulkanEngine::Entity& entity) {
    // 检查 Entity 是否有效
    if (!entity) {
        std::cerr << "[WaterPass] Invalid entity provided!" << std::endl;
        return false;
    }
    
    // 检查是否有 MeshRendererComponent
    if (!entity.hasComponent<VulkanEngine::MeshRendererComponent>()) {
        std::cerr << "[WaterPass] Entity does not have MeshRendererComponent!" << std::endl;
        return false;
    }
    
    // 获取 MeshRendererComponent
    const auto& meshRenderer = entity.getComponent<VulkanEngine::MeshRendererComponent>();
    
    if (meshRenderer.meshPath.empty()) {
        std::cerr << "[WaterPass] MeshRendererComponent has empty meshPath!" << std::endl;
        return false;
    }
    
    // 从 MeshManager 获取 GPUMesh
    auto gpuMesh = VulkanEngine::MeshManager::getInstance().getMesh(meshRenderer.meshPath);
    
    if (!gpuMesh || !gpuMesh->isValid()) {
        std::cerr << "[WaterPass] Failed to get mesh: " << meshRenderer.meshPath << std::endl;
        return false;
    }
    
    return setWaterMesh(gpuMesh);
}

bool WaterPass::setWaterMesh(std::shared_ptr<VulkanEngine::GPUMesh> gpuMesh) {
    if (!gpuMesh || !gpuMesh->isValid()) {
        std::cerr << "[WaterPass] Invalid GPUMesh provided!" << std::endl;
        return false;
    }
    
    externalMesh = gpuMesh;
    useExternalMesh = true;
    
    std::cout << "[WaterPass] Using external mesh with " 
              << gpuMesh->getVertexCount() << " vertices, "
              << gpuMesh->getIndexCount() << " indices" << std::endl;
    
    return true;
}

void WaterPass::clearExternalMesh() {
    externalMesh.reset();
    useExternalMesh = false;
    std::cout << "[WaterPass] Cleared external mesh, using built-in water mesh" << std::endl;
}