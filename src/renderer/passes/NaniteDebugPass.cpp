#include "NaniteDebugPass.h"
#include "../nanite/NaniteManager.h"
#include "../nanite/NaniteCluster.h"
#include "ClusterCullingPass.h"

// Pure RHI headers — NO Vulkan backend headers
#include "Mesh.h"
#include "RHIDevice.h"
#include "RHISwapChain.h"
#include "RHIBuffer.h"
#include "RHIDescriptor.h"
#include "RHIPipeline.h"
#include "RHIRenderPass.h"
#include "RHICommandBuffer.h"

#include <stdexcept>
#include <iostream>
#include <array>
#include <cstring>
#include <set>
#include <unordered_set>

// ============================================
// 构造与析构
// ============================================

NaniteDebugPass::NaniteDebugPass(RHIDevice* rhiDevice,
                                 RHISwapChain* rhiSwapChain,
                                 std::shared_ptr<Nanite::NaniteManager> naniteManager)
    : RenderPassBase(rhiDevice, rhiSwapChain->getExtent().width, rhiSwapChain->getExtent().height)
    , rhiDevice_(rhiDevice)
    , rhiSwapChain_(rhiSwapChain)
    , m_naniteManager(naniteManager) {
    passName = "Nanite Debug Pass";
}

NaniteDebugPass::~NaniteDebugPass() { cleanup(); }

void NaniteDebugPass::initialize(RHIRenderPass* externalRenderPass) {
    if (m_initialized) return;
    externalRenderPass_ = externalRenderPass;
    createBindingLayout();
    createUniformBuffers();
    createDescriptorSets();
    createPipeline();
    m_initialized = true;
    std::cout << "[NaniteDebugPass] Initialized (Pure RHI)" << std::endl;
}

void NaniteDebugPass::cleanup() {
    if (!rhiDevice_) return;
    rhiDevice_->waitIdle();
    m_vertexBuffer_.reset(); m_indexBuffer_.reset();
    m_clusterRenderData.clear();
    m_uniformBuffers_.clear(); m_bindingGroups_.clear();
    m_pipeline_.reset(); m_bindingLayout_.reset();
    m_initialized = false; m_renderDataBuilt = false;
}

// ============================================
// RHI 资源创建
// ============================================

void NaniteDebugPass::createBindingLayout() {
    RHIBindingLayoutDesc desc;
    desc.entries.push_back({0, RHIDescriptorType::UniformBuffer,
                            RHIShaderStage::Vertex | RHIShaderStage::Fragment, 1});
    m_bindingLayout_ = rhiDevice_->createBindingLayout(desc);
}

void NaniteDebugPass::createUniformBuffers() {
    size_t frameCount = rhiSwapChain_->getImageCount();
    m_uniformBuffers_.resize(frameCount);
    for (size_t i = 0; i < frameCount; i++) {
        RHIBufferDesc d{}; d.size = sizeof(NaniteDebugUBO);
        d.usage = RHIBufferUsage::Uniform; d.memoryUsage = RHIMemoryUsage::CPUToGPU;
        m_uniformBuffers_[i] = rhiDevice_->createBuffer(d);
    }
}

void NaniteDebugPass::createDescriptorSets() {
    size_t frameCount = rhiSwapChain_->getImageCount();
    m_bindingGroups_.resize(frameCount);
    for (size_t i = 0; i < frameCount; ++i) {
        m_bindingGroups_[i] = rhiDevice_->allocateBindingGroup(m_bindingLayout_.get());
        m_bindingGroups_[i]->updateBuffer(0, m_uniformBuffers_[i].get(), 0, sizeof(NaniteDebugUBO));
    }
}

