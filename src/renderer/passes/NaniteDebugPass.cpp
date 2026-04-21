#include "NaniteDebugPass.h"
#include "VulkanDevice.h"
#include "VulkanSwapChain.h"
#include "VulkanBuffer.h"
#include "../nanite/NaniteManager.h"
#include "../nanite/NaniteCluster.h"
#include "ClusterCullingPass.h"
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <array>
#include <cstring>
#include <set>

// UBO 结构�?
struct NaniteDebugUBO {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 viewPos;
    glm::vec4 lightPos;
    glm::vec4 lightColor;
};

// ============================================
// 构造与析构
// ============================================

NaniteDebugPass::NaniteDebugPass(std::shared_ptr<VulkanDevice> device,
                                 std::shared_ptr<VulkanSwapChain> swapChain,
                                 std::shared_ptr<Nanite::NaniteManager> naniteManager)
    : RenderPassBase(device, swapChain->getExtent().width, swapChain->getExtent().height)
    , m_device(device)
    , m_swapChain(swapChain)
    , m_naniteManager(naniteManager) {
    
    passName = "Nanite Debug Pass";
}

NaniteDebugPass::~NaniteDebugPass() {
    cleanup();
}

// ============================================
// 初始�?
// ============================================

void NaniteDebugPass::initialize(VkRenderPass renderPass) {
    if (m_initialized) {
        return;
    }
    
    createDescriptorSetLayout();
    createPipelineLayout();
    createGraphicsPipeline(renderPass);
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();
    
    m_initialized = true;
    std::cout << "[NaniteDebugPass] Initialized" << std::endl;
}

