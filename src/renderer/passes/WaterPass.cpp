#include "WaterPass.h"
#include "GBufferPass.h"
#include "Mesh.h"

// Pure RHI headers — NO Vulkan backend headers
#include "RHIDevice.h"
#include "RHIBuffer.h"
#include "RHIDescriptor.h"
#include "RHIPipeline.h"
#include "RHICommandBuffer.h"
#include "RHITexture.h"
#include "RHISampler.h"

#include <stdexcept>
#include <iostream>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>

// =============================================================================
// Constructor / Destructor
// =============================================================================

WaterPass::WaterPass(RHIDevice* rhiDevice,
                     uint32_t width, uint32_t height, RHIRenderPass* externalRenderPass)
    : RenderPassBase(rhiDevice, width, height)
    , rhiDevice_(rhiDevice)
    , width_(width)
    , height_(height)
    , externalRenderPass_(externalRenderPass)
{
    passName = "Water Pass";

    createWaterMesh();
    createVertexAndIndexBuffers();
    createBindingLayout();
    createUniformBuffers();
    createBindingGroups();
    createPipeline();

    std::cout << "WaterPass created (Pure RHI): " << width_ << "x" << height_ << std::endl;
}

WaterPass::~WaterPass() { cleanup(); }

void WaterPass::cleanup() {
    if (rhiDevice_) rhiDevice_->waitIdle();
    bindingGroups_.clear();
    uniformBuffers_.clear();
    pipeline_.reset();
    bindingLayout_.reset();
    indexBuffer_.reset();
    vertexBuffer_.reset();
    waterMesh.reset();
}

void WaterPass::resize(uint32_t w, uint32_t h) { width_ = w; height_ = h; }

void WaterPass::setWaterColor(const glm::vec3& color, float alpha) {
    waterColor = color; waterAlpha = alpha;
}

// =============================================================================
// Mesh
// =============================================================================

void WaterPass::createWaterMesh() {
    waterMesh = std::make_unique<Mesh>();
    float size = 50.0f; int resolution = 32;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    float step = size / resolution, halfSize = size / 2.0f;

    for (int z = 0; z <= resolution; z++)
        for (int x = 0; x <= resolution; x++) {
            Vertex v;
            v.pos = glm::vec3(-halfSize + x * step, waterHeight, -halfSize + z * step);
            v.tangent = glm::vec3(1, 0, 0);
            v.normal = glm::vec3(0, 1, 0);
            v.texCoord = glm::vec2(float(x) / resolution, float(z) / resolution);
            vertices.push_back(v);
        }

    for (int z = 0; z < resolution; z++)
        for (int x = 0; x < resolution; x++) {
            int tl = z * (resolution + 1) + x, tr = tl + 1;
            int bl = (z + 1) * (resolution + 1) + x, br = bl + 1;
            indices.insert(indices.end(), {(uint32_t)tl,(uint32_t)bl,(uint32_t)tr,(uint32_t)tr,(uint32_t)bl,(uint32_t)br});
        }

    waterMesh->setVertices(vertices);
    waterMesh->setIndices(indices);
    indexCount_ = static_cast<uint32_t>(indices.size());
}

void WaterPass::createVertexAndIndexBuffers() {
    const auto& verts = waterMesh->getVertices();
    const auto& idxs = waterMesh->getIndices();

    { RHIBufferDesc d{}; d.size = sizeof(Vertex) * verts.size();
      d.usage = RHIBufferUsage::Vertex; d.memoryUsage = RHIMemoryUsage::GPUOnly;
      vertexBuffer_ = rhiDevice_->createBuffer(d);
      vertexBuffer_->uploadData(verts.data(), d.size); }

    { RHIBufferDesc d{}; d.size = sizeof(uint32_t) * idxs.size();
      d.usage = RHIBufferUsage::Index; d.memoryUsage = RHIMemoryUsage::GPUOnly;
      indexBuffer_ = rhiDevice_->createBuffer(d);
      indexBuffer_->uploadData(idxs.data(), d.size); }
}

// =============================================================================
// RHI Resources
// =============================================================================

void WaterPass::createBindingLayout() {
    RHIBindingLayoutDesc desc;
    desc.entries.push_back({0, RHIDescriptorType::UniformBuffer,
                            RHIShaderStage::Vertex | RHIShaderStage::Fragment, 1});
    for (uint32_t i = 1; i <= 4; ++i)
        desc.entries.push_back({i, RHIDescriptorType::CombinedImageSampler, RHIShaderStage::Fragment, 1});
    bindingLayout_ = rhiDevice_->createBindingLayout(desc);
}