void NaniteDebugPass::createPipeline() {
    const auto vertexAttrs = Vertex::getRHIAttributes();

    auto builder = rhiDevice_->createGraphicsPipelineBuilder();
    builder->setVertexShader("shaders/nanite/cluster_debug_vert.spv")
        .setFragmentShader("shaders/nanite/cluster_debug_frag.spv")
        .addVertexBinding(0, Vertex::getStride(), RHIVertexInputRate::Vertex);
    for (const auto& a : vertexAttrs) {
        builder->addVertexAttribute(a.binding, a.location, a.format, a.offset);
    }
    builder->setTopology(RHIPrimitiveTopology::TriangleList)
        .setCullMode(RHICullMode::None)
        .setFrontFace(RHIFrontFace::CounterClockwise)
        .setPolygonMode(RHIPolygonMode::Fill)
        .setDepthTest(true, true, RHICompareOp::Less)
        .setSampleCount(RHISampleCount::Count1)
        .setColorAttachmentCount(1)
        .addBindingLayout(m_bindingLayout_.get())
        .addPushConstant(RHIShaderStage::Vertex | RHIShaderStage::Fragment, 0, sizeof(ClusterDebugPushConstants))
        .setRenderPass(externalRenderPass_);

    m_pipeline_ = builder->build();
    std::cout << "[NaniteDebugPass] Pipeline created (Pure RHI)" << std::endl;
}

// ============================================
// Cluster 渲染数据构建 (same business logic, no Vulkan)
// ============================================

void NaniteDebugPass::buildRenderData() {
    if (!m_naniteManager) return;
    std::vector<std::string> meshNames;
    if (m_renderAllMeshes) meshNames = m_naniteManager->getAllMeshNames();
    else if (!m_targetMeshName.empty()) meshNames.push_back(m_targetMeshName);
    if (meshNames.empty()) return;

    m_totalVertexCount = m_totalIndexCount = m_totalClusterCount = m_lod0ClusterCount = 0;
    std::vector<std::pair<std::string, std::shared_ptr<Nanite::ClusterizedMesh>>> meshesToRender;
    for (const auto& name : meshNames) {
        auto cm = m_naniteManager->getMesh(name);
        if (!cm || cm->clusters.empty()) continue;
        meshesToRender.push_back({name, cm});
        for (const auto& c : cm->clusters) {
            m_totalVertexCount += c.vertexCount;
            m_totalIndexCount += static_cast<uint32_t>(c.localIndices.size());
        }
        m_totalClusterCount += static_cast<uint32_t>(cm->clusters.size());
        m_lod0ClusterCount += !cm->lodLevels.empty() ? cm->lodLevels[0].clusterCount : static_cast<uint32_t>(cm->clusters.size());
    }
    if (m_totalVertexCount == 0) return;

    std::vector<float> vertexData; vertexData.reserve(m_totalVertexCount * 11);
    std::vector<uint32_t> indexData; indexData.reserve(m_totalIndexCount);
    m_clusterRenderData.clear(); m_meshRenderInfos.clear();

    uint32_t curVOff = 0, curIOff = 0, globalCI = 0;
    for (const auto& [meshName, cm] : meshesToRender) {
        MeshRenderInfo mi; mi.meshName = meshName; mi.modelMatrix = glm::mat4(1.0f);
        for (uint32_t ci = 0; ci < cm->clusters.size(); ci++) {
            const auto& cluster = cm->clusters[ci];
            ClusterRenderData rd{curVOff, curIOff, static_cast<uint32_t>(cluster.localIndices.size()), globalCI, cluster.lodLevel, cluster.vertexCount};
            for (const auto& v : cluster.vertices) {
                vertexData.insert(vertexData.end(), {v.position.x, v.position.y, v.position.z,
                    v.normal.x, v.normal.y, v.normal.z, v.uv.x, v.uv.y,
                    v.tangent.x, v.tangent.y, v.tangent.z});
            }
            for (uint32_t li : cluster.localIndices) indexData.push_back(curVOff + li);
            m_clusterRenderData.push_back(rd); mi.clusters.push_back(rd);
            curVOff += cluster.vertexCount; curIOff += rd.indexCount; globalCI++;
        }
        m_meshRenderInfos.push_back(std::move(mi));
    }

    { RHIBufferDesc d{}; d.size = vertexData.size() * sizeof(float);
      d.usage = RHIBufferUsage::Vertex; d.memoryUsage = RHIMemoryUsage::GPUOnly;
      m_vertexBuffer_ = rhiDevice_->createBuffer(d);
      m_vertexBuffer_->uploadData(vertexData.data(), d.size); }
    { RHIBufferDesc d{}; d.size = indexData.size() * sizeof(uint32_t);
      d.usage = RHIBufferUsage::Index; d.memoryUsage = RHIMemoryUsage::GPUOnly;
      m_indexBuffer_ = rhiDevice_->createBuffer(d);
      m_indexBuffer_->uploadData(indexData.data(), d.size); }

    m_renderDataBuilt = true;
    std::cout << "[NaniteDebugPass] Render data built: " << m_totalClusterCount << " clusters" << std::endl;
}