void NaniteDebugPass::cleanup() {
    if (!m_device) return;
    
    VkDevice dev = m_device->getDevice();
    vkDeviceWaitIdle(dev);
    
    // 清理渲染数据缓冲
    m_vertexBuffer.reset();
    m_indexBuffer.reset();
    m_clusterRenderData.clear();
    
    // 清理 Uniform Buffers
    m_uniformBuffers.clear();
    
    // 清理描述�?
    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(dev, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
    
    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(dev, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }
    
    // 清理管线
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(dev, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(dev, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    
    m_initialized = false;
    m_renderDataBuilt = false;
}

// ============================================
// 管线创建
// ============================================

void NaniteDebugPass::createDescriptorSetLayout() {
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
    
    if (vkCreateDescriptorSetLayout(m_device->getDevice(), &layoutInfo, nullptr, 
                                     &m_descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("[NaniteDebugPass] Failed to create descriptor set layout!");
    }
}

void NaniteDebugPass::createPipelineLayout() {
    // Push Constants: model(64) + normalMatrix(64) + clusterIndex(4) + totalClusters(4) + debugMode(4) + padding(4) = 144 bytes
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(ClusterDebugPushConstants);
    
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &m_descriptorSetLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;
    
    if (vkCreatePipelineLayout(m_device->getDevice(), &layoutInfo, nullptr, 
                               &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("[NaniteDebugPass] Failed to create pipeline layout!");
    }
}

static std::vector<char> readShaderFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open shader file: " + filename);
    }
    
    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    
    return buffer;
}

static VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module!");
    }
    
    return shaderModule;
}

void NaniteDebugPass::createGraphicsPipeline(VkRenderPass renderPass) {
    VkDevice dev = m_device->getDevice();
    
    // 读取着色器
    auto vertShaderCode = readShaderFile("shaders/nanite/cluster_debug_vert.spv");
    auto fragShaderCode = readShaderFile("shaders/nanite/cluster_debug_frag.spv");
    
    VkShaderModule vertShaderModule = createShaderModule(dev, vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(dev, fragShaderCode);
    
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
    
    // 顶点输入 - 使用�?GBuffer 相同的布局
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(float) * 11;  // pos(3) + normal(3) + texCoord(2) + tangent(3)
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
    
    // 视口（动态）
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    
    // 光栅�?- 禁用背面剔除以正确显�?Cluster
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;  // 禁用剔除，显示双�?
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
    
    // 动态状�?
    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();
    
    // 创建管线
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
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    
    if (vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, 
                                   &m_pipeline) != VK_SUCCESS) {
        throw std::runtime_error("[NaniteDebugPass] Failed to create graphics pipeline!");
    }
    
    // 销毁着色器模块
    vkDestroyShaderModule(dev, fragShaderModule, nullptr);
    vkDestroyShaderModule(dev, vertShaderModule, nullptr);
    
    std::cout << "[NaniteDebugPass] Graphics pipeline created" << std::endl;
}

// ============================================
// 描述符资�?
// ============================================

void NaniteDebugPass::createUniformBuffers() {
    size_t frameCount = m_swapChain->getImageCount();
    m_uniformBuffers.resize(frameCount);
    
    for (size_t i = 0; i < frameCount; i++) {
        m_uniformBuffers[i] = std::make_unique<VulkanBuffer>(
            m_device,
            sizeof(NaniteDebugUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
    }
}

void NaniteDebugPass::createDescriptorPool() {
    size_t frameCount = m_swapChain->getImageCount();
    
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = static_cast<uint32_t>(frameCount);
    
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = static_cast<uint32_t>(frameCount);
    
    if (vkCreateDescriptorPool(m_device->getDevice(), &poolInfo, nullptr, 
                               &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("[NaniteDebugPass] Failed to create descriptor pool!");
    }
}

void NaniteDebugPass::createDescriptorSets() {
    VkDevice dev = m_device->getDevice();
    size_t frameCount = m_swapChain->getImageCount();
    
    std::vector<VkDescriptorSetLayout> layouts(frameCount, m_descriptorSetLayout);
    
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(frameCount);
    allocInfo.pSetLayouts = layouts.data();
    
    m_descriptorSets.resize(frameCount);
    if (vkAllocateDescriptorSets(dev, &allocInfo, m_descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("[NaniteDebugPass] Failed to allocate descriptor sets!");
    }
    
    // 更新描述符集
    for (size_t i = 0; i < frameCount; i++) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffers[i]->getBuffer();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(NaniteDebugUBO);
        
        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_descriptorSets[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;
        
        vkUpdateDescriptorSets(dev, 1, &descriptorWrite, 0, nullptr);
    }
}

// ============================================
// Cluster 渲染数据构建
// ============================================

void NaniteDebugPass::buildRenderData() {
    if (!m_naniteManager) {
        return;
    }
    
    // 获取要渲染的网格列表
    std::vector<std::string> meshNames;
    if (m_renderAllMeshes) {
        meshNames = m_naniteManager->getAllMeshNames();
    } else if (!m_targetMeshName.empty()) {
        meshNames.push_back(m_targetMeshName);
    }
    
    if (meshNames.empty()) {
        return;
    }
    
    // 计算总的顶点和索引数�?
    m_totalVertexCount = 0;
    m_totalIndexCount = 0;
    m_totalClusterCount = 0;
    m_lod0ClusterCount = 0;
    
    // 收集所有网格的 cluster 数据
    std::vector<std::pair<std::string, std::shared_ptr<Nanite::ClusterizedMesh>>> meshesToRender;
    
    for (const auto& meshName : meshNames) {
        auto clusterizedMesh = m_naniteManager->getMesh(meshName);
        if (!clusterizedMesh || clusterizedMesh->clusters.empty()) {
            continue;
        }
        
        meshesToRender.push_back({meshName, clusterizedMesh});
        
        for (const auto& cluster : clusterizedMesh->clusters) {
            m_totalVertexCount += cluster.vertexCount;
            m_totalIndexCount += static_cast<uint32_t>(cluster.localIndices.size());
        }
        m_totalClusterCount += static_cast<uint32_t>(clusterizedMesh->clusters.size());
        
        // 计算 LOD0 �?cluster 数量
        if (!clusterizedMesh->lodLevels.empty()) {
            m_lod0ClusterCount += clusterizedMesh->lodLevels[0].clusterCount;
            std::cout << "[NaniteDebugPass] Mesh '" << meshName << "' LOD0 clusters: " 
                      << clusterizedMesh->lodLevels[0].clusterCount 
                      << " (total LOD levels: " << clusterizedMesh->lodLevels.size() << ")" << std::endl;
        } else {
            // 如果没有 LOD 信息，假设所�?cluster 都是 LOD0
            m_lod0ClusterCount += static_cast<uint32_t>(clusterizedMesh->clusters.size());
            std::cout << "[NaniteDebugPass] Mesh '" << meshName << "' no LOD info, using all " 
                      << clusterizedMesh->clusters.size() << " clusters as LOD0" << std::endl;
        }
    }
    
    if (m_totalVertexCount == 0 || m_totalIndexCount == 0) {
        std::cout << "[NaniteDebugPass] No vertex/index data" << std::endl;
        return;
    }
    
    // 构建顶点数据（与 GBuffer 格式一致：pos(3) + normal(3) + texCoord(2) + tangent(3)�?
    std::vector<float> vertexData;
    vertexData.reserve(m_totalVertexCount * 11);
    
    std::vector<uint32_t> indexData;
    indexData.reserve(m_totalIndexCount);
    
    m_clusterRenderData.clear();
    m_clusterRenderData.reserve(m_totalClusterCount);
    
    m_meshRenderInfos.clear();
    m_meshRenderInfos.reserve(meshesToRender.size());
    
    uint32_t currentVertexOffset = 0;
    uint32_t currentIndexOffset = 0;
    uint32_t globalClusterIndex = 0;
    
    for (const auto& [meshName, clusterizedMesh] : meshesToRender) {
        MeshRenderInfo meshInfo;
        meshInfo.meshName = meshName;
        meshInfo.modelMatrix = glm::mat4(1.0f);  // 默认单位矩阵，后续会更新
        
        for (uint32_t clusterIdx = 0; clusterIdx < clusterizedMesh->clusters.size(); clusterIdx++) {
            const auto& cluster = clusterizedMesh->clusters[clusterIdx];
            
            ClusterRenderData renderData;
            renderData.vertexOffset = currentVertexOffset;
            renderData.indexOffset = currentIndexOffset;
            renderData.indexCount = static_cast<uint32_t>(cluster.localIndices.size());
            renderData.clusterIndex = globalClusterIndex;
            
            // 添加顶点数据
            for (const auto& vertex : cluster.vertices) {
                // Position
                vertexData.push_back(vertex.position.x);
                vertexData.push_back(vertex.position.y);
                vertexData.push_back(vertex.position.z);
                // Normal
                vertexData.push_back(vertex.normal.x);
                vertexData.push_back(vertex.normal.y);
                vertexData.push_back(vertex.normal.z);
                // TexCoord
                vertexData.push_back(vertex.uv.x);
                vertexData.push_back(vertex.uv.y);
                // Tangent
                vertexData.push_back(vertex.tangent.x);
                vertexData.push_back(vertex.tangent.y);
                vertexData.push_back(vertex.tangent.z);
            }
            
            // 添加索引数据（将局部索引转换为全局索引�?
            for (uint32_t localIdx : cluster.localIndices) {
                indexData.push_back(currentVertexOffset + localIdx);
            }
            
            m_clusterRenderData.push_back(renderData);
            meshInfo.clusters.push_back(renderData);
            
            currentVertexOffset += cluster.vertexCount;
            currentIndexOffset += renderData.indexCount;
            globalClusterIndex++;
        }
        
        m_meshRenderInfos.push_back(std::move(meshInfo));
    }
    
    // 创建 GPU 缓冲
    VkDeviceSize vertexBufferSize = vertexData.size() * sizeof(float);
    VkDeviceSize indexBufferSize = indexData.size() * sizeof(uint32_t);
    
    m_vertexBuffer = std::make_unique<VulkanBuffer>(
        m_device,
        vertexBufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    m_vertexBuffer->uploadData(vertexData.data(), vertexBufferSize);
    
    m_indexBuffer = std::make_unique<VulkanBuffer>(
        m_device,
        indexBufferSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    m_indexBuffer->uploadData(indexData.data(), indexBufferSize);
    
    m_renderDataBuilt = true;
    
    // 输出详细�?LOD 层级统计
    std::cout << "[NaniteDebugPass] Render data built: " 
              << m_totalClusterCount << " clusters, "
              << m_meshRenderInfos.size() << " mesh(es)" << std::endl;
    
    // 输出每个 mesh �?LOD 分布
    for (const auto& [meshName, clusterizedMesh] : meshesToRender) {
        std::cout << "[NaniteDebugPass] Mesh '" << meshName << "' LOD levels:" << std::endl;
        for (size_t lod = 0; lod < clusterizedMesh->lodLevels.size(); ++lod) {
            const auto& level = clusterizedMesh->lodLevels[lod];
            std::cout << "  LOD " << lod << ": " << level.clusterCount 
                      << " clusters (start: " << level.clusterStartIndex
                      << ", error: " << level.maxError << ")" << std::endl;
        }
    }
}

// ============================================
// 渲染
// ============================================

void NaniteDebugPass::updateUniforms(uint32_t frameIndex,
                                     const glm::mat4& viewMatrix,
                                     const glm::mat4& projMatrix,
                                     const glm::vec3& viewPos,
                                     const glm::vec3& lightPos,
                                     const glm::vec3& lightColor) {
    if (frameIndex >= m_uniformBuffers.size()) return;
    
    NaniteDebugUBO ubo{};
    ubo.view = viewMatrix;
    ubo.proj = projMatrix;
    ubo.viewPos = glm::vec4(viewPos, 1.0f);
    ubo.lightPos = glm::vec4(lightPos, 1.0f);
    ubo.lightColor = glm::vec4(lightColor, 1.0f);
    
    m_uniformBuffers[frameIndex]->copyFrom(&ubo, sizeof(ubo));
}

void NaniteDebugPass::recordCommands(VkCommandBuffer commandBuffer, 
                                     uint32_t frameIndex,
                                     const glm::mat4& modelMatrix) {
    if (!m_initialized || !enabled) {
        return;
    }
    
    // 如果还没有构建渲染数据，尝试构建（只输出一次日志）
    if (!m_renderDataBuilt) {
        std::cout << "[NaniteDebugPass] Building render data for: " << m_targetMeshName << std::endl;
        buildRenderData();
        
        if (!m_renderDataBuilt) {
            std::cout << "[NaniteDebugPass] Failed to build render data!" << std::endl;
        }
    }
    
    if (!m_renderDataBuilt || m_clusterRenderData.empty()) {
        return;
    }
    
    // 绑定管线
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    
    // 设置视口和裁�?
    VkExtent2D extent = m_swapChain->getExtent();
    
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    
    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    
    // 绑定描述符集
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout, 0, 1, &m_descriptorSets[frameIndex], 0, nullptr);
    
    // 绑定顶点和索引缓�?
    VkBuffer vertexBuffers[] = { m_vertexBuffer->getBuffer() };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
    
    // 计算法线矩阵
    glm::mat4 normalMatrix = glm::transpose(glm::inverse(modelMatrix));
    
    // 为每�?Cluster 绘制
    for (const auto& clusterData : m_clusterRenderData) {
        ClusterDebugPushConstants pushConstants{};
        pushConstants.model = modelMatrix;
        pushConstants.normalMatrix = normalMatrix;
        pushConstants.clusterIndex = clusterData.clusterIndex;
        pushConstants.totalClusters = m_totalClusterCount;
        pushConstants.debugMode = static_cast<uint32_t>(m_debugMode);
        pushConstants.padding = 0.0f;
        
        vkCmdPushConstants(commandBuffer, m_pipelineLayout,
                          VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                          0, sizeof(ClusterDebugPushConstants), &pushConstants);
        
        vkCmdDrawIndexed(commandBuffer, clusterData.indexCount, 1, 
                         clusterData.indexOffset, 0, 0);
    }
}

void NaniteDebugPass::recordCommandsMultiMesh(VkCommandBuffer commandBuffer,
                                              uint32_t frameIndex,
                                              const std::unordered_map<std::string, glm::mat4>& meshMatrices) {
    if (!m_initialized || !enabled) {
        return;
    }
    
    // 如果还没有构建渲染数据，尝试构建
    if (!m_renderDataBuilt) {
        std::cout << "[NaniteDebugPass] Building render data for all meshes" << std::endl;
        buildRenderData();
        
        if (!m_renderDataBuilt) {
            std::cout << "[NaniteDebugPass] Failed to build render data!" << std::endl;
            return;
        }
    }
    
    if (!m_renderDataBuilt || m_meshRenderInfos.empty()) {
        return;
    }
    
    // 绑定管线
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    
    // 设置视口和裁�?
    VkExtent2D extent = m_swapChain->getExtent();
    
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    
    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    
    // 绑定描述符集
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout, 0, 1, &m_descriptorSets[frameIndex], 0, nullptr);
    
    // 绑定顶点和索引缓�?
    VkBuffer vertexBuffers[] = { m_vertexBuffer->getBuffer() };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
    
    // 为每个网格的每个 Cluster 绘制
    for (const auto& meshInfo : m_meshRenderInfos) {
        // 查找该网格的模型矩阵
        glm::mat4 modelMatrix = glm::mat4(1.0f);
        auto it = meshMatrices.find(meshInfo.meshName);
        if (it != meshMatrices.end()) {
            modelMatrix = it->second;
        }
        
        // 计算法线矩阵
        glm::mat4 normalMatrix = glm::transpose(glm::inverse(modelMatrix));
        
        // 绘制该网格的所�?Cluster
        for (const auto& clusterData : meshInfo.clusters) {
            ClusterDebugPushConstants pushConstants{};
            pushConstants.model = modelMatrix;
            pushConstants.normalMatrix = normalMatrix;
            pushConstants.clusterIndex = clusterData.clusterIndex;
            pushConstants.totalClusters = m_totalClusterCount;
            pushConstants.debugMode = static_cast<uint32_t>(m_debugMode);
            pushConstants.padding = 0.0f;
            
            vkCmdPushConstants(commandBuffer, m_pipelineLayout,
                              VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                              0, sizeof(ClusterDebugPushConstants), &pushConstants);
            
            vkCmdDrawIndexed(commandBuffer, clusterData.indexCount, 1, 
                             clusterData.indexOffset, 0, 0);
        }
    }
}

void NaniteDebugPass::resize(uint32_t newWidth, uint32_t newHeight) {
    width = newWidth;
    height = newHeight;
    // 管线使用动态视口，不需要重�?
}

// ============================================
// 调试模式
// ============================================

void NaniteDebugPass::cycleDebugMode() {
    uint32_t current = static_cast<uint32_t>(m_debugMode);
    current = (current + 1) % 4;
    m_debugMode = static_cast<NaniteDebugMode>(current);
    
    std::cout << "[NaniteDebugPass] Debug mode: " << getDebugModeName() << std::endl;
}

const char* NaniteDebugPass::getDebugModeName() const {
    switch (m_debugMode) {
        case NaniteDebugMode::ClusterColor: return "Cluster Color";
        case NaniteDebugMode::Normal: return "Normal";
        case NaniteDebugMode::LOD: return "LOD Level";
        case NaniteDebugMode::HashColor: return "Hash Color";
        default: return "Unknown";
    }
}

bool NaniteDebugPass::hasClusterData() const {
    return m_renderDataBuilt && !m_clusterRenderData.empty();
}

void NaniteDebugPass::ensureRenderDataBuilt() {
    // �?RenderPass 之前调用此方法，确保渲染数据已构�?
    // buildRenderData() 会使�?VulkanBuffer::uploadData()，这会触�?vkCmdCopyBuffer
    // 必须�?RenderPass 之前完成
    if (!m_initialized) {
        return;
    }
    
    if (!m_renderDataBuilt) {
        std::cout << "[NaniteDebugPass] Building render data (pre-RenderPass)" << std::endl;
        buildRenderData();
        
        if (!m_renderDataBuilt) {
            std::cerr << "[NaniteDebugPass] Failed to build render data!" << std::endl;
        }
    }
}

void NaniteDebugPass::recordCommandsWithLOD(VkCommandBuffer commandBuffer,
                                            uint32_t frameIndex,
                                            const std::unordered_map<std::string, glm::mat4>& meshMatrices,
                                            Nanite::NaniteManager* naniteManager) {
    if (!m_initialized || !enabled) {
        return;
    }
    
    // 渲染数据应该�?prepareNaniteCulling() 中通过 ensureRenderDataBuilt() 构建
    // 如果还没有构建，说明调用顺序有问题，输出警告
    if (!m_renderDataBuilt || m_clusterRenderData.empty()) {
        // 不在这里调用 buildRenderData()，因为我们在 RenderPass 内部
        // 无法安全地执行缓冲区上传操作
        return;
    }
    
    // =====================================================
    // �?ClusterCullingPass 获取 GPU culling 结果
    // 使用双缓冲机制确保数据稳定（1帧延迟）
    // =====================================================
    std::set<uint32_t> visibleSet;
    
    // ========================================================
    // 调试开关：强制渲染所�?cluster（绕�?GPU culling�?
    // 用于测试边缘闪烁是否�?culling 结果相关
    // ========================================================
    constexpr bool DEBUG_RENDER_ALL_CLUSTERS = false;  // 设为 false 使用 GPU LOD 选择
    constexpr bool DEBUG_RENDER_LOD0_ONLY = false;     // 已禁用，LOD �?GPU Shader 选择
    
    if (DEBUG_RENDER_ALL_CLUSTERS) {
        if (DEBUG_RENDER_LOD0_ONLY) {
            // 只渲�?LOD0 �?cluster（避免不�?LOD 级别几何重叠导致 Z-fighting�?
            // 需要为每个 mesh 分别计算�?LOD0 cluster 的全局索引范围
            uint32_t globalClusterOffset = 0;
            
            if (naniteManager) {
                for (const auto& meshInfo : m_meshRenderInfos) {
                    auto clusterizedMesh = naniteManager->getMesh(meshInfo.meshName);
                    if (!clusterizedMesh) continue;
                    
                    // 获取�?mesh �?LOD0 cluster 数量
                    uint32_t lod0Count = 0;
                    if (!clusterizedMesh->lodLevels.empty()) {
                        lod0Count = clusterizedMesh->lodLevels[0].clusterCount;
                    } else {
                        // 没有 LOD 信息，假设所�?cluster 都是 LOD0
                        lod0Count = static_cast<uint32_t>(clusterizedMesh->clusters.size());
                    }
                    
                    // 将该 mesh �?LOD0 cluster 添加到可见列�?
                    for (uint32_t i = 0; i < lod0Count; ++i) {
                        visibleSet.insert(globalClusterOffset + i);
                    }
                    
                    // 更新全局偏移（包含所�?LOD �?cluster�?
                    globalClusterOffset += static_cast<uint32_t>(clusterizedMesh->clusters.size());
                }
            } else {
                // 如果没有 naniteManager，回退到使�?m_lod0ClusterCount
                for (uint32_t i = 0; i < m_lod0ClusterCount; ++i) {
                    visibleSet.insert(i);
                }
            }
            
            std::cout << "[NaniteDebugPass] LOD0 only mode: " << visibleSet.size() 
                      << " clusters visible (total: " << m_totalClusterCount << ")" << std::endl;
        } else {
            // 渲染所�?LOD �?cluster（会导致 Z-fighting！）
            for (uint32_t i = 0; i < m_totalClusterCount; ++i) {
                visibleSet.insert(i);
            }
        }
    }
    else {
        // 从 ClusterCullingPass 获取 GPU 视锥剔除后的可见 cluster 列表
        std::set<uint32_t> frustumVisible;
        if (m_clusterCullingPass) {
            const auto& visibleIndices = m_clusterCullingPass->getVisibleIndices();
            for (uint32_t idx : visibleIndices) {
                frustumVisible.insert(idx);
            }
        }
        
        // 如果 GPU culling 没有数据，包含所有 cluster
        if (frustumVisible.empty()) {
            for (uint32_t i = 0; i < m_totalClusterCount; ++i) {
                frustumVisible.insert(i);
            }
        }
        
        // ============================================================
        // CPU 端 LOD 选择（绕过 GPU Shader 的 LOD 逻辑）
        // 
        // 对 frustumVisible 中的每个 cluster，基于距离做简单 LOD 选择：
        // 只保留目标 LOD 级别的 cluster
        // ============================================================
        if (m_naniteManager) {
            const auto& allGPUData = m_naniteManager->getAllGPUClusterData();
            glm::vec3 cameraPos = m_naniteManager->getLastCameraPosition();
            
            // ============================================================
            // 第一步：按 mesh 分组，对每个 mesh 计算统一的 targetLOD
            // 使用 mesh 的整体包围球中心（用根节点的包围球）来计算距离
            // 这保证同一 mesh 的所有 cluster 选择同一个 LOD 级别
            // 避免同一区域两个 LOD 同时显示
            // ============================================================
            
            // 找到每个 mesh 的根节点，用根节点的包围球代表整个 mesh
            // 根节点 = parentGroupIndex == 0xFFFFFFFF 的 cluster
            struct MeshLODInfo {
                glm::vec3 meshCenter{0.0f};
                float meshRadius = 0.0f;
                uint32_t maxLOD = 0;
                uint32_t targetLOD = 0;
            };
            
            // 通过根节点收集 mesh 信息
            // 由于多个 mesh 合并上传，我们用 cluster 的全局索引范围来区分 mesh
            // 简单方案：找所有根节点，每个根节点代表一个 mesh
            std::vector<std::pair<uint32_t, MeshLODInfo>> rootClusters; // (globalIdx, info)
            
            for (uint32_t idx = 0; idx < allGPUData.size(); ++idx) {
                const auto& c = allGPUData[idx];
                if (c.parentGroupIndex == 0xFFFFFFFF && (c.flags & 1u)) {
                    MeshLODInfo info;
                    info.meshCenter = glm::vec3(c.boundingSphere.x, c.boundingSphere.y, c.boundingSphere.z);
                    info.meshRadius = c.boundingSphere.w;
                    info.maxLOD = c.lodLevel;
                    
                    float dist = glm::length(info.meshCenter - cameraPos);
                    
                    // 基于距离选择目标 LOD
                    if (dist > 60.0f) info.targetLOD = 3;
                    else if (dist > 30.0f) info.targetLOD = 2;
                    else if (dist > 10.0f) info.targetLOD = 1;
                    else info.targetLOD = 0;
                    
                    rootClusters.push_back({idx, info});
                }
            }
            
            // 对每个 cluster，找到它所属的 mesh（通过向上追溯 parent 直到根节点）
            // 然后使用该 mesh 的统一 targetLOD
            for (uint32_t idx : frustumVisible) {
                if (idx >= allGPUData.size()) continue;
                const auto& cluster = allGPUData[idx];
                
                // 向上追溯到根节点，找到 mesh 的 targetLOD
                uint32_t rootIdx = idx;
                uint32_t safetyCounter = 0;
                while (allGPUData[rootIdx].parentGroupIndex != 0xFFFFFFFF && 
                       allGPUData[rootIdx].parentGroupIndex < allGPUData.size() &&
                       safetyCounter < 10) {
                    rootIdx = allGPUData[rootIdx].parentGroupIndex;
                    safetyCounter++;
                }
                
                // 用根节点的距离计算统一的 targetLOD
                glm::vec3 rootCenter(allGPUData[rootIdx].boundingSphere.x, 
                                     allGPUData[rootIdx].boundingSphere.y, 
                                     allGPUData[rootIdx].boundingSphere.z);
                float dist = glm::length(rootCenter - cameraPos);
                
                // 距离阈值按指数递增，适配 7-8 层 LOD
                uint32_t targetLOD = 0;
                if (dist > 200.0f) targetLOD = 7;
                else if (dist > 120.0f) targetLOD = 6;
                else if (dist > 70.0f) targetLOD = 5;
                else if (dist > 45.0f) targetLOD = 4;
                else if (dist > 30.0f) targetLOD = 3;
                else if (dist > 18.0f) targetLOD = 2;
                else if (dist > 10.0f) targetLOD = 1;
                else targetLOD = 0;
                
                // 精确匹配
                if (cluster.lodLevel == targetLOD) {
                    visibleSet.insert(idx);
                } else if (cluster.parentGroupIndex == 0xFFFFFFFF && targetLOD > cluster.lodLevel) {
                    // 根节点回退：目标 LOD 超过最大层级，使用根节点
                    visibleSet.insert(idx);
                }
            }
        } else {
            visibleSet = frustumVisible;
        }
    }
    
    // 绑定管线
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    
    // 设置视口和裁�?
    VkExtent2D extent = m_swapChain->getExtent();
    
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    
    VkRect2D scissor{};
    scissor.offset = { 0, 0 };
    scissor.extent = extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    
    // 绑定描述符集
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_pipelineLayout, 0, 1, &m_descriptorSets[frameIndex], 0, nullptr);
    
    // 绑定顶点和索引缓�?
    VkBuffer vertexBuffers[] = { m_vertexBuffer->getBuffer() };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
    
    uint32_t drawnClusters = 0;
    
    // 为每个网格的每个可见 Cluster 绘制
    for (const auto& meshInfo : m_meshRenderInfos) {
        // 查找该网格的模型矩阵
        glm::mat4 modelMatrix = glm::mat4(1.0f);
        auto it = meshMatrices.find(meshInfo.meshName);
        if (it != meshMatrices.end()) {
            modelMatrix = it->second;
        }
        
        // 计算法线矩阵
        glm::mat4 normalMatrix = glm::transpose(glm::inverse(modelMatrix));
        
        // 只绘制可见的 Cluster
        for (const auto& clusterData : meshInfo.clusters) {
            // 检查该 cluster 是否在可见列表中
            if (visibleSet.find(clusterData.clusterIndex) == visibleSet.end()) {
                continue; // 跳过不可见的 cluster
            }
            
            ClusterDebugPushConstants pushConstants{};
            pushConstants.model = modelMatrix;
            pushConstants.normalMatrix = normalMatrix;
            pushConstants.clusterIndex = clusterData.clusterIndex;
            pushConstants.totalClusters = m_totalClusterCount;
            pushConstants.debugMode = static_cast<uint32_t>(m_debugMode);
            pushConstants.padding = 0.0f;
            
            vkCmdPushConstants(commandBuffer, m_pipelineLayout,
                              VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                              0, sizeof(ClusterDebugPushConstants), &pushConstants);
            
            vkCmdDrawIndexed(commandBuffer, clusterData.indexCount, 1, 
                             clusterData.indexOffset, 0, 0);
            
            drawnClusters++;
        }
    }
    
    // ======== LOD 追踪和实时输�?========
    // 统计�?LOD 等级数量（从 GPU visible list 统计�?
    std::unordered_map<uint32_t, uint32_t> lodClusterCounts;
    
    // 构建 cluster index -> LOD level 的映�?
    std::unordered_map<uint32_t, uint32_t> clusterToLOD;
    if (naniteManager) {
        for (const auto& meshInfo : m_meshRenderInfos) {
            auto clusterizedMesh = naniteManager->getMesh(meshInfo.meshName);
            if (!clusterizedMesh) continue;
            
            for (size_t lod = 0; lod < clusterizedMesh->lodLevels.size(); ++lod) {
                const auto& level = clusterizedMesh->lodLevels[lod];
                for (uint32_t i = 0; i < level.clusterCount; ++i) {
                    clusterToLOD[level.clusterStartIndex + i] = static_cast<uint32_t>(lod);
                }
            }
        }
    }
    
    // 统计可见 cluster �?LOD 分布
    for (uint32_t idx : visibleSet) {
        auto it = clusterToLOD.find(idx);
        if (it != clusterToLOD.end()) {
            lodClusterCounts[it->second]++;
        }
    }
    
    // 实时输出 LOD 信息（每 60 帧输出一次）
    static uint32_t frameCounter = 0;
    if (++frameCounter % 60 == 0) {
        std::cout << "\r[LOD] Drawn:" << drawnClusters << "/" << m_totalClusterCount 
                  << " | GPU Culling Mode"
                  << " | Visible:" << visibleSet.size()
                  << " | ";
        
        // 输出分布
        std::cout << "[";
        for (uint32_t lod = 0; lod < 10; ++lod) {
            auto it = lodClusterCounts.find(lod);
            if (it != lodClusterCounts.end() && it->second > 0) {
                std::cout << "L" << lod << ":" << it->second << " ";
            }
        }
        std::cout << "]";
        
        std::cout << "                " << std::flush;
    }
}
