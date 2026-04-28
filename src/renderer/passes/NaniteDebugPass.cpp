#include "NaniteDebugPass.h"
#include "VulkanDevice.h"
#include "VulkanSwapChain.h"
#include "../nanite/NaniteManager.h"
#include "../nanite/NaniteCluster.h"
#include "ClusterCullingPass.h"

// RHI headers
#include "RHIDevice.h"
#include "RHIBuffer.h"
#include "RHIDescriptor.h"
#include "RHIPipeline.h"

// Vulkan backend headers — for downcast to get native handles
#include "VulkanRHIDevice.h"
#include "VulkanRHIPipeline.h"
#include "VulkanRHIDescriptor.h"
#include "VulkanRHIBuffer.h"

#include <stdexcept>
#include <iostream>
#include <array>
#include <cstring>
#include <set>

// ============================================
// 构造与析构
// ============================================

NaniteDebugPass::NaniteDebugPass(std::shared_ptr<VulkanDevice> device,
                                 RHIDevice* rhiDevice,
                                 std::shared_ptr<VulkanSwapChain> swapChain,
                                 std::shared_ptr<Nanite::NaniteManager> naniteManager)
    : RenderPassBase(device, swapChain->getExtent().width, swapChain->getExtent().height)
    , rhiDevice_(rhiDevice)
    , m_device(device)
    , m_swapChain(swapChain)
    , m_naniteManager(naniteManager) {
    
    passName = "Nanite Debug Pass";
}

NaniteDebugPass::~NaniteDebugPass() {
    cleanup();
}

// ============================================
// 初始化
// ============================================

void NaniteDebugPass::initialize(VkRenderPass renderPass) {
    if (m_initialized) {
        return;
    }
    
    createBindingLayout();
    createUniformBuffers();
    createDescriptorSets();
    createPipeline(renderPass);
    
    m_initialized = true;
    std::cout << "[NaniteDebugPass] Initialized (RHI)" << std::endl;
}

void NaniteDebugPass::cleanup() {
    if (!rhiDevice_) return;
    rhiDevice_->waitIdle();

    m_vertexBuffer_.reset();
    m_indexBuffer_.reset();
    m_clusterRenderData.clear();
    m_uniformBuffers_.clear();
    m_descriptorSets_.clear();
    m_pipeline_.reset();
    m_bindingLayout_.reset();

    m_initialized = false;
    m_renderDataBuilt = false;
}

// ============================================
// RHI 资源创建
// ============================================

void NaniteDebugPass::createBindingLayout() {
    RHIBindingLayoutDesc desc;
    desc.entries.push_back({
        0,                                  // binding
        RHIDescriptorType::UniformBuffer,   // type
        RHIShaderStage::Vertex | RHIShaderStage::Fragment,  // stageFlags
        1                                   // count
    });
    m_bindingLayout_ = rhiDevice_->createBindingLayout(desc);
}

void NaniteDebugPass::createUniformBuffers() {
    size_t frameCount = m_swapChain->getImageCount();
    m_uniformBuffers_.resize(frameCount);
    
    for (size_t i = 0; i < frameCount; i++) {
        RHIBufferDesc desc{};
        desc.size = sizeof(NaniteDebugUBO);
        desc.usage = RHIBufferUsage::Uniform;
        desc.memoryUsage = RHIMemoryUsage::CPUToGPU;
        m_uniformBuffers_[i] = rhiDevice_->createBuffer(desc);
    }
}