// ============================================
// UBO
// ============================================

void NaniteDebugPass::updateUniforms(uint32_t frameIndex, const glm::mat4& viewMatrix,
                                     const glm::mat4& projMatrix, const glm::vec3& viewPos,
                                     const glm::vec3& lightPos, const glm::vec3& lightColor) {
    if (frameIndex >= m_uniformBuffers_.size()) return;
    NaniteDebugUBO ubo{};
    ubo.view = viewMatrix; ubo.proj = projMatrix;
    ubo.viewPos = glm::vec4(viewPos, 1); ubo.lightPos = glm::vec4(lightPos, 1); ubo.lightColor = glm::vec4(lightColor, 1);
    void* ptr = m_uniformBuffers_[frameIndex]->map();
    memcpy(ptr, &ubo, sizeof(ubo));
    m_uniformBuffers_[frameIndex]->unmap();
}

// ============================================
// recordCommands (Pure RHI)
// ============================================

void NaniteDebugPass::recordCommands(RHICommandBuffer* cmd, uint32_t frameIndex, const glm::mat4& modelMatrix) {
    if (!m_initialized || !enabled) return;
    if (!m_renderDataBuilt) { buildRenderData(); }
    if (!m_renderDataBuilt || m_clusterRenderData.empty()) return;

    auto ext = rhiSwapChain_->getExtent();
    cmd->bindGraphicsPipeline(m_pipeline_.get());
    cmd->setViewport(0, 0, float(ext.width), float(ext.height));
    cmd->setScissor(0, 0, ext.width, ext.height);
    cmd->setBindingGroup(0, m_bindingGroups_[frameIndex].get());
    cmd->bindVertexBuffer(0, m_vertexBuffer_.get());
    cmd->bindIndexBuffer(m_indexBuffer_.get(), 0, RHIIndexType::UInt32);

    glm::mat4 normalMat = glm::transpose(glm::inverse(modelMatrix));
    for (const auto& cd : m_clusterRenderData) {
        ClusterDebugPushConstants pc{};
        pc.model = modelMatrix; pc.normalMatrix = normalMat;
        pc.clusterIndex = cd.clusterIndex; pc.totalClusters = m_totalClusterCount;
        pc.debugMode = static_cast<uint32_t>(m_debugMode);
        pc.lodLevel = cd.lodLevel;
        cmd->pushConstants(RHIShaderStage::Vertex | RHIShaderStage::Fragment, 0, sizeof(pc), &pc);
        cmd->drawIndexed(cd.indexCount, 1, cd.indexOffset, 0, 0);
    }
}