void WaterPass::createUniformBuffers() {
    uniformBuffers_.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        RHIBufferDesc d{}; d.size = sizeof(WaterUBO);
        d.usage = RHIBufferUsage::Uniform; d.memoryUsage = RHIMemoryUsage::CPUToGPU;
        uniformBuffers_[i] = rhiDevice_->createBuffer(d);
    }
}

void WaterPass::createBindingGroups() {
    bindingGroups_.resize(MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        bindingGroups_[i] = rhiDevice_->allocateBindingGroup(bindingLayout_.get());
        bindingGroups_[i]->updateBuffer(0, uniformBuffers_[i].get(), 0, sizeof(WaterUBO));
    }
}

void WaterPass::createPipeline() {
    constexpr uint32_t stride = sizeof(Vertex);
    RHIColorBlendAttachment blend{};
    blend.blendEnable = true;
    blend.srcColorFactor = RHIBlendFactor::SrcAlpha;
    blend.dstColorFactor = RHIBlendFactor::OneMinusSrcAlpha;
    blend.colorBlendOp = RHIBlendOp::Add;
    blend.srcAlphaFactor = RHIBlendFactor::One;
    blend.dstAlphaFactor = RHIBlendFactor::Zero;
    blend.alphaBlendOp = RHIBlendOp::Add;

    auto builder = rhiDevice_->createGraphicsPipelineBuilder();
    builder->setVertexShader("shaders/water_vert.spv")
        .setFragmentShader("shaders/water_frag.spv")
        .addVertexBinding(0, stride, RHIVertexInputRate::Vertex)
        .addVertexAttribute(0, 0, RHIFormat::R32G32B32_SFLOAT, offsetof(Vertex, pos))
        .addVertexAttribute(0, 1, RHIFormat::R32G32B32_SFLOAT, offsetof(Vertex, normal))
        .addVertexAttribute(0, 2, RHIFormat::R32G32_SFLOAT,    offsetof(Vertex, texCoord))
        .addVertexAttribute(0, 3, RHIFormat::R32G32B32_SFLOAT, offsetof(Vertex, tangent))
        .setTopology(RHIPrimitiveTopology::TriangleList)
        .setCullMode(RHICullMode::None)
        .setFrontFace(RHIFrontFace::CounterClockwise)
        .setPolygonMode(RHIPolygonMode::Fill)
        .setDepthTest(false, false, RHICompareOp::Less)
        .setSampleCount(RHISampleCount::Count1)
        .addColorBlendAttachment(blend)
        .addBindingLayout(bindingLayout_.get())
        .setRenderPass(externalRenderPass_);

    pipeline_ = builder->build();
    std::cout << "Water pipeline created (Pure RHI)" << std::endl;
}

// =============================================================================
// Updates
// =============================================================================

void WaterPass::updateUniforms(const glm::mat4& view, const glm::mat4& projection,
                               const glm::vec3& cameraPos, float time, uint32_t frameIndex) {
    WaterUBO ubo{};
    ubo.model = glm::translate(glm::mat4(1.0f), glm::vec3(0, waterHeight, 0));
    ubo.view = view; ubo.projection = projection;
    ubo.invView = glm::inverse(view); ubo.invProjection = glm::inverse(projection);
    ubo.cameraPos = glm::vec4(cameraPos, 1);
    ubo.waterColor = glm::vec4(waterColor, waterAlpha);
    ubo.waterParams = glm::vec4(waveSpeed, waveStrength, time, refractionStrength);
    ubo.screenSize = glm::vec4(width_, height_, 0.1f, 100.0f);
    ubo.ssrParams = glm::vec4(ssrMaxDistance, ssrMaxSteps, ssrThickness, 0);

    void* ptr = uniformBuffers_[frameIndex]->map();
    memcpy(ptr, &ubo, sizeof(ubo));
    uniformBuffers_[frameIndex]->unmap();
}

void WaterPass::setGBufferInputs(GBufferPass* gbuffer, RHITexture* sceneColorTexture, RHISampler* sampler) {
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        bindingGroups_[i]->updateTexture(1, gbuffer->getPositionTexture(), sampler);
        bindingGroups_[i]->updateTexture(2, gbuffer->getNormalTexture(), sampler);
        bindingGroups_[i]->updateTexture(3, gbuffer->getDepthTexture(), sampler);
        bindingGroups_[i]->updateTexture(4, sceneColorTexture, sampler);
    }
}

// =============================================================================
// Render (Pure RHI)
// =============================================================================

void WaterPass::render(RHICommandBuffer* cmd, uint32_t frameIndex) {
    cmd->bindGraphicsPipeline(pipeline_.get());
    cmd->setBindingGroup(0, bindingGroups_[frameIndex].get());
    cmd->bindVertexBuffer(0, vertexBuffer_.get());
    cmd->bindIndexBuffer(indexBuffer_.get(), 0, RHIIndexType::UInt32);
    cmd->drawIndexed(indexCount_);
}

