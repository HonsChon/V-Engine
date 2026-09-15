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
    createCompactionPipeline();
    createPipeline();
    m_initialized = true;
    std::cout << "[NaniteDebugPass] Initialized (GPU-driven indirect, Pure RHI)" << std::endl;
}

void NaniteDebugPass::cleanup() {
    if (!rhiDevice_) return;
    rhiDevice_->waitIdle();
    m_vertexBuffer_.reset(); m_indexBuffer_.reset();
    m_geomTableBuffer_.reset();
    m_expVertexBuffer_.reset(); m_drawArgsBuffer_.reset();
    m_clusterRenderData.clear();
    m_uniformBuffers_.clear(); m_bindingGroups_.clear();
    m_compactionGroup_.reset(); m_compactionLayout_.reset(); m_compactionPipeline_.reset();
    m_dummyTransformBuffer_.reset();
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
    desc.entries.push_back({1, RHIDescriptorType::StorageBuffer,
                            RHIShaderStage::Vertex, 1});
    desc.entries.push_back({2, RHIDescriptorType::StorageBuffer,
                            RHIShaderStage::Vertex, 1});
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

void NaniteDebugPass::createCompactionPipeline() {
    RHIBindingLayoutDesc layoutDesc;
    layoutDesc.entries.push_back({0, RHIDescriptorType::StorageBuffer, RHIShaderStage::Compute, 1});
    layoutDesc.entries.push_back({1, RHIDescriptorType::StorageBuffer, RHIShaderStage::Compute, 1});
    layoutDesc.entries.push_back({2, RHIDescriptorType::StorageBuffer, RHIShaderStage::Compute, 1});
    layoutDesc.entries.push_back({3, RHIDescriptorType::StorageBuffer, RHIShaderStage::Compute, 1});
    layoutDesc.entries.push_back({4, RHIDescriptorType::StorageBuffer, RHIShaderStage::Compute, 1});
    layoutDesc.entries.push_back({5, RHIDescriptorType::StorageBuffer, RHIShaderStage::Compute, 1});
    layoutDesc.entries.push_back({6, RHIDescriptorType::StorageBuffer, RHIShaderStage::Compute, 1});
    m_compactionLayout_ = rhiDevice_->createBindingLayout(layoutDesc);

    auto builder = rhiDevice_->createComputePipelineBuilder();
    builder->setComputeShader("shaders/nanite/build_visible_geometry.comp.spv")
        .addBindingLayout(m_compactionLayout_.get())
        .addPushConstant(RHIShaderStage::Compute, 0, 16);
    m_compactionPipeline_ = builder->build();

    m_compactionGroup_ = rhiDevice_->allocateBindingGroup(m_compactionLayout_.get());
    m_compactionBindingsDirty = true;

    // mesh 变换未上传时的兜底(与 ClusterCullingPass 的 dummy 同款)
    { RHIBufferDesc d{}; d.size = sizeof(glm::mat4);
      d.usage = RHIBufferUsage::Storage; d.memoryUsage = RHIMemoryUsage::GPUOnly;
      m_dummyTransformBuffer_ = rhiDevice_->createBuffer(d); }
}

void NaniteDebugPass::createPipeline() {
    const auto vertexAttrs = Vertex::getRHIAttributes();
    const uint32_t stride = Vertex::getStride() + sizeof(uint32_t);  // 44B + clusterIndex

    auto builder = rhiDevice_->createGraphicsPipelineBuilder();
    builder->setVertexShader("shaders/nanite/cluster_debug_vert.spv")
        .setFragmentShader("shaders/nanite/cluster_debug_frag.spv")
        .addVertexBinding(0, stride, RHIVertexInputRate::Vertex);
    for (const auto& a : vertexAttrs) {
        builder->addVertexAttribute(a.binding, a.location, a.format, a.offset);
    }
    // location 4 = clusterIndex(GPU 展开时写入,flat 插值保边界精确)
    builder->addVertexAttribute(0, 4, RHIFormat::R32_UINT, Vertex::getStride());
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
// Cluster 渲染数据构建(合并 VB/IB + 静态几何表 + GPU-driven 缓冲)
// ============================================

void NaniteDebugPass::buildRenderData() {
    if (!m_naniteManager) return;
    std::vector<std::string> meshNames;
    if (m_renderAllMeshes) meshNames = m_naniteManager->getAllMeshNames();
    else if (!m_targetMeshName.empty()) meshNames.push_back(m_targetMeshName);
    if (meshNames.empty()) return;

    // mesh 名 → TransformBuffer 全局下标(与 culling/TransformBuffer 顺序一致)
    std::unordered_map<std::string, uint32_t> meshIndexByPath;
    {
        const auto all = m_naniteManager->getAllMeshNames();
        for (uint32_t i = 0; i < all.size(); ++i) meshIndexByPath[all[i]] = i;
    }

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
    // 静态几何表(每 cluster 2×uvec4):a=(srcIndexOffset,indexCount,dstCornerOffset,meshIndex)
    //                                              b=(lodLevel,isRootFlag,clusterIndex,0)
    std::vector<uint32_t> geomTable; geomTable.reserve(m_totalClusterCount * 8);
    m_clusterRenderData.clear(); m_meshRenderInfos.clear();

    uint32_t curVOff = 0, curIOff = 0, globalCI = 0;
    for (const auto& [meshName, cm] : meshesToRender) {
        MeshRenderInfo mi; mi.meshName = meshName; mi.modelMatrix = glm::mat4(1.0f);
        const uint32_t meshIndex = meshIndexByPath.count(meshName) ? meshIndexByPath[meshName] : 0u;
        for (uint32_t ci = 0; ci < cm->clusters.size(); ci++) {
            const auto& cluster = cm->clusters[ci];
            const uint32_t indexCount = static_cast<uint32_t>(cluster.localIndices.size());
            ClusterRenderData rd{curVOff, curIOff, indexCount, globalCI, cluster.lodLevel, cluster.vertexCount};
            for (const auto& v : cluster.vertices) {
                vertexData.insert(vertexData.end(), {v.position.x, v.position.y, v.position.z,
                    v.normal.x, v.normal.y, v.normal.z, v.uv.x, v.uv.y,
                    v.tangent.x, v.tangent.y, v.tangent.z});
            }
            for (uint32_t li : cluster.localIndices) indexData.push_back(curVOff + li);

            // a
            geomTable.push_back(curIOff);        // srcIndexOffset(索引单位)
            geomTable.push_back(indexCount);     // indexCount
            geomTable.push_back(curIOff);        // dstCornerOffset(= 索引单位前缀和)
            geomTable.push_back(meshIndex);
            // b
            geomTable.push_back(cluster.lodLevel);
            geomTable.push_back(cluster.parentGroupIndex == 0xFFFFFFFFu ? 1u : 0u);  // isRoot
            geomTable.push_back(globalCI);
            geomTable.push_back(0u);

            m_clusterRenderData.push_back(rd); mi.clusters.push_back(rd);
            curVOff += cluster.vertexCount; curIOff += indexCount; globalCI++;
        }
        m_meshRenderInfos.push_back(std::move(mi));
    }

    // ---- 源几何(SSBO 只读,GPU 展开的输入) ----
    { RHIBufferDesc d{}; d.size = vertexData.size() * sizeof(float);
      d.usage = RHIBufferUsage::Storage; d.memoryUsage = RHIMemoryUsage::GPUOnly;
      m_vertexBuffer_ = rhiDevice_->createBuffer(d);
      m_vertexBuffer_->uploadData(vertexData.data(), d.size); }
    { RHIBufferDesc d{}; d.size = indexData.size() * sizeof(uint32_t);
      d.usage = RHIBufferUsage::Storage; d.memoryUsage = RHIMemoryUsage::GPUOnly;
      m_indexBuffer_ = rhiDevice_->createBuffer(d);
      m_indexBuffer_->uploadData(indexData.data(), d.size); }
    { RHIBufferDesc d{}; d.size = geomTable.size() * sizeof(uint32_t);
      d.usage = RHIBufferUsage::Storage; d.memoryUsage = RHIMemoryUsage::GPUOnly;
      m_geomTableBuffer_ = rhiDevice_->createBuffer(d);
      m_geomTableBuffer_->uploadData(geomTable.data(), d.size); }

    // ---- GPU-driven 输出 ----
    { RHIBufferDesc d{}; d.size = static_cast<uint64_t>(m_totalIndexCount) * 48;  // 48B/corner
      d.usage = RHIBufferUsage::Storage | RHIBufferUsage::Vertex;
      d.memoryUsage = RHIMemoryUsage::GPUOnly;
      m_expVertexBuffer_ = rhiDevice_->createBuffer(d); }
    { RHIBufferDesc d{}; d.size = 16;  // {vertexCount, instanceCount=1, firstVertex=0, firstInstance=0}
      d.usage = RHIBufferUsage::Storage | RHIBufferUsage::Indirect;
      d.memoryUsage = RHIMemoryUsage::GPUOnly;
      m_drawArgsBuffer_ = rhiDevice_->createBuffer(d);
      const uint32_t args[4] = { 0, 1, 0, 0 };
      m_drawArgsBuffer_->uploadData(args, sizeof(args), 0); }

    m_compactionBindingsDirty = true;
    m_renderDataBuilt = true;
    std::cout << "[NaniteDebugPass] Render data built: " << m_totalClusterCount
              << " clusters, " << m_totalIndexCount << " indices (GPU-driven)" << std::endl;
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
// GPU-driven 间接绘制(render pass 之外)
// ============================================

void NaniteDebugPass::updateCompactionBindings(RHIBuffer* transformBuffer) {
    if (!m_compactionGroup_ || !m_renderDataBuilt) return;
    if (!m_clusterCullingPass) return;

    RHIBuffer* transform = transformBuffer ? transformBuffer : m_dummyTransformBuffer_.get();
    if (!m_compactionBindingsDirty && m_boundTransformBuffer_ == transform) return;

    m_compactionGroup_->updateBuffer(0, m_geomTableBuffer_.get(), 0, 0);
    m_compactionGroup_->updateBuffer(1, m_clusterCullingPass->getVisibleIndicesBuffer(), 0, 0);
    m_compactionGroup_->updateBuffer(2, m_clusterCullingPass->getCounterBuffer(), 0, 0);
    m_compactionGroup_->updateBuffer(3, m_indexBuffer_.get(), 0, 0);
    m_compactionGroup_->updateBuffer(4, m_vertexBuffer_.get(), 0, 0);
    m_compactionGroup_->updateBuffer(5, m_expVertexBuffer_.get(), 0, 0);
    m_compactionGroup_->updateBuffer(6, m_drawArgsBuffer_.get(), 0, 0);

    m_boundTransformBuffer_ = transform;
    m_compactionBindingsDirty = false;

    // 绘制管线的 binding 1/2(geomTable + transforms)也要更新
    for (auto& group : m_bindingGroups_) {
        group->updateBuffer(1, m_geomTableBuffer_.get(), 0, 0);
        group->updateBuffer(2, transform, 0, 0);
    }
}

void NaniteDebugPass::prepareIndirectDraw(RHICommandBuffer* cmd) {
    if (!m_initialized || !m_renderDataBuilt || !enabled) return;
    if (!m_clusterCullingPass || !m_compactionPipeline_) return;

    RHIBuffer* transformBuffer = m_naniteManager ? m_naniteManager->getTransformBuffer() : nullptr;
    updateCompactionBindings(transformBuffer);

    // 1) drawArgs.vertexCount 清零(首 4B;instanceCount/first 恒定),
    //    随后屏障到 UAV(fill=transfer write,compaction=shader write)
    cmd->fillBuffer(m_drawArgsBuffer_.get(), 0, sizeof(uint32_t), 0);
    cmd->bufferBarrier(m_drawArgsBuffer_.get(), m_drawArgsBuffer_->getSize(),
                       RHIPipelineStage::Transfer, RHIPipelineStage::ComputeShader,
                       RHIAccessFlags::TransferWrite, RHIAccessFlags::ShaderWrite);
    // expVB 进入 UAV 可写(首帧 COMMON→UAV,后续帧 VCB→UAV)
    cmd->bufferBarrier(m_expVertexBuffer_.get(), m_expVertexBuffer_->getSize(),
                       RHIPipelineStage::ComputeShader, RHIPipelineStage::ComputeShader,
                       RHIAccessFlags::ShaderWrite, RHIAccessFlags::ShaderWrite);

    // 2) 源缓冲进入 UAV 读状态(首帧 COMMON→UAV,之后 no-op)
    auto toUavRead = [&](RHIBuffer* buf) {
        cmd->bufferBarrier(buf, buf->getSize(),
                           RHIPipelineStage::ComputeShader, RHIPipelineStage::ComputeShader,
                           RHIAccessFlags::ShaderRead, RHIAccessFlags::ShaderRead);
    };
    toUavRead(m_geomTableBuffer_.get());
    toUavRead(m_indexBuffer_.get());
    toUavRead(m_vertexBuffer_.get());
    RHIBuffer* transform = transformBuffer ? transformBuffer : m_dummyTransformBuffer_.get();
    toUavRead(transform);

    // 3) 几何展开 dispatch(visibleClusterCount 槽,按 cluster 总数保守派发)
    struct CompactionPush { uint32_t forceLOD, pad0, pad1, pad2; };
    cmd->bindComputePipeline(m_compactionPipeline_.get());
    cmd->setBindingGroup(0, m_compactionGroup_.get());
    CompactionPush pc{};
    pc.forceLOD = m_forceLOD >= 0 ? static_cast<uint32_t>(m_forceLOD) : 0xFFFFFFFFu;
    cmd->pushConstants(RHIShaderStage::Compute, 0, sizeof(pc), &pc);

    constexpr uint32_t kWorkgroupSize = 64;
    const uint32_t groups = (m_totalClusterCount + kWorkgroupSize - 1) / kWorkgroupSize;
    cmd->dispatch(groups, 1, 1);

    // 4) 输出屏障:expVB → 顶点输入;drawArgs → 间接参数
    cmd->bufferBarrier(m_expVertexBuffer_.get(), m_expVertexBuffer_->getSize(),
                       RHIPipelineStage::ComputeShader, RHIPipelineStage::VertexInput,
                       RHIAccessFlags::ShaderWrite, RHIAccessFlags::VertexAttributeRead);
    cmd->bufferBarrier(m_drawArgsBuffer_.get(), m_drawArgsBuffer_->getSize(),
                       RHIPipelineStage::ComputeShader, RHIPipelineStage::DrawIndirect,
                       RHIAccessFlags::ShaderWrite, RHIAccessFlags::IndirectCommandRead);
    // geomTable/transform 的 VS UAV 读(compute 写 → graphics 读的全局 UAV 屏障)
    cmd->pipelineBarrier(RHIPipelineStage::ComputeShader,
                         RHIPipelineStage::VertexShader,
                         RHIAccessFlags::ShaderWrite, RHIAccessFlags::ShaderRead);
}

// ============================================
// 绘制(render pass 内,单次 indirect draw)
// ============================================

void NaniteDebugPass::recordCommandsWithLOD(RHICommandBuffer* cmd, uint32_t frameIndex,
                                            const std::unordered_map<std::string, glm::mat4>& /*meshMatrices*/,
                                            Nanite::NaniteManager* naniteManager) {
    if (!m_initialized || !enabled) return;
    if (!m_renderDataBuilt || !m_drawArgsBuffer_) return;

    // ---- 统计(基于上一帧 readback 的可见列表;绘制本身零 CPU 依赖) ----
    m_drawnStats = DrawnStats{};
    m_drawnStats.totalClusters = m_totalClusterCount;
    if (m_clusterCullingPass) {
        const auto& visible = m_clusterCullingPass->getVisibleIndices();
        m_drawnStats.visibleClusters = static_cast<uint32_t>(visible.size());
        m_drawnStats.drawnClusters = m_drawnStats.visibleClusters;
        for (uint32_t idx : visible) {
            if (idx >= m_clusterRenderData.size()) continue;
            const auto& cd = m_clusterRenderData[idx];
            m_drawnStats.drawnTriangles += cd.indexCount / 3;
            m_drawnStats.drawnVertices += cd.vertexCount;
            if (cd.lodLevel < 8) m_drawnStats.lodClusterCounts[cd.lodLevel]++;
        }
        // forceLOD 诊断模式下 GPU 侧过滤,统计为未过滤口径(仅诊断显示用)
    }
    (void)naniteManager;

    // ---- 单次 indirect draw ----
    auto ext = rhiSwapChain_->getExtent();
    cmd->bindGraphicsPipeline(m_pipeline_.get());
    cmd->setViewport(0, 0, float(ext.width), float(ext.height));
    cmd->setScissor(0, 0, ext.width, ext.height);
    cmd->setBindingGroup(0, m_bindingGroups_[frameIndex].get());
    cmd->bindVertexBuffer(0, m_expVertexBuffer_.get());

    ClusterDebugPushConstants pc{};
    pc.totalClusters = m_totalClusterCount;
    pc.debugMode = static_cast<uint32_t>(m_debugMode);
    cmd->pushConstants(RHIShaderStage::Vertex | RHIShaderStage::Fragment, 0, sizeof(pc), &pc);

    cmd->drawIndirect(m_drawArgsBuffer_.get(), 0, 1, 16);

    // ======== 实时输出（每 60 帧）========
    static uint32_t frameCounter = 0;
    if (++frameCounter % 60 == 0) {
        std::cout << "\r[LOD] Drawn:" << m_drawnStats.drawnClusters << "/" << m_totalClusterCount
                  << " tris=" << m_drawnStats.drawnTriangles
                  << " verts=" << m_drawnStats.drawnVertices
                  << " | GPU-driven indirect (1 draw)"
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
// 其余接口
// ============================================

void NaniteDebugPass::ensureRenderDataBuilt() {
    if (!m_renderDataBuilt) {
        buildRenderData();
    }
}

void NaniteDebugPass::resize(uint32_t width, uint32_t height) {
    // Debug pass uses swapchain extent directly; nothing to recreate here.
    // Pipeline viewport/scissor are set dynamically in recordCommandsWithLOD.
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