void NaniteDebugPass::recordCommandsMultiMesh(RHICommandBuffer* cmd, uint32_t frameIndex,
                                              const std::unordered_map<std::string, glm::mat4>& meshMatrices) {
    if (!m_initialized || !enabled) return;
    if (!m_renderDataBuilt) { buildRenderData(); }
    if (!m_renderDataBuilt || m_meshRenderInfos.empty()) return;

    auto ext = rhiSwapChain_->getExtent();
    cmd->bindGraphicsPipeline(m_pipeline_.get());
    cmd->setViewport(0, 0, float(ext.width), float(ext.height));
    cmd->setScissor(0, 0, ext.width, ext.height);
    cmd->setBindingGroup(0, m_bindingGroups_[frameIndex].get());
    cmd->bindVertexBuffer(0, m_vertexBuffer_.get());
    cmd->bindIndexBuffer(m_indexBuffer_.get(), 0, RHIIndexType::UInt32);

    for (const auto& mi : m_meshRenderInfos) {
        glm::mat4 model = glm::mat4(1.0f);
        auto it = meshMatrices.find(mi.meshName);
        if (it != meshMatrices.end()) model = it->second;
        glm::mat4 normalMat = glm::transpose(glm::inverse(model));
        for (const auto& cd : mi.clusters) {
            ClusterDebugPushConstants pc{};
            pc.model = model; pc.normalMatrix = normalMat;
            pc.clusterIndex = cd.clusterIndex; pc.totalClusters = m_totalClusterCount;
            pc.debugMode = static_cast<uint32_t>(m_debugMode);
            pc.lodLevel = cd.lodLevel;
            cmd->pushConstants(RHIShaderStage::Vertex | RHIShaderStage::Fragment, 0, sizeof(pc), &pc);
            cmd->drawIndexed(cd.indexCount, 1, cd.indexOffset, 0, 0);
        }
    }
}