void NaniteDebugPass::createDescriptorSets() {
    auto* vkDevice = static_cast<VulkanRHIDevice*>(rhiDevice_);
    auto* vkLayout = static_cast<VulkanRHIBindingLayout*>(m_bindingLayout_.get());

    size_t frameCount = m_swapChain->getImageCount();
    m_descriptorSets_.resize(frameCount);

    for (size_t i = 0; i < frameCount; ++i) {
        m_descriptorSets_[i] = vkDevice->allocateDescriptorSet(vkLayout->getVkDescriptorSetLayout());
    }

    // Update UBO bindings
    for (size_t i = 0; i < frameCount; i++) {
        auto* vkUBO = static_cast<VulkanRHIBuffer*>(m_uniformBuffers_[i].get());
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = vkUBO->getVkBuffer();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(NaniteDebugUBO);
        
        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_descriptorSets_[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;
        
        vkUpdateDescriptorSets(vkDevice->getVkDevice(), 1, &descriptorWrite, 0, nullptr);
    }
}

void NaniteDebugPass::createPipeline(VkRenderPass renderPass) {
    constexpr uint32_t vertexStride = sizeof(float) * 11;  // pos(3) + normal(3) + texCoord(2) + tangent(3)

    auto builder = rhiDevice_->createGraphicsPipelineBuilder();
    auto* vkBuilder = static_cast<VulkanGraphicsPipelineBuilder*>(builder.get());

    // Set native render pass first (VulkanGraphicsPipelineBuilder-specific)
    vkBuilder->setNativeRenderPass(renderPass, 0);

    vkBuilder
        ->setVertexShader("shaders/nanite/cluster_debug_vert.spv")
        .setFragmentShader("shaders/nanite/cluster_debug_frag.spv")
        .addVertexBinding(0, vertexStride, RHIVertexInputRate::Vertex)
        .addVertexAttribute(0, 0, RHIFormat::R32G32B32_SFLOAT, 0)                      // Position
        .addVertexAttribute(0, 1, RHIFormat::R32G32B32_SFLOAT, sizeof(float) * 3)      // Normal
        .addVertexAttribute(0, 2, RHIFormat::R32G32_SFLOAT,    sizeof(float) * 6)      // TexCoord
        .addVertexAttribute(0, 3, RHIFormat::R32G32B32_SFLOAT, sizeof(float) * 8)      // Tangent
        .setTopology(RHIPrimitiveTopology::TriangleList)
        .setCullMode(RHICullMode::None)   // 禁用剔除，显示双面
        .setFrontFace(RHIFrontFace::CounterClockwise)
        .setPolygonMode(RHIPolygonMode::Fill)
        .setDepthTest(true, true, RHICompareOp::Less)
        .setSampleCount(RHISampleCount::Count1)
        .setColorAttachmentCount(1)
        .addBindingLayout(m_bindingLayout_.get())
        .addPushConstant(RHIShaderStage::Vertex | RHIShaderStage::Fragment,
                         0, sizeof(ClusterDebugPushConstants));

    m_pipeline_ = vkBuilder->build();

    std::cout << "[NaniteDebugPass] Graphics pipeline created (RHI Pipeline Builder)" << std::endl;
}

// ============================================
// Cluster 渲染数据构建
// ============================================

void NaniteDebugPass::buildRenderData() {
    if (!m_naniteManager) {
        return;
    }
    
    std::vector<std::string> meshNames;
    if (m_renderAllMeshes) {
        meshNames = m_naniteManager->getAllMeshNames();
    } else if (!m_targetMeshName.empty()) {
        meshNames.push_back(m_targetMeshName);
    }
    
    if (meshNames.empty()) {
        return;
    }
    
    m_totalVertexCount = 0;
    m_totalIndexCount = 0;
    m_totalClusterCount = 0;
    m_lod0ClusterCount = 0;
    
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
        
        if (!clusterizedMesh->lodLevels.empty()) {
            m_lod0ClusterCount += clusterizedMesh->lodLevels[0].clusterCount;
            std::cout << "[NaniteDebugPass] Mesh '" << meshName << "' LOD0 clusters: " 
                      << clusterizedMesh->lodLevels[0].clusterCount 
                      << " (total LOD levels: " << clusterizedMesh->lodLevels.size() << ")" << std::endl;
        } else {
            m_lod0ClusterCount += static_cast<uint32_t>(clusterizedMesh->clusters.size());
            std::cout << "[NaniteDebugPass] Mesh '" << meshName << "' no LOD info, using all " 
                      << clusterizedMesh->clusters.size() << " clusters as LOD0" << std::endl;
        }
    }
    
    if (m_totalVertexCount == 0 || m_totalIndexCount == 0) {
        std::cout << "[NaniteDebugPass] No vertex/index data" << std::endl;
        return;
    }
    
    // 构建顶点数据（与 GBuffer 格式一致：pos(3) + normal(3) + texCoord(2) + tangent(3)）
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
        meshInfo.modelMatrix = glm::mat4(1.0f);
        
        for (uint32_t clusterIdx = 0; clusterIdx < clusterizedMesh->clusters.size(); clusterIdx++) {
            const auto& cluster = clusterizedMesh->clusters[clusterIdx];
            
            ClusterRenderData renderData;
            renderData.vertexOffset = currentVertexOffset;
            renderData.indexOffset = currentIndexOffset;
            renderData.indexCount = static_cast<uint32_t>(cluster.localIndices.size());
            renderData.clusterIndex = globalClusterIndex;
            
            for (const auto& vertex : cluster.vertices) {
                vertexData.push_back(vertex.position.x);
                vertexData.push_back(vertex.position.y);
                vertexData.push_back(vertex.position.z);
                vertexData.push_back(vertex.normal.x);
                vertexData.push_back(vertex.normal.y);
                vertexData.push_back(vertex.normal.z);
                vertexData.push_back(vertex.uv.x);
                vertexData.push_back(vertex.uv.y);
                vertexData.push_back(vertex.tangent.x);
                vertexData.push_back(vertex.tangent.y);
                vertexData.push_back(vertex.tangent.z);
            }
            
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
    
    // 创建 GPU 缓冲 (RHI)
    uint64_t vertexBufferSize = vertexData.size() * sizeof(float);
    uint64_t indexBufferSize = indexData.size() * sizeof(uint32_t);
    
    {
        RHIBufferDesc desc{};
        desc.size = vertexBufferSize;
        desc.usage = RHIBufferUsage::Vertex;
        desc.memoryUsage = RHIMemoryUsage::GPUOnly;
        m_vertexBuffer_ = rhiDevice_->createBuffer(desc);
        m_vertexBuffer_->uploadData(vertexData.data(), vertexBufferSize);
    }
    
    {
        RHIBufferDesc desc{};
        desc.size = indexBufferSize;
        desc.usage = RHIBufferUsage::Index;
        desc.memoryUsage = RHIMemoryUsage::GPUOnly;
        m_indexBuffer_ = rhiDevice_->createBuffer(desc);
        m_indexBuffer_->uploadData(indexData.data(), indexBufferSize);
    }
    
    m_renderDataBuilt = true;
    
    std::cout << "[NaniteDebugPass] Render data built: " 
              << m_totalClusterCount << " clusters, "
              << m_meshRenderInfos.size() << " mesh(es)" << std::endl;
    
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
    if (frameIndex >= m_uniformBuffers_.size()) return;
    
    NaniteDebugUBO ubo{};
    ubo.view = viewMatrix;
    ubo.proj = projMatrix;
    ubo.viewPos = glm::vec4(viewPos, 1.0f);
    ubo.lightPos = glm::vec4(lightPos, 1.0f);
    ubo.lightColor = glm::vec4(lightColor, 1.0f);
    
    void* ptr = m_uniformBuffers_[frameIndex]->map();
    memcpy(ptr, &ubo, sizeof(ubo));
    m_uniformBuffers_[frameIndex]->unmap();
}

void NaniteDebugPass::recordCommands(VkCommandBuffer commandBuffer, 
                                     uint32_t frameIndex,
                                     const glm::mat4& modelMatrix) {
    if (!m_initialized || !enabled) {
        return;
    }
    
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
    
    auto* vkPipeline = static_cast<VulkanRHIPipeline*>(m_pipeline_.get());
    
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline->getVkPipeline());
    
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
    
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vkPipeline->getVkPipelineLayout(), 0, 1, 
                            &m_descriptorSets_[frameIndex], 0, nullptr);
    
    auto* vkVB = static_cast<VulkanRHIBuffer*>(m_vertexBuffer_.get());
    auto* vkIB = static_cast<VulkanRHIBuffer*>(m_indexBuffer_.get());
    VkBuffer vertexBuffers[] = { vkVB->getVkBuffer() };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, vkIB->getVkBuffer(), 0, VK_INDEX_TYPE_UINT32);
    
    glm::mat4 normalMatrix = glm::transpose(glm::inverse(modelMatrix));
    
    for (const auto& clusterData : m_clusterRenderData) {
        ClusterDebugPushConstants pushConstants{};
        pushConstants.model = modelMatrix;
        pushConstants.normalMatrix = normalMatrix;
        pushConstants.clusterIndex = clusterData.clusterIndex;
        pushConstants.totalClusters = m_totalClusterCount;
        pushConstants.debugMode = static_cast<uint32_t>(m_debugMode);
        pushConstants.padding = 0.0f;
        
        vkCmdPushConstants(commandBuffer, vkPipeline->getVkPipelineLayout(),
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
    
    auto* vkPipeline = static_cast<VulkanRHIPipeline*>(m_pipeline_.get());
    
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline->getVkPipeline());
    
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
    
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vkPipeline->getVkPipelineLayout(), 0, 1, 
                            &m_descriptorSets_[frameIndex], 0, nullptr);
    
    auto* vkVB = static_cast<VulkanRHIBuffer*>(m_vertexBuffer_.get());
    auto* vkIB = static_cast<VulkanRHIBuffer*>(m_indexBuffer_.get());
    VkBuffer vertexBuffers[] = { vkVB->getVkBuffer() };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, vkIB->getVkBuffer(), 0, VK_INDEX_TYPE_UINT32);
    
    for (const auto& meshInfo : m_meshRenderInfos) {
        glm::mat4 modelMatrix = glm::mat4(1.0f);
        auto it = meshMatrices.find(meshInfo.meshName);
        if (it != meshMatrices.end()) {
            modelMatrix = it->second;
        }
        
        glm::mat4 normalMatrix = glm::transpose(glm::inverse(modelMatrix));
        
        for (const auto& clusterData : meshInfo.clusters) {
            ClusterDebugPushConstants pushConstants{};
            pushConstants.model = modelMatrix;
            pushConstants.normalMatrix = normalMatrix;
            pushConstants.clusterIndex = clusterData.clusterIndex;
            pushConstants.totalClusters = m_totalClusterCount;
            pushConstants.debugMode = static_cast<uint32_t>(m_debugMode);
            pushConstants.padding = 0.0f;
            
            vkCmdPushConstants(commandBuffer, vkPipeline->getVkPipelineLayout(),
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
    
    if (!m_renderDataBuilt || m_clusterRenderData.empty()) {
        return;
    }
    
    // =====================================================
    // 用ClusterCullingPass 获取 GPU culling 结果
    // =====================================================
    std::set<uint32_t> visibleSet;
    
    constexpr bool DEBUG_RENDER_ALL_CLUSTERS = false;
    constexpr bool DEBUG_RENDER_LOD0_ONLY = false;
    
    if (DEBUG_RENDER_ALL_CLUSTERS) {
        if (DEBUG_RENDER_LOD0_ONLY) {
            uint32_t globalClusterOffset = 0;
            
            if (naniteManager) {
                for (const auto& meshInfo : m_meshRenderInfos) {
                    auto clusterizedMesh = naniteManager->getMesh(meshInfo.meshName);
                    if (!clusterizedMesh) continue;
                    
                    uint32_t lod0Count = 0;
                    if (!clusterizedMesh->lodLevels.empty()) {
                        lod0Count = clusterizedMesh->lodLevels[0].clusterCount;
                    } else {
                        lod0Count = static_cast<uint32_t>(clusterizedMesh->clusters.size());
                    }
                    
                    for (uint32_t i = 0; i < lod0Count; ++i) {
                        visibleSet.insert(globalClusterOffset + i);
                    }
                    
                    globalClusterOffset += static_cast<uint32_t>(clusterizedMesh->clusters.size());
                }
            } else {
                for (uint32_t i = 0; i < m_lod0ClusterCount; ++i) {
                    visibleSet.insert(i);
                }
            }
            
            std::cout << "[NaniteDebugPass] LOD0 only mode: " << visibleSet.size() 
                      << " clusters visible (total: " << m_totalClusterCount << ")" << std::endl;
        } else {
            for (uint32_t i = 0; i < m_totalClusterCount; ++i) {
                visibleSet.insert(i);
            }
        }
    }
    else {
        std::set<uint32_t> frustumVisible;
        if (m_clusterCullingPass) {
            const auto& visibleIndices = m_clusterCullingPass->getVisibleIndices();
            for (uint32_t idx : visibleIndices) {
                frustumVisible.insert(idx);
            }
        }
        
        if (frustumVisible.empty()) {
            for (uint32_t i = 0; i < m_totalClusterCount; ++i) {
                frustumVisible.insert(i);
            }
        }
        
        if (m_naniteManager) {
            const auto& allGPUData = m_naniteManager->getAllGPUClusterData();
            glm::vec3 cameraPos = m_naniteManager->getLastCameraPosition();
            
            for (uint32_t idx : frustumVisible) {
                if (idx >= allGPUData.size()) continue;
                const auto& cluster = allGPUData[idx];
                
                uint32_t rootIdx = idx;
                uint32_t safetyCounter = 0;
                while (allGPUData[rootIdx].parentGroupIndex != 0xFFFFFFFF && 
                       allGPUData[rootIdx].parentGroupIndex < allGPUData.size() &&
                       safetyCounter < 10) {
                    rootIdx = allGPUData[rootIdx].parentGroupIndex;
                    safetyCounter++;
                }
                
                glm::vec3 rootCenter(allGPUData[rootIdx].boundingSphere.x, 
                                     allGPUData[rootIdx].boundingSphere.y, 
                                     allGPUData[rootIdx].boundingSphere.z);
                float dist = glm::length(rootCenter - cameraPos);
                
                uint32_t targetLOD = 0;
                if (dist > 200.0f) targetLOD = 7;
                else if (dist > 120.0f) targetLOD = 6;
                else if (dist > 70.0f) targetLOD = 5;
                else if (dist > 45.0f) targetLOD = 4;
                else if (dist > 30.0f) targetLOD = 3;
                else if (dist > 18.0f) targetLOD = 2;
                else if (dist > 10.0f) targetLOD = 1;
                else targetLOD = 0;
                
                if (cluster.lodLevel == targetLOD) {
                    visibleSet.insert(idx);
                } else if (cluster.parentGroupIndex == 0xFFFFFFFF && targetLOD > cluster.lodLevel) {
                    visibleSet.insert(idx);
                }
            }
        } else {
            visibleSet = frustumVisible;
        }
    }
    
    // 绑定管线
    auto* vkPipeline = static_cast<VulkanRHIPipeline*>(m_pipeline_.get());
    
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline->getVkPipeline());
    
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
    
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            vkPipeline->getVkPipelineLayout(), 0, 1, 
                            &m_descriptorSets_[frameIndex], 0, nullptr);
    
    auto* vkVB = static_cast<VulkanRHIBuffer*>(m_vertexBuffer_.get());
    auto* vkIB = static_cast<VulkanRHIBuffer*>(m_indexBuffer_.get());
    VkBuffer vertexBuffers[] = { vkVB->getVkBuffer() };
    VkDeviceSize offsets_arr[] = { 0 };
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets_arr);
    vkCmdBindIndexBuffer(commandBuffer, vkIB->getVkBuffer(), 0, VK_INDEX_TYPE_UINT32);
    
    uint32_t drawnClusters = 0;
    
    for (const auto& meshInfo : m_meshRenderInfos) {
        glm::mat4 modelMatrix = glm::mat4(1.0f);
        auto it = meshMatrices.find(meshInfo.meshName);
        if (it != meshMatrices.end()) {
            modelMatrix = it->second;
        }
        
        glm::mat4 normalMatrix = glm::transpose(glm::inverse(modelMatrix));
        
        for (const auto& clusterData : meshInfo.clusters) {
            if (visibleSet.find(clusterData.clusterIndex) == visibleSet.end()) {
                continue;
            }
            
            ClusterDebugPushConstants pushConstants{};
            pushConstants.model = modelMatrix;
            pushConstants.normalMatrix = normalMatrix;
            pushConstants.clusterIndex = clusterData.clusterIndex;
            pushConstants.totalClusters = m_totalClusterCount;
            pushConstants.debugMode = static_cast<uint32_t>(m_debugMode);
            pushConstants.padding = 0.0f;
            
            vkCmdPushConstants(commandBuffer, vkPipeline->getVkPipelineLayout(),
                              VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                              0, sizeof(ClusterDebugPushConstants), &pushConstants);
            
            vkCmdDrawIndexed(commandBuffer, clusterData.indexCount, 1, 
                             clusterData.indexOffset, 0, 0);
            
            drawnClusters++;
        }
    }
    
    // ======== LOD 追踪和实时输出========
    std::unordered_map<uint32_t, uint32_t> lodClusterCounts;
    
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
    
    for (uint32_t idx : visibleSet) {
        auto it = clusterToLOD.find(idx);
        if (it != clusterToLOD.end()) {
            lodClusterCounts[it->second]++;
        }
    }
    
    static uint32_t frameCounter = 0;
    if (++frameCounter % 60 == 0) {
        std::cout << "\r[LOD] Drawn:" << drawnClusters << "/" << m_totalClusterCount 
                  << " | GPU Culling Mode"
                  << " | Visible:" << visibleSet.size()
                  << " | ";
        
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