void NaniteDebugPass::recordCommandsWithLOD(RHICommandBuffer* cmd, uint32_t frameIndex,
                                            const std::unordered_map<std::string, glm::mat4>& meshMatrices,
                                            Nanite::NaniteManager* naniteManager) {
    if (!m_initialized || !enabled) return;
    if (!m_renderDataBuilt || m_clusterRenderData.empty()) return;

    // GPU 选择结果（含 DAG LOD 选择）：直接绘制回读到的可见 cluster 列表
    m_drawnStats = DrawnStats{};
    m_drawnStats.totalClusters = m_totalClusterCount;

    std::vector<uint32_t> visibleIndices;
    if (m_clusterCullingPass) {
        const auto& vi = m_clusterCullingPass->getVisibleIndices();
        visibleIndices.assign(vi.begin(), vi.end());
    }

    // 回读尚未就绪（前几帧 / 剔除未初始化）：降级为只画 LOD0，避免所有 LOD 重叠
    if (visibleIndices.empty() && naniteManager) {
        const auto& allGPU = naniteManager->getAllGPUClusterData();
        for (uint32_t i = 0; i < allGPU.size(); ++i) {
            if (allGPU[i].lodLevel == 0) visibleIndices.push_back(i);
        }
    }
    if (visibleIndices.empty()) {
        for (uint32_t i = 0; i < m_totalClusterCount; ++i) visibleIndices.push_back(i);
    }

    // 诊断模式: 强制显示指定 LOD（层级不存在时用根节点兜底）
    if (m_forceLOD >= 0 && naniteManager) {
        const auto& allGPU = naniteManager->getAllGPUClusterData();
        const uint32_t forced = static_cast<uint32_t>(m_forceLOD);
        std::vector<uint32_t> filtered;
        filtered.reserve(visibleIndices.size());
        for (uint32_t idx : visibleIndices) {
            if (idx >= allGPU.size()) continue;
            const auto& c = allGPU[idx];
            bool isRoot = (c.parentGroupIndex == 0xFFFFFFFFu);
            if (c.lodLevel == forced || (isRoot && c.lodLevel <= forced)) filtered.push_back(idx);
        }
        visibleIndices.swap(filtered);
    }

    std::unordered_set<uint32_t> visibleSet(visibleIndices.begin(), visibleIndices.end());
    m_drawnStats.visibleClusters = static_cast<uint32_t>(visibleIndices.size());

    auto ext = rhiSwapChain_->getExtent();
    cmd->bindGraphicsPipeline(m_pipeline_.get());
    cmd->setViewport(0, 0, float(ext.width), float(ext.height));
    cmd->setScissor(0, 0, ext.width, ext.height);
    cmd->setBindingGroup(0, m_bindingGroups_[frameIndex].get());
    cmd->bindVertexBuffer(0, m_vertexBuffer_.get());
    cmd->bindIndexBuffer(m_indexBuffer_.get(), 0, RHIIndexType::UInt32);

    for (const auto& mi : m_meshRenderInfos) {
        glm::mat4 model = glm::mat4(1.0f);
        auto it = meshMatrices.find(mi.meshName); if (it != meshMatrices.end()) model = it->second;
        glm::mat4 normalMat = glm::transpose(glm::inverse(model));
        for (const auto& cd : mi.clusters) {
            if (visibleSet.find(cd.clusterIndex) == visibleSet.end()) continue;
            ClusterDebugPushConstants pc{};
            pc.model = model; pc.normalMatrix = normalMat;
            pc.clusterIndex = cd.clusterIndex; pc.totalClusters = m_totalClusterCount;
            pc.debugMode = static_cast<uint32_t>(m_debugMode);
            pc.lodLevel = cd.lodLevel;
            cmd->pushConstants(RHIShaderStage::Vertex | RHIShaderStage::Fragment, 0, sizeof(pc), &pc);
            cmd->drawIndexed(cd.indexCount, 1, cd.indexOffset, 0, 0);
            m_drawnStats.drawnClusters++;
            m_drawnStats.drawnTriangles += cd.indexCount / 3;
            m_drawnStats.drawnVertices += cd.vertexCount;
            if (cd.lodLevel < 8) m_drawnStats.lodClusterCounts[cd.lodLevel]++;
        }
    }

    // ======== 实时输出（每 60 帧）========
    static uint32_t frameCounter = 0;
    if (++frameCounter % 60 == 0) {
        std::cout << "\r[LOD] Drawn:" << m_drawnStats.drawnClusters << "/" << m_totalClusterCount
                  << " tris=" << m_drawnStats.drawnTriangles
                  << " verts=" << m_drawnStats.drawnVertices
                  << " | GPU LOD Selection"
                  << " | Visible:" << m_drawnStats.visibleClusters
                  << " | [";
        for (uint32_t lod = 0; lod < 8; ++lod) {
            if (m_drawnStats.lodClusterCounts[lod] > 0) {
                std::cout << "L" << lod << ":" << m_drawnStats.lodClusterCounts[lod] << " ";
            }
        }
        std::cout << "]                " << std::flush;
    }
}

// ============================================
// Missing method implementations
// ============================================

void NaniteDebugPass::ensureRenderDataBuilt() {
    if (!m_renderDataBuilt) {
        buildRenderData();
    }
}

void NaniteDebugPass::resize(uint32_t width, uint32_t height) {
    // Debug pass uses swapchain extent directly; nothing to recreate here.
    // Pipeline viewport/scissor are set dynamically in recordCommands.
}

bool NaniteDebugPass::hasClusterData() const {
    return m_renderDataBuilt && m_totalClusterCount > 0;
}

void NaniteDebugPass::cycleDebugMode() {
    uint32_t next = (static_cast<uint32_t>(m_debugMode) + 1) % 4;
    m_debugMode = static_cast<NaniteDebugMode>(next);
}

const char* NaniteDebugPass::getDebugModeName() const {
    switch (m_debugMode) {
        case NaniteDebugMode::ClusterColor: return "Cluster Color";
        case NaniteDebugMode::Normal:       return "Normal";
        case NaniteDebugMode::LOD:          return "LOD";
        case NaniteDebugMode::HashColor:    return "Hash Color";
        default:                            return "Unknown";
    }
}
